/* ===========================================================================
 * Unit test for Sector Ownership Table
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST

#if OPTION_ACL == FALSE
	#error ACL is not enabled
#endif

#include "sot.h"
#include "ftl.h"
#include "test_util.h"
#include <stdlib.h>

#define LBA_BUF_ADDR	TEMP_BUF_ADDR
#define LBA_BUF_SIZE	SECTORS_PER_PAGE
#define UID_BUF_ADDR	HIL_BUF_ADDR
#define UID_BUF_SIZE	SECTORS_PER_PAGE

SETUP_BUF(lba, LBA_BUF_ADDR, LBA_BUF_SIZE);
SETUP_BUF(uid, UID_BUF_ADDR, UID_BUF_SIZE);

#define SAMPLE_SIZE 	(BYTES_PER_PAGE / sizeof(UINT32))

#define RAND_SEED	123456

#define MAX_UID		0xFFFF

void ftl_test()
{
	uart_print("Running unit test for SOT...");

	UINT32 lba;
	user_id_t  uid;
	UINT32 i;
	UINT32 j, repeats = 4;

	srand(RAND_SEED);

	init_lba_buf(0);
	init_uid_buf(0);

	uart_print("Randomly check SOT initial state");
	for (i = 0; i < SAMPLE_SIZE; i++) {
		lba = random(0, MAX_LBA);
		sot_fetch(lba, &uid);
		BUG_ON("SOT entries is not initialized as zeros", uid != 0);
	}

	for (j = 0; j < repeats; j++) {
		uart_print("Randomly update SOT entries");
		for (i = 0; i < SAMPLE_SIZE; i++) {
			lba = random(0, MAX_LBA);
			uid = random(0, MAX_UID);
			sot_update(lba, uid);

			set_lba(i, lba);
			set_uid(i, uid);
		}

		uart_print("Verify the update SOT entries");
		for (i = 0; i < SAMPLE_SIZE; i++) {
			lba = get_lba(i);
			sot_fetch(lba, &uid);
//			uart_printf("actual uid = %u, expected uid = %u", uid, get_uid(i));
			BUG_ON("SOT entry is not as expected", uid != get_uid(i));
		}
	}

	uart_print("Unit test for SOT passed ^_^");
}

#endif
