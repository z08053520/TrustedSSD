/* ===========================================================================
 * Unit test for Page Mapping Table (PMT) 
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "pmt.h"
#include "gc.h"
#include <stdlib.h>

#define RAND_SEED	123456
// a sample size that much bigger than CMT or BC
#define SAMPLE_SIZE	16384
#define MAX_LPN		NUM_LPAGES

void ftl_test()
{
	UINT32 i;
	UINT32 lpn, vpn;
	UINT32 lpns[SAMPLE_SIZE], vpns[SAMPLE_SIZE];

	uart_print("Running unit test for PMT... ");

	srand(RAND_SEED);
	uart_print("\tsample lpn to check vpn is set to 0 by default");
	for(i = 0; i < SAMPLE_SIZE; i++) {
		lpn = rand() % MAX_LPN;
		pmt_fetch(lpn, &vpn);
		BUG_ON("vpn of an unused lpn is not 0", vpn != 0);
	}
	uart_print("\tupdate lpns");
	for(i = 0; i < SAMPLE_SIZE; i++) {
		lpns[i] = lpn = rand() % MAX_LPN;
		vpns[i] = vpn = gc_allocate_new_vpn(lpn2bank(lpn));
		pmt_update(lpn, vpn);
	}
	uart_print("\tverify lpn->vpn");
	for(i = 0; i < SAMPLE_SIZE; i++) {
		pmt_fetch(lpns[i], &vpn);
		BUG_ON("vpn is not as expected", vpn != vpns[i]);
	}

	uart_print("PMT passed the unit test ^_^");
}


#endif
