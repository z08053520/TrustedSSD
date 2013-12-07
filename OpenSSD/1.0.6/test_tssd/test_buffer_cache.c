/* ===========================================================================
 * Unit test for Buffer Cache 
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "buffer_cache.h"
#include "cmt.h"
#include "gc.h"
#include <stdlib.h>

#define RAND_SEED	123456
#define MAX_LPN		NUM_LPAGES

static void test_bc_get_put(void)
{
	uart_print("test buffer cache get/put operations");

	UINT32 err;
	UINT32 lpn, vpn, buf, bank;
	UINT32 lpns[NUM_BC_BUFFERS], 
	       vpns[NUM_BC_BUFFERS],
	       bufs[NUM_BC_BUFFERS];
	UINT32 i;

	// try to get lpn from empty buffer cache
	lpn = 1234;
	bc_get(lpn, &buf);
	BUG_ON("cache hit when empty: impossible", addr != NULL);

	// fill buffer cache until full
	for(i = 0; i < NUM_BC_BUFFERS; i++) {
		// find a lpn not cached yet
		do {
			lpn = rand() % MAX_LPN;
			err = cmt_get(lpn, &vpn);
		} while(!err);
		lpn[i] = lpn;
	
		bank   = lpn2bank(lpn[i]);
		vpn[i] = gc_allocate_new_vpn(bank);

		err = cmt_add(lpn[i], vpn[i]);
		BUG_ON("failed to add <lpn->vpn> to CMT", err);

		bc_put(lpn[i], &bufs[i], BC_BUF_TYPE_USR);
		mem_set_dram(bufs[i], lpn[i], BYTES_PER_PAGE);
		bc_set_dirty()
	}
	
	// no evition yet, right?
	for(i = 0; i < NUM_BC_BUFFERS; i++) {
		
	}
	uart_print("done");
}

static void test_bc_fill(void)
{
	uart_print("test buffer cache fill operations");

	uart_print("done");
}

void ftl_test(void)
{
	INFO("test", "start testing buffer cache...");

	srand(RAND_SEED);
	test_bc_get_put();
	test_bc_fill();

	uart_print("buffer cache passed unit test ^_^");
}


#endif
