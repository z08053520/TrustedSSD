#include "flash_util.h"
#include "ftl.h"

/* ========================================================================== 
 * Private Functions 
 * ========================================================================*/

#define is_sector_valid(i, mask) 	(((mask >> i) & 1) == 1)

static void merge_buff(UINT32 const target_buf_addr, 
		       sectors_mask_t const valid_sectors_mask, 
		       UINT32 const src_buf_addr)
{
	UINT8 begin = 0, end = 0; 

	BUG_ON("invalid arguments", target_buf_addr < DRAM_BASE || 
				    src_buf_addr    < DRAM_BASE);

	while (1) {
		// find the first invalid sector
		while (begin < SECTORS_PER_PAGE && is_sector_valid(begin, valid_sectors_mask))
			begin++;

		if (begin >= SECTORS_PER_PAGE) return;

		// find the last invalid sector
		end = begin + 1;
		while (end < SECTORS_PER_PAGE && !is_sector_valid(end, valid_sectors_mask))
			end++;

		// fill sectors from begin to end (exclusive)
		INFO("fu>merge", "targe_buf_addr = %d, src_buf_addr = %d, begin = %d, end = %d", 
				target_buf_addr, src_buf_addr, begin, end);
		mem_copy(target_buf_addr + begin * BYTES_PER_SECTOR,
			 src_buf_addr    + begin * BYTES_PER_SECTOR, 
			 (end - begin) * BYTES_PER_SECTOR);

		begin = end + 1;
	}
}

static BOOL32 read_page_with_mask(UINT32 const bank, 
				UINT32 const vpn,
		      		UINT32 const buff_addr, 
				sectors_mask_t const valid_sectors_mask,
		      		UINT32 const sync)
{
	UINT8 valid_sectors = __builtin_popcount(valid_sectors_mask);
	UINT8 low_holes     = __builtin_ctz(valid_sectors_mask);
	UINT8 high_holes    = __builtin_clz(valid_sectors_mask);

	BUG_ON("invalid argument", buff_addr < DRAM_BASE);

	if (valid_sectors >= SECTORS_PER_PAGE) return 0;

	/* page not written to flash yet */
	if (!vpn) {
		mem_set_dram(FTL_BUF(bank), 0xFFFFFFFF, BYTES_PER_PAGE);
		merge_buff(buff_addr, valid_sectors_mask, FTL_BUF(bank));
		return 0;
	}

	/* this is an unproved performance optimization */
	if (valid_sectors > 16 && (high_holes + valid_sectors + low_holes == SECTORS_PER_PAGE)) {
		if (high_holes) {
			nand_page_ptread(bank, 
				 	 vpn / PAGES_PER_VBLK, 
				 	 vpn % PAGES_PER_VBLK, 
			 	 	 low_holes + valid_sectors, 
					 high_holes, 
				 	 buff_addr, 
					 sync);
		}
		if (low_holes) {
			nand_page_ptread(bank, 
				 	 vpn / PAGES_PER_VBLK, 
				 	 vpn % PAGES_PER_VBLK, 
			 	 	 0, 
					 low_holes, 
				 	 buff_addr, 
					 sync);
		}
	}
	else if (valid_sectors == 0) {
		nand_page_ptread(bank, 
				 vpn / PAGES_PER_VBLK, 
				 vpn % PAGES_PER_VBLK, 
			 	 0, SECTORS_PER_PAGE, 
				 buff_addr, sync);
	}
	else {
		nand_page_ptread(bank, 
				 vpn / PAGES_PER_VBLK, 
				 vpn % PAGES_PER_VBLK, 
			 	 0, SECTORS_PER_PAGE, 
				 FTL_BUF(bank), sync);

		if (sync != RETURN_WHEN_DONE)
			return 1;	// need merge
		
		merge_buff(buff_addr, valid_sectors_mask, FTL_BUF(bank));
	}
	return 0; // don't need merge
}

/* ========================================================================== 
 * Public Interface 
 * ========================================================================*/

void fu_format(UINT32 const from_vblk)
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

void fu_read_page(UINT32 const bank, UINT32 const vpn, 
		    	    UINT32 const buff_addr, sectors_mask_t const valid_sectors_mask)
{
	read_page_with_mask(bank, vpn, buff_addr, 
			    valid_sectors_mask, RETURN_WHEN_DONE);	
}

#define can_skip_bank(bank)	(buff_addr[bank] == 0)

void fu_read_pages_in_parallel( UINT32 vpn[], 
				UINT32 buff_addr[],
				sectors_mask_t valid_sectors_mask[])
{
	UINT32 bank;
	BOOL32 need_merge[NUM_BANKS];

	FOR_EACH_BANK(bank) {
		if (can_skip_bank(bank)) continue;

		need_merge[bank] = read_page_with_mask(bank, 
				    		       vpn[bank], 
				    		       buff_addr[bank], 
			    	                       valid_sectors_mask[bank], 
				                       RETURN_ON_ISSUE);	
	}
	flash_finish();
	
	FOR_EACH_BANK(bank) {
		if (can_skip_bank(bank) || !need_merge[bank]) continue;

		merge_buff(buff_addr[bank], valid_sectors_mask[bank], FTL_BUF(bank));
	}
}

void fu_write_page(UINT32 const bank, UINT32 const vpn, UINT32 const buff_addr)
{
	BUG_ON("can't write to vpn #0", vpn == 0);

	nand_page_program(bank, 
			  vpn / PAGES_PER_VBLK, 
			  vpn % PAGES_PER_VBLK, 
			  buff_addr);
}

void fu_write_pages_in_parallel(UINT32 vpn[],
				UINT32 buff_addr[])
{
	UINT32 bank;

	FOR_EACH_BANK(bank) {
		if (can_skip_bank(bank)) continue;

		fu_write_page(bank, vpn[bank], buff_addr[bank]);	
	}
	// This seemingly-natural line of flash_finish() is the wisdom 
	// comes from a painful debugging effort. I am not sure wait for 
	// flash to finish is the best way to go.
	//
	// TODO: improve performance by eliminating synchronous flash writes
	flash_finish();
}
