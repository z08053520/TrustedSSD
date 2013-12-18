/* ===========================================================================
 * Unit test for Sector Ownership Table 
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST

#if OPTION_ACL == FALSE
	#error ACL is not enabled
#endif

#include "sot.h"
#include "test_util.h"
#include <stdlib.h>

#define MAX_LBA		NUM_LSECTORS

#define LBA_BUF_ADDR	TEMP_BUF_ADDR
#define LBA_BUF_SIZE	SECTORS_PER_PAGE
#define UID_BUF_ADDR	HIL_BUF_ADDR
#define UID_BUF_SIZE	SECTORS_PER_PAGE

SETUP_BUF(lba, LBA_BUF_ADDR, LBA_BUF_SIZE);
SETUP_BUF(uid, UID_BUF_ADDR, UID_BUF_SIZE);

#define MAX_NUM_UIDS	(BYTES_PER_PAGE / sizeof(uid_t))
#define MAX_NUM_LBAS	(BYTES_PER_PAGE / sizeof(UINT32))
#if	MAX_NUM_UIDS <= MAX_NUM_LBAS
	#define SAMPLE_SIZE	MAX_NUM_UIDS
#else
	#define SAMPLE_SIZE	MAX_NUM_LBAS
#endif

#define RAND_SEED	123456

void ftl_test()
{
	uart_print("Running unit test for SOT...");
	
	UINT32 lba;
	uid_t  uid;
	UINT32 i;
	UINT32 j, repeats = 8;

	srand(RAND_SEED);

	init_lba_buf();
	init_uid_buf();

	uart_print("Randomly check SOT initial state");
	for (i = 0; i < SAMPLE_SIZE; i++) {
		lba = random(0, MAX_LBA);
		sot_get(lba, &uid);
		BUG_ON("SOT entries is not initialized as zeros", uid != 0);
	}

	for (j = 0; j < repeats; j++) {
		uart_print("Randomly update SOT entries");
		for (i = 0; i < SAMPLE_SIZE; i++) {
			lba = random(0, MAX_LBA);
			uid = rand();
			sot_udpate(lba, uid);

			set_lba(i, lba);
			set_uid(i, uid);
		}

		uart_print("Verify the udpate SOT entries");
		for (i = 0; i < SAMPLE_SIZE; i++) {
			lba = get_lba(i);
			sot_get(lba, &uid);
			BUG_ON("SOT entry is not as expected", uid != get_uid(i));
		}
	}
	
	uart_print("Unit test for SOT passed ^_^");
}

#endif
