/* ===========================================================================
 * Unit test for Garbage Collector (GC)
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "gc.h"
#include "bad_blocks.h"

void ftl_test(void)
{
	UINT32 num_blocks_to_test = 1024;
	UINT32 i, last_blk = 0, this_blk; 
	UINT32 vpn;
	UINT8 bank = 0;

	INFO("test", "start testing garbage collector");

	FOR_EACH_BANK(bank) {
		uart_printf("start testing gc for bank %d...", bank);
		for (i = 0; i < num_blocks_to_test; i++) {
			vpn = gc_allocate_new_vpn(bank);
			this_blk = vpn / PAGES_PER_VBLK;
			
			BUG_ON("bad block", bb_is_bad(bank, this_blk));
			BUG_ON("not starting from page 0 inside a new block", 
					this_blk != last_blk && 
					vpn % PAGES_PER_VBLK != 0);
			last_blk = this_blk;
		}
		uart_print("done");
	}

	uart_print("gc passed unit test ^_^");
}

#endif
