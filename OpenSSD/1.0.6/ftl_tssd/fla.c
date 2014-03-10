#include "fla.h"
#include "bad_blocks.h"

void fla_format_all(UINT32 const from_vblk)
{
	for (UINT32 vblk = from_vblk; vblk < VBLKS_PER_BANK; vblk++)
	{
		UINT8 bank;
		FOR_EACH_BANK(bank)
		{
            		if (bb_is_bad(bank, vblk))
				continue;

			nand_block_erase(bank, vblk);
            	}
        }
}

BOOL8 fla_is_bank_idle(UINT8 const bank)
{
	return FALSE;
}

BOOL8 fla_is_bank_complete(UINT8 const bank)
{
	return FALSE;
}

void fla_read_page(vp_t const vp, UINT8 const sect_offset,
			UINT8 const num_sectors, UINT32 const rd_buf)
{
	nand_page_ptread(vp.bank,
			 vp.vpn / PAGES_PER_VBLK,
			 vp.vpn % PAGES_PER_VBLK,
			 sect_offset,
			 num_sectors,
			 rd_buf,
			 RETURN_ON_ISSUE);
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
