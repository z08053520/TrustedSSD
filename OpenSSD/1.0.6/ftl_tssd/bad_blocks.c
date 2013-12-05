#include "bad_blocks.h"
#include "ftl.h"

#define _bmp_base_addr(bank)	(BAD_BLK_BMP_ADDR + bank * BAD_BLK_BMP_BYTES_PER_BANK)
#define _bb_set_bmp(bank, blk)	set_bit_dram(_bmp_base_addr(bank), blk)
#define _bb_tst_bmp(bank, blk)	tst_bit_dram(_bmp_base_addr(bank), blk)

void bb_init()
{
	UINT32 bank, num_entries;
	scan_list_t* scan_list = (scan_list_t*) TEMP_BUF_ADDR;
	UINT32 i;

	INFO("bb>init", "bad block bitmap initialization");
	
	mem_set_dram(BAD_BLK_BMP_ADDR, 0, BAD_BLK_BMP_BYTES);

	disable_irq();

	flash_clear_irq();

	FOR_EACH_BANK(bank)	
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

		BUG_ON("data corruption", BSP_INTR(bank) & FIRQ_DATA_CORRUPT);

		num_entries = read_dram_16(&(scan_list->num_entries));
		BUG_ON("too many entries for scan list", num_entries > SCAN_LIST_ITEMS);

		INFO("bb>init", "bank %d: # of bad blocks = %d", bank, num_entries);

		uart_printf("\tbank %d: ", bank);
		for (i = 0; i < num_entries; i++)
		{
			UINT16 entry = read_dram_16(scan_list->list + i);
			UINT16 pblk_offset = entry & 0x7FFF;

			BUG_ON("invalid entry in scan list", 
					pblk_offset == 0 || pblk_offset >= PBLKS_PER_BANK);
			write_dram_16(scan_list->list + i, pblk_offset);

			uart_printf(i ? ", %d" : "%d", pblk_offset);
#if OPTION_2_PLANE
			_bb_set_bmp(bank, pblk_offset / 2);
#else
			_bb_set_bmp(bank, pblk_offset);
#endif
			BUG_ON("should be bad but not tested", !bb_is_bad(bank, pblk_offset));
		}
		uart_print("");
	}
	
	enable_irq();
}

BOOL32 bb_is_bad(UINT32 const bank, UINT32 const blk)
{    
	return _bb_tst_bmp(bank, blk);
}
