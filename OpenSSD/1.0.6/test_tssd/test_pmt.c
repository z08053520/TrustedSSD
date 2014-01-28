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

static void load_pmt(UINT32 const lspn)
{
	UINT32		lpn  = lspn / SUB_PAGES_PER_PAGE;
	BOOL8		idle = FALSE;
	task_res_t	res  = pmt_load(lpn);
	while (res != TASK_CONTINUED) {
		BUG_ON("task engine is idle, but pmt page haven't loaded", idle);
		idle = task_engine_run();
		res  = pmt_load(lpn);
	}
}

void ftl_test()
{
	UINT32 i, j;
	UINT32 lspn;
	vp_t   vp, expected_vp;

	uart_print("Running unit test for PMT... ");
	uart_printf("sample size = %d\r\n", SAMPLE_SIZE);

	srand(RAND_SEED);
 	init_lspn_buf(0xFFFFFFFF);
	init_vp_buf(0);

	uart_print("\tsample lspn to check vp is set to 0 by default");
	for(i = 0; i < SAMPLE_SIZE; i++) {
		lspn = rand() % MAX_LSPN;
		load_pmt(lspn);
		pmt_fetch(lspn, &vp);
		BUG_ON("vpn of an unused lspn is not 0", vp.vpn != 0);
	}

	uart_print("\tupdate lspns");
	i = 0;
	while (i < SAMPLE_SIZE) {	
		lspn    = rand() % MAX_LSPN;	

		vp.bank = rand() % NUM_BANKS;
		vp.vpn  = rand() % PAGES_PER_BANK;

		load_pmt(lspn);
		pmt_update(lspn, vp);

		j 	= mem_search_equ_dram(LSPN_BUF, sizeof(UINT32), 
					      BUF_SIZE, lspn);
		if (j < BUF_SIZE) {
			set_vp(j, vp.as_uint);
		}
		else {
			set_lspn(i, lspn);
			set_vp(i, vp.as_uint);
			i++;
		}
	}
	
	uart_print("\tverify lspn->vsp");
	for(i = 0; i < SAMPLE_SIZE; i++) {
		lspn = get_lspn(i);
		expected_vp.as_uint = get_vp(i);

		load_pmt(lspn);
		pmt_fetch(lspn, &vp);
		/*  uart_printf("vp (bank, vpn) = (%u, %u); expected_vp (bank, vpn) = (%u, %u)\r\n",
			    vp.bank, vp.vpn, expected_vp.bank, expected_vp.vpn);*/
		BUG_ON("vp is not as expected", vp_not_equal(vp, expected_vp) );
	}
	
	uart_print("PMT passed the unit test ^_^");
}

#endif
