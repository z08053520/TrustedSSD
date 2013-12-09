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

static void test_bc_get_put(void)
{
	uart_print("test buffer cache get/put operations");

	UINT32 lpn, vpn, buf, bank, val;
	UINT32 lpns[NUM_BC_BUFFERS];
	UINT32 i;
	UINT32 last_num_free_pages[NUM_BANKS];
	UINT32 new_num_free_pages;

	// try to get lpn from empty buffer cache
	lpn = 1234;
	bc_get(lpn, &buf, BC_BUF_TYPE_USR);
	BUG_ON("cache hit when empty: impossible", buf != NULL);

	FOR_EACH_BANK(bank) {
		last_num_free_pages[bank] = gc_get_num_free_pages(bank);
	}

	uart_print("\r\nfill buffer cache until full (eviction may or may not happens)\r\n");
	for(i = 0; i < NUM_BC_BUFFERS; i++) {
		// add an uncached lpn to buffer cache
		lpns[i] = lpn = rand_add_lpn_to_bc(&buf);
		DEBUG("test>bc>get/put", "i=%d, lpn=%d", i, lpn);

		// for pages with even lpn, we modify the content of its buff
		if (lpn % 2 == 0) {
			val = lpn;
			mem_set_dram(buf, val, BYTES_PER_PAGE);
			bc_set_valid_sectors(lpn, 0, SECTORS_PER_PAGE, 
					     BC_BUF_TYPE_USR);
			bc_set_dirty(lpn, BC_BUF_TYPE_USR);
		}

		// check any changes in the # of free pages
		bank = lpn2bank(lpn);
		new_num_free_pages = gc_get_num_free_pages(bank);
		if (last_num_free_pages[bank] != new_num_free_pages)
			uart_print("evition happens");
	}
	
	uart_print("\r\nfill the buffer again to evict every buffer in the last round\r\n");
	for(i = 0; i < NUM_BC_BUFFERS; i++) {
		// find a lpn not cached yet
		lpn = rand_add_lpn_to_bc(&buf);
		uart_printf("lpn %d is added\r\n", lpn);
	}

	uart_print("\r\nvalidate buffers are evicted correctly to flash\r\n");
	for(i = 0; i < NUM_BC_BUFFERS; i++) {
		lpn = lpns[i];
		DEBUG("test>bc>get/put", "i=%d, lpn=%d", i, lpn);
		if (lpn % 2 == 1) continue;

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

	srand(RAND_SEED);
	test_bc_get_put();
	test_bc_fill();

	uart_print("buffer cache passed unit test ^_^");
}


#endif
