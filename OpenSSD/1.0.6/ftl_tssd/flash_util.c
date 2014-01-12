#include "flash_util.h"
#include "ftl.h"
#include "bad_blocks.h"

/* ========================================================================== 
 * Private Functions 
 * ========================================================================*/

/* ========================================================================== 
 * Public Interface 
 * ========================================================================*/

void fu_format(const UINT32 from_vblk)
{
	UINT32 vblk;
	UINT32 bank;

	for (vblk = from_vblk; vblk < VBLKS_PER_BANK; vblk++)
	{
		FOR_EACH_BANK(bank)
		{
            		if (bb_is_bad(bank, vblk))
				continue;
				
			nand_block_erase(bank, vblk);
            	}
        }
}

UINT8 fu_get_idle_bank()
{
	static UINT8 bank = NUM_BANKS - 1;

	do {
		bank = (bank + 1) % NUM_BANKS;
	} while (BSP_FSM(bank) != BANK_IDLE);
	return bank;
}

void fu_read_sub_page(const vsp_t vsp, const UINT32 buff_addr, const BOOL8 is_async)
{
	BUG_ON("should not read vpn #0", vsp.vspn < SUB_PAGES_PER_PAGE);

	UINT32 vpn    = vsp.vspn / SUB_PAGES_PER_PAGE;
	UINT32 offset = vsp.vspn % SUB_PAGES_PER_PAGE * SECTORS_PER_SUB_PAGE;
	nand_page_ptread(vsp.bank, 
			 vpn / PAGES_PER_VBLK,
			 vpn % PAGES_PER_VBLK,
			 offset, 
			 SECTORS_PER_SUB_PAGE,
			 buff_addr, 
			 is_async ? RETURN_ON_ISSUE : RETURN_WHEN_DONE);
}

void fu_write_page(const vp_t vp, const UINT32 buff_addr)
{
	BUG_ON("can't write to vpn #0", vp.vpn == 0);

	nand_page_program(vp.bank, 
			  vp.vpn / PAGES_PER_VBLK, 
			  vp.vpn % PAGES_PER_VBLK, 
			  buff_addr);
}

