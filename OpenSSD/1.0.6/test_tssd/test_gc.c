/* ===========================================================================
 * Unit test for Garbage Collector (GC)
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "dram.h"
#include "gc.h"
#include "bad_blocks.h"
#include "test_util.h"
#include <stdlib.h>

#define RAND_SEED	123456

#define USAGE_BUF_ADDR	TEMP_BUF_ADDR

SETUP_BUF(usage,	USAGE_BUF_ADDR,		SECTORS_PER_PAGE);

#define MAX_VBLK	(BYTES_PER_PAGE / sizeof(UINT32))

void ftl_test(void)
{
	INFO("test", "start testing garbage collector");

	UINT32 	num_blocks_to_test = 1024;
	BUG_ON("too many blocks to test", 
		num_blocks_to_test > VBLKS_PER_BANK || num_blocks_to_test > MAX_VBLK);
	UINT8	bank = 0;
	FOR_EACH_BANK(bank) {
		uart_printf("start testing gc for bank %d...", bank);

		init_usage_buf(0xFFFFFFFF);
		UINT32	last_vpn[2] = {0, 0};
		UINT32 	vblk = 0;
		while (vblk < num_blocks_to_test) {
			BOOL8	is_sys 	= rand() % 2;
			UINT32	vpn	= gc_allocate_new_vpn(bank, is_sys);
		
			if (vpn % PAGES_PER_VBLK == 0) {
				vblk = vpn / PAGES_PER_VBLK;
				if (vblk >= num_blocks_to_test) break;

				if (last_vpn[is_sys]) {
					BUG_ON("not use all pages in last block",
						(last_vpn[is_sys] + 1) % PAGES_PER_VBLK != 0);
				}
				BUG_ON("bad block", bb_is_bad(bank, vblk));
				BUG_ON("this block has been used",
					get_usage(vblk) != 0xFFFFFFFF);

				set_usage(vblk, is_sys);
				vblk++;
			}
			else {
				BUG_ON("not use all pages in last block",
					last_vpn[is_sys] + 1 != vpn);
			}
			last_vpn[is_sys] = vpn;
		}
		uart_print("done");
	}

	uart_print("gc passed unit test ^_^");
}

#endif
