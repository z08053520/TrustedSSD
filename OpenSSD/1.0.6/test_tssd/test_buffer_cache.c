/* ===========================================================================
 * Unit test for Buffer Cache 
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "buffer_cache.h"
#include "cmt.h"
#include "gc.h"
#include "flash_util.h"
#include "test_util.h"
#include <stdlib.h>


/* ===========================================================================
 * Generate random LPN 
 * =========================================================================*/

#define RAND_SEED	123456
#define MAX_LPN		NUM_LPAGES

extern UINT32 get_vpn(UINT32 const lpn);

static UINT32 rand_new_lpn() 
{
	UINT32 lpn; 
	UINT32 buf;
	
	do {
		lpn = rand() % MAX_LPN;
		bc_get(lpn, &buf, BC_BUF_TYPE_USR);
	} while(buf);
	
	return lpn;
}

static UINT32 rand_add_lpn_to_bc(UINT32 *buf)
{
	// find a lpn not cached yet
	UINT32 lpn = rand_new_lpn();

	// make sure lpn->vpn mapping is in CMT
	// this is a requirement of putting a lpn into buffer cache 
	get_vpn(lpn);
	bc_put(lpn, buf, BC_BUF_TYPE_USR);

	return lpn;
}

/* ===========================================================================
 * DRAM buffer to store LPNs 
 * =========================================================================*/

#define LPN_BUF			TEMP_BUF_ADDR
#define BUF_SIZE		(BYTES_PER_PAGE)
#define MAX_BUF_ENTRIES		(BUF_SIZE / sizeof(UINT32))

static void init_dram()
{
	mem_set_dram(LPN_BUF, 0, BUF_SIZE);
}

static UINT32 get_lpn(UINT32 const i)
{
	return read_dram_32(LPN_BUF + sizeof(UINT32) * i);
}

static void set_lpn(UINT32 const i, UINT32 const lpn)
{
	write_dram_32(LPN_BUF + sizeof(UINT32) * i, lpn);
}

/* ===========================================================================
 * Tests 
 * =========================================================================*/

#define NUM_PUT_OPERATIONS	MAX_BUF_ENTRIES	
/* This function determines whether the page of lpn will be made dirty.
 * The intent of this function is to have more dirty pages than clean pages. */
#define IS_DIRTY(lpn)		(lpn % 7 != 0)

static void test_bc_get_put(void)
{
	uart_print("test buffer cache get/put operations");

	UINT32 lpn, vpn, buf, bank, val;
	UINT32 i;

	// try to get lpn from empty buffer cache
	lpn = 1234;
	bc_get(lpn, &buf, BC_BUF_TYPE_USR);
	BUG_ON("cache hit when empty: impossible", buf != NULL);

	uart_print("fill buffer cache with many, many entries "
		   "causing eviction in the process"); 
	for(i = 0; i < NUM_PUT_OPERATIONS; i++) {
		// add an uncached lpn to buffer cache
		lpn = rand_add_lpn_to_bc(&buf);
		set_lpn(i, lpn);

		// make some pages dirty
		if (IS_DIRTY(lpn)) {
			val = lpn;
			mem_set_dram(buf, val, BYTES_PER_PAGE);
			bc_set_valid_sectors(lpn, 0, SECTORS_PER_PAGE, 
					     BC_BUF_TYPE_USR);
			bc_set_dirty(lpn, BC_BUF_TYPE_USR);
		}
	}
	
	uart_print("fill the buffer with enough entries to evict every entry "
		   "that we have just put");
	for(i = 0; i < NUM_BC_BUFFERS; i++) {
		// find a lpn not cached yet
		lpn = rand_add_lpn_to_bc(&buf);
	}

	uart_print("validate buffers are evicted correctly to flash");
	for(i = 0; i < NUM_PUT_OPERATIONS; i++) {
		lpn = get_lpn(i);
		if (!IS_DIRTY(lpn)) continue;

		bank = lpn2bank(lpn);
		vpn  = get_vpn(lpn);
		fu_read_page(bank, vpn, TEMP_BUF_ADDR, 0);

		val  = lpn;
		BUG_ON("data is not as expected", 
			is_buff_wrong(TEMP_BUF_ADDR, val, 0, SECTORS_PER_PAGE));
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

	init_dram();

	srand(RAND_SEED);
	test_bc_get_put();
	test_bc_fill();

	uart_print("buffer cache passed unit test ^_^");
}


#endif
