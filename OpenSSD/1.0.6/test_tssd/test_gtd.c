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
	UINT32 vpn;

	print("Test GTD with PMT entries");

	/* test initial state */
	for (i = 0; i < NUM_TRIALS; i++) {
		pmt_idx = rand() % PMT_PAGES; 
		vpn = gtd_get_vpn(pmt_idx, GTD_ZONE_TYPE_PMT);
		BUG_ON("vpn is not initialized as 0", vpn != 0);
	}

	/* set and check */
	for (i = 0; i < NUM_TRIALS; i++) {
		pmt_idx = rand() % PMT_PAGES;
		vpn = rand() % PAGES_PER_BANK;
		gtd_set_vpn(pmt_idx, vpn, GTD_ZONE_TYPE_PMT);
		BUG_ON("vpn wrong", vpn != gtd_get_vpn(pmt_idx, GTD_ZONE_TYPE_PMT));
	}
}

#if OPTION_ACL
static void test_sot(void)
{
	UINT32 i;
	UINT32 sot_idx;
	UINT32 vpn;

	print("Test GTD with SOT entries");

	/* test initial state */
	for (i = 0; i < NUM_TRIALS; i++) {
		sot_idx = rand() % SOT_PAGES; 
		vpn = gtd_get_vpn(sot_idx, GTD_ZONE_TYPE_SOT);
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
	test_sot();

	uart_print("GTD passed unit test ^_^");
}

#endif
