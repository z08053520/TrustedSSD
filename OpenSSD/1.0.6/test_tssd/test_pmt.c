/* ===========================================================================
 * Unit test for Page Mapping Table (PMT) 
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "dram.h"
#include "pmt.h"
#include "mem_util.h"
#include "test_util.h"
#include <stdlib.h>

#define RAND_SEED	1234
#define MAX_LSPN	(SUB_PAGES_PER_PAGE * NUM_LPAGES)

#define LSPN_BUF	TEMP_BUF_ADDR
#define VP_BUF		HIL_BUF_ADDR	

#define BUF_SIZE	(BYTES_PER_PAGE / sizeof(UINT32))
#define SAMPLE_SIZE	BUF_SIZE	

SETUP_BUF(lspn,		LSPN_BUF,	SECTORS_PER_PAGE);
SETUP_BUF(vp,		VP_BUF,		SECTORS_PER_PAGE);

void ftl_test()
{
	UINT32 i, j;
	UINT32 lspn;
	vp_t   vp, expected_vp;
	vp_or_int vp_or_int;

	uart_print("Running unit test for PMT... ");
	uart_printf("sample size = %d\r\n", SAMPLE_SIZE);

	srand(RAND_SEED);
 	init_lspn_buf(0xFFFFFFFF);
	init_vp_buf(0);

	uart_print("\tsample lspn to check vp is set to 0 by default");
	for(i = 0; i < SAMPLE_SIZE; i++) {
		lspn = rand() % MAX_LSPN;
		pmt_fetch(lspn, &vp);
		vp_or_int.as_vp = vp;
		BUG_ON("vp of an unused lspn is not 0", vp_or_int.as_int != 0);
	}

	uart_print("\tupdate lspns");
	i = 0;
	while (i < SAMPLE_SIZE) {	
		lspn    = rand() % MAX_LSPN;	

		vp.bank = rand() % NUM_BANKS;
		vp.vpn  = rand() % PAGES_PER_BANK;
		vp_or_int.as_vp = vp;

		pmt_update(lspn, vp);

		j 	= mem_search_equ_dram(LSPN_BUF, sizeof(UINT32), 
					      BUF_SIZE, lspn);
		if (j < BUF_SIZE) {
			set_vp(j, vp_or_int.as_int);
		}
		else {
			set_lspn(i, lspn);
			set_vp(i, vp_or_int.as_int);
			i++;
		}
	}
	
	uart_print("\tverify lspn->vsp");
	for(i = 0; i < SAMPLE_SIZE; i++) {
		lspn = get_lspn(i);
		vp_or_int.as_int = get_vp(i);
		expected_vp = vp_or_int.as_vp; 

		pmt_fetch(lspn, &vp);
		/*  uart_printf("vp (bank, vpn) = (%u, %u); expected_vp (bank, vpn) = (%u, %u)\r\n",
			    vp.bank, vp.vpn, expected_vp.bank, expected_vp.vpn);*/
		BUG_ON("vp is not as expected", vp_not_equal(vp, expected_vp) );
	}
	
	uart_print("PMT passed the unit test ^_^");
}

#endif
