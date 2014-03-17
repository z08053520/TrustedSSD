/* ===========================================================================
 * Unit test for Page Mapping Table (PMT)
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "dram.h"
#include "scheduler.h"
#include "pmt_thread.h"
#include "mem_util.h"
#include "test_util.h"
#include <stdlib.h>

#define RAND_SEED	1234
#define MAX_LSPN	(SUB_PAGES_PER_PAGE * NUM_LPAGES)

#define LSPN_BUF	TEMP_BUF_ADDR
#define VP_BUF		HIL_BUF_ADDR

#define BUF_SIZE	(BYTES_PER_PAGE / sizeof(UINT32))
/* #define SAMPLE_SIZE	BUF_SIZE */
#define SAMPLE_SIZE	(BUF_SIZE / 8)
/* #define SAMPLE_SIZE	64 */

SETUP_BUF(lspn,		LSPN_BUF,	SECTORS_PER_PAGE);
SETUP_BUF(vp,		VP_BUF,		SECTORS_PER_PAGE);

static void load_pmt(UINT32 const lspn)
{
	UINT32 lpn = lspn / SUB_PAGES_PER_PAGE;
	pmt_load(lpn);

	while (!pmt_is_loaded(lpn)) schedule();
}

void ftl_test()
{
	UINT32 i, j;
	UINT32 lspn, lpn, sp_i;
	vp_t   vp, expected_vp;

	uart_print("Running unit test for PMT... ");
	uart_printf("sample size = %d\r\n", SAMPLE_SIZE);

	srand(RAND_SEED);
 	init_lspn_buf(0xFFFFFFFF);
	init_vp_buf(0);

	thread_t *pmt_thread = thread_allocate();
	pmt_thread_init(pmt_thread);
	enqueue(pmt_thread);

	uart_print("\tsample lspn to check vp is set to 0 by default");
	for(i = 0; i < SAMPLE_SIZE; i++) {
		lspn = rand() % MAX_LSPN;
		lpn = lspn / SUB_PAGES_PER_PAGE;
		sp_i = lspn % SUB_PAGES_PER_PAGE;

		load_pmt(lspn);
		pmt_get_vp(lpn, sp_i, &vp);
		BUG_ON("vpn of an unused lspn is not 0", vp.vpn != 0);
	}

	uart_print("\tupdate lspns");
	i = 0;
	while (i < SAMPLE_SIZE) {
		lspn = rand() % MAX_LSPN;
		lpn = lspn / SUB_PAGES_PER_PAGE;
		sp_i = lspn % SUB_PAGES_PER_PAGE;

		vp.bank = rand() % NUM_BANKS;
		vp.vpn  = rand() % PAGES_PER_BANK;

		load_pmt(lspn);
		pmt_update_vp(lpn, sp_i, vp);

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
		lpn = lspn / SUB_PAGES_PER_PAGE;
		sp_i = lspn % SUB_PAGES_PER_PAGE;
		expected_vp.as_uint = get_vp(i);

		load_pmt(lspn);
		pmt_get_vp(lpn, sp_i, &vp);
		/*  uart_printf("vp (bank, vpn) = (%u, %u); expected_vp (bank, vpn) = (%u, %u)\r\n",
			    vp.bank, vp.vpn, expected_vp.bank, expected_vp.vpn);*/
		BUG_ON("vp is not as expected", vp_not_equal(vp, expected_vp) );
	}

	uart_print("PMT passed the unit test ^_^");
}

#endif
