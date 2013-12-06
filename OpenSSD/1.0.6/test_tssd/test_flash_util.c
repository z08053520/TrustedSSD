/* ===========================================================================
 * Unit test for flash util 
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "flash_util.h"
#include "gc.h"
#include <stdlib.h>

static BOOL8 is_buff_wrong(UINT32 buff_addr, UINT32 val,
			   UINT8 offset, UINT8 num_sectors)
{
	if (offset >= SECTORS_PER_PAGE || num_sectors == 0) return FALSE;	
/*  	
	UINT32 i;
	UINT32 v;
	UINT32 j;

	INFO("is_buff_wrong", "Print the first/last five UINT32 values in every valid sectors of buffer: from %d, to %d",
				offset, offset + num_sectors);
	for(j = 0; j < num_sectors; j++) {
		uart_printf("sector %d: ", offset + j);
		for(i = 0; i < 5; i++) {
			v = read_dram_32(buff_addr + BYTES_PER_SECTOR * (offset+j) + i * sizeof(UINT32));
			if (i) uart_printf(", ");
			uart_printf("%d", v);
		}
		uart_printf("... ");
		for(i = 5; i > 0; i--) {
			v = read_dram_32(buff_addr + BYTES_PER_SECTOR * (offset+j+1) - i * sizeof(UINT32));
			if (i != 5) uart_printf(", ");
			uart_printf("%d", v);
		}
		uart_print("");
	}
*/	
	buff_addr    	    = buff_addr + BYTES_PER_SECTOR * offset;
	UINT32 buff_entries = BYTES_PER_SECTOR * num_sectors / sizeof(UINT32);

    	UINT32 min_idx  = mem_search_min_max(
				buff_addr,    sizeof(UINT32), 
				buff_entries, MU_CMD_SEARCH_MIN_DRAM);
	UINT32 min_val  = read_dram_32(buff_addr + min_idx * sizeof(UINT32));
	if (min_val != val) {
		uart_printf("expect min val to be %d but was %d, at position %d\r\n", val, min_val, min_idx);
		return TRUE;
	}
    	
	UINT32 max_idx  = mem_search_min_max(
				buff_addr,    sizeof(UINT32),
                              	buff_entries, MU_CMD_SEARCH_MAX_DRAM);
	UINT32 max_val  = read_dram_32(buff_addr + max_idx * sizeof(UINT32));
	if (max_val != val) {
		uart_printf("expect max val to be %d but was %d, at position %d\r\n", val, min_val);
		return TRUE;
	}

	return FALSE;
}

#define RAND_SEED	123456

void ftl_test()
{
	UINT32 bank;
	UINT32 vpn[NUM_BANKS];
	UINT32 val;
	UINT8  offset, num_sectors;
	BOOL8  wrong;
	UINT32 mask;

	UINT32 i, repeats = 128;

	srand(RAND_SEED);

	uart_print("test read/write page individually");
	for(i = 0; i < repeats; i++) {
		uart_printf("\tstarting round %d...\r\n", i);

		uart_print ("\twrite a page each bank, then read it back to check");
		FOR_EACH_BANK(bank) {
			// prepare write buf
			val = bank;
			mem_set_dram(COPY_BUF(bank), val, BYTES_PER_PAGE);
		
			// write to flash
			vpn[bank] = gc_allocate_new_vpn(bank);
			fu_write_page(bank, vpn[bank], COPY_BUF(bank));

			// read back and check
			fu_read_page(bank, vpn[bank], TEMP_BUF_ADDR, 0);
			wrong = is_buff_wrong(TEMP_BUF_ADDR, val, 0, SECTORS_PER_PAGE);
			BUG_ON("data read from flash is not the same as data written to flash", wrong);
		}
		uart_print("\tdone");

		uart_print ("\ttest whether flash utility honors valid sector mask");
		FOR_EACH_BANK(bank) {
			// prepare buff
			val = rand();
			offset = rand() % SECTORS_PER_PAGE;
			num_sectors = rand() % (SECTORS_PER_PAGE - offset) + 1;
			mem_set_dram(TEMP_BUF_ADDR + offset * BYTES_PER_SECTOR,
					val, num_sectors * BYTES_PER_SECTOR);

			// do read with mask
			mask = num_sectors == SECTORS_PER_PAGE ? 
					0xFFFFFFFF : ((1<<num_sectors)-1) << offset;
			
			fu_read_page(bank, vpn[bank], TEMP_BUF_ADDR, mask);

			// Check sectors [0, offset) == bank
			BUG_ON("left hole wrong", 
				is_buff_wrong(TEMP_BUF_ADDR, bank, 0, offset));
			// Check sectors [offset, offset + num_sectors) == val
			BUG_ON("middle data wrong", 
				is_buff_wrong(TEMP_BUF_ADDR, val, offset, num_sectors));
			// Check sectors [offset + num_sectors, SECTORS_PER_PAGE) 
			BUG_ON("right hole wrong", 
				is_buff_wrong(TEMP_BUF_ADDR, bank, offset + num_sectors, 
						SECTORS_PER_PAGE - offset - num_sectors));
		}
		uart_print("\tdone\r\n");
	}

	uart_print("flash utility passed unit test ^_^");
}

#endif
