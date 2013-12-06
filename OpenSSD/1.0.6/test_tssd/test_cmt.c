/* ===========================================================================
 * Unit test for Cached Mapping Table (CMT)
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "cmt.h"

#define MAX_VPN		PAGES_PER_BANK

void ftl_test(void)
{
	UINT32 res;
	UINT32 lpn, vpn, is_dirty;
	UINT32 i;

	res = cmt_is_full();
	BUG_ON("should be empty, not full", res);

	res = cmt_get(0, &vpn);
	BUG_ON("get for non-existing element should have failed", res == 0);

	// add
	for(i = 0; i < CMT_ENTRIES; i++) {
		res = cmt_is_full();
		BUG_ON("actually cmt is not full", res != 0);
		res = cmt_add(i, 3 * i);
		BUG_ON("adding failed", res != 0);
	}

	// full
	res = cmt_is_full();
	BUG_ON("cmt is now full!", res == 0);

	res = cmt_add(CMT_ENTRIES + 1, 100);
	BUG_ON("adding to full cmt should have failed", res == 0);

	// check
	for(i = 0; i < CMT_ENTRIES; i++) {
		res = cmt_get(i, &vpn);
		BUG_ON("getting failed", res != 0);
		BUG_ON("val incorrect", vpn != 3 * i);
	}

	// update odd lpn
	for(i = 1; i < CMT_ENTRIES; i+=2) {
		res = cmt_update(i, 4 * i);
		BUG_ON("updating failed", res != 0);
	}

	// still full
	res = cmt_is_full();
	BUG_ON("cmt is now full!", res == 0);

	// fix entry 0 and 1
	cmt_fix(0);
	cmt_fix(1);

	// first evict 2, 4, 6, ...
	for(i = 0; i < CMT_ENTRIES / 2 - 1; i++) {
		res = cmt_evict(&lpn, &vpn, &is_dirty);
		BUG_ON("any evciting error", res != 0);
		BUG_ON("lpn wrong", lpn != 2*(i + 1));
		BUG_ON("vpn wrong", vpn != 3 * lpn);
		BUG_ON("should not be dirty", is_dirty);
	}

	// then  evict 3, 5, 7, ...
	for(i = 0; i < CMT_ENTRIES / 2 - 1; i++) {
		res = cmt_evict(&lpn, &vpn, &is_dirty);
		BUG_ON("any evciting error", res != 0);
		BUG_ON("lpn wrong", lpn != 3 + 2 * i);
		BUG_ON("vpn wrong", vpn != 4 * lpn);
		BUG_ON("should be dirty", !is_dirty);
	}

	res = cmt_evict(&lpn, &vpn, &is_dirty);
	BUG_ON("there should be no evictable entries", res == 0);

	cmt_unfix(0);
	cmt_unfix(1);

	res = cmt_evict(&lpn, &vpn, &is_dirty);
	res += cmt_evict(&lpn, &vpn, &is_dirty);
	BUG_ON("failed to evict the last two unfixed entries", res != 0);

	uart_print("CMT passed the unit test ^_^");
}

#endif
