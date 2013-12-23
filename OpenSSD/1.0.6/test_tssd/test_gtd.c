/* ===========================================================================
 * Unit test for Global Translation Directory (GTD)
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "gtd.h"
#include <stdlib.h>

#define RAND_SEED	1234
#define NUM_TRIALS	2048

static void test_pmt(void)
{
	UINT32 i;
	UINT32 pmt_idx;
	vsp_t   vsp;

	uart_print("Test GTD with PMT entries");

	/* test initial state */
	for (i = 0; i < NUM_TRIALS; i++) {
		pmt_idx = rand() % PMT_PAGES; 
		vsp = gtd_get_vsp(pmt_idx, GTD_ZONE_TYPE_PMT);
		BUG_ON("vsp is not properly initialized", vsp.bank != 0 || vsp.vspn != 0);
	}

	/* set and check */
	for (i = 0; i < NUM_TRIALS; i++) {
		pmt_idx  = rand() % PMT_PAGES;
		vsp.bank = rand() % NUM_BANKS;
		vsp.vspn = rand() % PAGES_PER_BANK;
		gtd_set_vsp(pmt_idx, vsp, GTD_ZONE_TYPE_PMT);
		BUG_ON("vpn wrong", vsp_not_equal(vsp, gtd_get_vsp(pmt_idx, GTD_ZONE_TYPE_PMT)));
	}
}

#if OPTION_ACL
static void test_sot(void)
{
	UINT32 i;
	UINT32 sot_idx;
	UINT32 vpn;

	uart_print("Test GTD with SOT entries");

	/* test initial state */
	for (i = 0; i < NUM_TRIALS; i++) {
		sot_idx = rand() % SOT_PAGES; 
		vpn = gtd_get_vp(sot_idx, GTD_ZONE_TYPE_SOT);
		BUG_ON("vpn is not initialized as 0", vpn != 0);
	}

	/* set and check */
	for (i = 0; i < NUM_TRIALS; i++) {
		sot_idx = rand() % SOT_PAGES;
		vpn = rand() % PAGES_PER_BANK;
		gtd_set_vpn(sot_idx, vpn, GTD_ZONE_TYPE_SOT);
		BUG_ON("vpn wrong", vpn != gtd_get_vpn(sot_idx, GTD_ZONE_TYPE_SOT));
	}
}
#endif

void ftl_test(void)
{

	INFO("test", "start testing GTD");

        srand(RAND_SEED);

	test_pmt();
#if OPTION_ACL
	test_sot();
#endif
	uart_print("GTD passed unit test ^_^");
}

#endif
