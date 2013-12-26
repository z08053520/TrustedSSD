/* ===========================================================================
 * Unit test for flash util 
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "flash_util.h"
#include "dram.h"
#include "gc.h"
#include "test_util.h"
#include <stdlib.h>

#define RAND_SEED	123456

#define VSP_BUF_ADDR	TEMP_BUF_ADDR
#define VAL_BUF_ADDR	HIL_BUF_ADDR

SETUP_BUF(vsp, 		VSP_BUF_ADDR, 		SECTORS_PER_PAGE);
SETUP_BUF(val,		VAL_BUF_ADDR,		SECTORS_PER_PAGE);

#define MAX_NUM_SP	(BYTES_PER_PAGE / sizeof(UINT32))

void ftl_test()
{
	uart_print("Start testing flash utility...");

	UINT32 num_vsp, total_vsp = MAX_NUM_SP;
	UINT8  expected_bank = 0;

	srand(RAND_SEED);

	init_vsp_buf(0);
	init_val_buf(0);

	uart_printf("%u sub pages are going to be written and verified\r\n", total_vsp);

	uart_print("Write pages with different value in each sub page");
	num_vsp = 0;
	while (num_vsp < total_vsp) {
		UINT8 bank 	    = fu_get_idle_bank();
		BUG_ON("bank not as expected", expected_bank != bank);

		UINT32 vpn 	    = gc_allocate_new_vpn(bank);
		vp_t   vp	    = {.bank = bank, .vpn = vpn};

		UINT32 vspn  	    = vpn * SUB_PAGES_PER_PAGE;
		UINT8  vsp_offset   = 0;	
		while (vsp_offset < SUB_PAGES_PER_PAGE && num_vsp < total_vsp) {
			vsp_t  vsp	    = {.bank = bank, .vspn = vspn};
			vsp_or_int vsp2int  = {.as_vsp = vsp};
			UINT32 val	    = rand();

			set_vsp(num_vsp, vsp2int.as_int);
			set_val(num_vsp, val);
			mem_set_dram(FTL_BUF(bank) + vsp_offset * BYTES_PER_SUB_PAGE,
				     val, BYTES_PER_SUB_PAGE);

			vspn++;
			vsp_offset++;
			num_vsp++;
		}
		
		fu_write_page(vp, FTL_BUF(bank));
		// take a break so that idle banks can be predictable
		flash_finish();
		
		expected_bank = (expected_bank + 1) % NUM_BANKS;
	}
	
	uart_print("Read sub pages to validate page writing operation");	
	num_vsp = 0;
	while (num_vsp < total_vsp) {
		vsp_or_int 	int2vsp = {.as_int = get_vsp(num_vsp)};
		vsp_t		vsp = int2vsp.as_vsp;	
		UINT32		val = get_val(num_vsp);

		fu_read_sub_page(vsp, COPY_BUF_ADDR);
		
		UINT8		sector_offset = vsp.vspn % SUB_PAGES_PER_PAGE * SECTORS_PER_SUB_PAGE;
		UINT8		wrong = is_buff_wrong(COPY_BUF_ADDR, 
						      val, 
				      		      sector_offset, 
						      SECTORS_PER_SUB_PAGE);
		BUG_ON("data read from flash is not the same as data written to flash", wrong);

		num_vsp++;
	}

	uart_print("Flash utility passed unit test ^_^");
}

#endif
