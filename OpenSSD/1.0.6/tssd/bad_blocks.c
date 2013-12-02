#include "bad_blocks.h"
#include "ftl.h"

void bb_init()
{
	UINT32 bank, num_entries, result, vblk_offset;
	scan_list_t* scan_list = (scan_list_t*) TEMP_BUF_ADDR;
	UINT32 i;

	mem_set_dram(BAD_BLK_BMP_ADDR, NULL, BAD_BLK_BMP_BYTES);

	disable_irq();

	flash_clear_irq();

	for (bank = 0; bank < NUM_BANKS; bank++)
	{
		SETREG(FCP_CMD, FC_COL_ROW_READ_OUT);
		SETREG(FCP_BANK, REAL_BANK(bank));
		SETREG(FCP_OPTION, FO_E);
		SETREG(FCP_DMA_ADDR, (UINT32) scan_list);
		SETREG(FCP_DMA_CNT, SCAN_LIST_SIZE);
		SETREG(FCP_COL, 0);
		SETREG(FCP_ROW_L(bank), SCAN_LIST_PAGE_OFFSET);
		SETREG(FCP_ROW_H(bank), SCAN_LIST_PAGE_OFFSET);

		SETREG(FCP_ISSUE, NULL);
		while ((GETREG(WR_STAT) & 0x00000001) != 0);
		
		while (BSP_FSM(bank) != BANK_IDLE);
		BUG_ON("scan list corruption", BSP_INTR(bank) & FIRQ_DATA_CORRUPT);

		num_entries = read_dram_16(&(scan_list->num_entries));
		BUG_ON("too many entries for scan list", num_entries > SCAN_LIST_ITEMS);

		for (i = 0; i < num_entries; i++)
		{
			UINT16 entry = read_dram_16(scan_list->list + i);
			UINT16 pblk_offset = entry & 0x7FFF;

			BUG_ON("invalid entry in scan list", 
					pblk_offset == 0 || pblk_offset >= PBLKS_PER_BANK);
			write_dram_16(scan_list->list + i, pblk_offset);
		}

		for (vblk_offset = 1; vblk_offset < VBLKS_PER_BANK; vblk_offset++)
			if (mem_search_equ_dram(scan_list, sizeof(UINT16), num_entries, vblk_offset) < num_entries)
				set_bit_dram(BAD_BLK_BMP_ADDR + bank*(VBLKS_PER_BANK/8 + 1), vblk_offset);
	}
	
	enable_irq();
}

BOOL32 bb_is_bad(UINT32 const bank, UINT32 const blk)
{    
	return tst_bit_dram(BAD_BLK_BMP_ADDR + bank*(VBLKS_PER_BANK/8 + 1), blk);
}
