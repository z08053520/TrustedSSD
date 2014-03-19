#include "fla.h"
#include "bad_blocks.h"
#include "mem_util.h"
#include "signal.h"

typedef UINT16 banks_mask_t;
/* 1 - idle; 0 - used */
static banks_mask_t idle_banks = 0xFFFF;
/* 1 - complete; 0 - not complete */
static banks_mask_t complete_banks = 0;

static UINT8 const num_banks =
#if OPTION_FTL_TEST && (MAX_NUM_THREADS < NUM_BANKS)
				MAX_NUM_THREADS;
#else
				NUM_BANKS;
#endif

/* notify scheduler for any banks state changes by signals */
extern signals_t g_scheduler_signals;
static inline void  update_scheduler_signals()
{
	signals_reset(g_scheduler_signals, SIG_ALL_BANKS);
	signals_set(g_scheduler_signals,
			SIG_BANKS(idle_banks | complete_banks));
}

static void use_bank(UINT8 const bank_i) {
	idle_banks &= ~(1 << bank_i);
	update_scheduler_signals();
}

void fla_format_all(UINT32 const from_vblk)
{
	for (UINT32 vblk = from_vblk; vblk < VBLKS_PER_BANK; vblk++)
	{
		for_each_bank(bank)
		{
            		if (bb_is_bad(bank, vblk))
				continue;

			nand_block_erase(bank, vblk);
            	}
        }
}

void fla_update_bank_state()
{
	/* Wait for all flash commands accepted */
	while ((GETREG(WR_STAT) & 0x00000001) != 0);

	banks_mask_t used_banks = ~idle_banks;
	/* update idle banks */
	idle_banks = 0;
	for_each_bank(bank_i) {
		if (BSP_FSM(bank_i) == BANK_IDLE)
			idle_banks |= (1 << bank_i);
	}
	/* update complete banks */
	complete_banks = used_banks & idle_banks;

	update_scheduler_signals();
}

BOOL8 fla_is_bank_idle(UINT8 const bank)
{
	return (idle_banks >> bank) & 1;
}

BOOL8 fla_is_bank_complete(UINT8 const bank)
{
	return (complete_banks >> bank) & 1;
}

UINT8 fla_get_idle_bank()
{
	static UINT8 bank_i = num_banks - 1;

	if (idle_banks == 0) return num_banks;

	for (UINT8 i = 0; i < num_banks; i++) {
		bank_i = (bank_i + 1) % num_banks;
		if (fla_is_bank_idle(bank_i)) return bank_i;
	}
	/* should never reach here */
	ASSERT(0);
	return num_banks;
}

void fla_read_page(vp_t const vp, UINT8 const sect_offset,
			UINT8 const num_sectors, UINT32 const rd_buf)
{
	ASSERT(fla_is_bank_idle(vp.bank));
	nand_page_ptread(vp.bank,
			 vp.vpn / PAGES_PER_VBLK,
			 vp.vpn % PAGES_PER_VBLK,
			 sect_offset,
			 num_sectors,
			 rd_buf,
			 RETURN_ON_ISSUE);
	use_bank(vp.bank);
}

void fla_write_page(vp_t const vp, UINT8 const sect_offset,
			UINT8 const num_sectors, UINT32 const wr_buf)
{
	ASSERT(fla_is_bank_idle(vp.bank));
	nand_page_ptprogram(vp.bank,
			    vp.vpn / PAGES_PER_VBLK,
			    vp.vpn % PAGES_PER_VBLK,
			    sect_offset,
			    num_sectors,
			    wr_buf);
	use_bank(vp.bank);
}

void fla_copy_buffer(UINT32 const target_buf, UINT32 const src_buf,
		    sectors_mask_t const mask)
{
	UINT8 sector_i = 0;
	while (sector_i < SECTORS_PER_PAGE) {
		// find the first sector to copy
		while (sector_i < SECTORS_PER_PAGE &&
		       ((mask >> sector_i) & 1) == 0) sector_i++;
		if (sector_i == SECTORS_PER_PAGE) break;
		UINT8 begin_sector = sector_i++;

		// find the last sector to copy
		while (sector_i < SECTORS_PER_PAGE &&
		       ((mask >> sector_i) & 1) == 1) sector_i++;
		UINT8 end_sector = sector_i++;

		mem_copy(target_buf + begin_sector * BYTES_PER_SECTOR,
			 src_buf    + begin_sector * BYTES_PER_SECTOR,
			 (end_sector - begin_sector) * BYTES_PER_SECTOR);
	}
}
