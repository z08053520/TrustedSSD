/* ===========================================================================
 * Unit test for Global Translation Directory (GTD)
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "gtd.h"
#include <stdlib.h>

#define RAND_SEED	1234
#define NUM_TRIALS	2048

void ftl_test(void)
{
	UINT32 i;
	UINT32 pmt_idx;
	UINT32 vpn;

	INFO("test", "start testing GTD");

        srand(RAND_SEED);

	/* test initial state */
	for (i = 0; i < NUM_TRIALS; i++) {
		pmt_idx = rand() % GTD_ENTRIES; 
		vpn = gtd_get_vpn(pmt_idx);
		BUG_ON("vpn is not initialized as 0", vpn != 0);
	}

	/* set and check */
	for (i = 0; i < NUM_TRIALS; i++) {
		pmt_idx = rand() % GTD_ENTRIES;
		vpn = rand() % PAGES_PER_BANK;
		gtd_set_vpn(pmt_idx, vpn);
		BUG_ON("vpn wrong", vpn != gtd_get_vpn(pmt_idx));
	}

	uart_print("GTD passed unit test ^_^");
}

#endif
