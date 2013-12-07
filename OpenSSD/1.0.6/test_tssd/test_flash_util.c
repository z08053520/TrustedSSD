/* ===========================================================================
 * Unit test for flash util 
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "flash_util.h"
#include "gc.h"
#include "test_util.h"
#include <stdlib.h>

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
