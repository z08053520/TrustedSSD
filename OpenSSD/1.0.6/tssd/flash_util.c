#include "flash_util.h"


static void merge_page(UINT32 const target_buf_addr, 
		       UINT32 const valid_sectors_mask, 
		       UINT32 const src_buf_addr)
{
	UINT8 begin = 0, end = 0; 

	while (begin < SECTORS_PER_PAGE) {
		while (begin < SECTORS_PER_PAGE && 
				(((valid_sectors_mask >> begin) & 1) == 1))
			begin++;

		if (begin >= SECTORS_PER_PAGE) break;

		end = begin + 1;
		while (end < SECTORS_PER_PAGE && 
				(((valid_sectors_mask >> end) & 1) == 0))
			end++;

		mem_copy(target_buf_addr + begin * BYTES_PER_SECTOR,
			 src_buf_addr + begin * BYTES_PER_SECTOR, 
			 (end - begin) * BYTES_PER_SECTOR);

		begin = end;
	}
}



void fu_read_page(UINT32 const bank, UINT32 const vpn, 
		  UINT32 const buff_addr, UINT32 const valid_sectors_mask
		  UINT32 const sync)
{
	UINT8 valid_sectors = __builtin_popcount(valid_sectors_mask);
	UINT8 low_holes     = __builtin_ctz(valid_sectors_mask);
	UINT8 high_holes    = __builtin_clz(valid_sectors_mask);

	/* this is an unproved performance optimization */
	if (valid_sectors > 16 && (high_holes + valid_sectors + low_holes == SECTORS_PER_PAGE)) {
		if (high_holes) {
			nand_page_ptread(bank, 
				 	 vpn / PAGES_PER_VBLK, 
				 	 vpn % PAGES_PER_VBLK, 
			 	 	 low_holes + valid_sectors, 
					 high_holes, 
				 	 addr, sync);
		}
		if (low_holes) {
			nand_page_ptread(bank, 
				 	 vpn / PAGES_PER_VBLK, 
				 	 vpn % PAGES_PER_VBLK, 
			 	 	 0, 
					 low_holes, 
				 	 addr, sync);
		}
	}
	else {
		nand_page_ptread(bank, 
				 vpn / PAGES_PER_VBLK, 
				 vpn % PAGES_PER_VBLK, 
			 	 0, SECTORS_PER_PAGE, 
				 FTL_BUF(bank), sync);
		merge_buf(buff_addr, valid_sectors_mask, FTL_BUF(bank));
	}
}
