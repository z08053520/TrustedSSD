/* ===========================================================================
 * Unit test for Sector Ownership Table
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST

#if OPTION_ACL == FALSE
	#error ACL is not enabled
#endif

#include "dram.h"
#include "sot.h"
#include "mem_util.h"
#include "test_util.h"
#include "sata.h"
#include <stdlib.h>

#define RAND_SEED	1234

#define LBA_BUF		TEMP_BUF_ADDR
#define UID_BUF		HIL_BUF_ADDR

#define BUF_SIZE	(BYTES_PER_PAGE / sizeof(user_id_t))
#define SAMPLE_SIZE	BUF_SIZE

SETUP_BUF(lba,		LBA_BUF,	SECTORS_PER_PAGE);
SETUP_BUF(vp,		UID_BUF,	SECTORS_PER_PAGE);

static void load_sot(UINT32 const lpn)
{
	BOOL8		idle = FALSE;
	task_res_t	res  = sot_load(lpn);
	while (res != TASK_CONTINUED) {
		BUG_ON("task engine is idle, but sot page haven't loaded", idle);
		idle = task_engine_run();
		res  = sot_load(lpn);
	}
}

void ftl_test()
{
	UINT32	i, j;
	UINT32	lba, lpn;
	UINT8	offset, num_sectors;
	user_id_t	uid;

	uart_print("Running unit test for SOT... ");
	uart_printf("sample size = %d\r\n", SAMPLE_SIZE);

	srand(RAND_SEED);
 	init_lba_buf(0xFFFFFFFF);
	init_uid_buf(0);

	uart_print("\tcheck default uid for sectors is 0");
	for(i = 0; i < SAMPLE_SIZE; i++) {
		lba = rand() % MAX_LBA;
		lpn = lba / SECTORS_PER_PAGE;
		offset = lba % SECTORS_PER_PAGE;
		num_sectors = random(1, SECTORS_PER_PAGE - offset);

		load_sot(lpn);
		BOOL8 ok = sot_authenticate(lpn, offset, num_sectors, 0);
		BUG_ON("default uid is not ZERO!!!", !ok);
	}

	uart_print("\tupdate uid of sector");
	i = 0;
	while (i < SAMPLE_SIZE) {
		lba = rand() % MAX_LBA;
		lpn = lba / SECTORS_PER_PAGE;
		offset = lba % SECTORS_PER_PAGE;
		num_sectors = random(1, SECTORS_PER_PAGE - offset);
		uid = (user_id_t) rand();

		load_sot(lpn);
		sot_authorize(lpn, offset, num_sectors, uid);

		UINT32 lba_end = lba + num_sectors;
		for (; lba < lba_end; lba++) {
			j 	= mem_search_equ_dram(LBA_BUF, sizeof(UINT32),
						      BUF_SIZE, lba);
			if (j < BUF_SIZE) {
				set_uid(j, uid);
			}
			else {
				if (i >= SAMPLE_SIZE) break;
				set_lba(i, lba);
				set_uid(i, uid);
				i++;
			}
		}
	}

	uart_print("\tverify lba->uid");
	for(i = 0; i < SAMPLE_SIZE; i++) {
		lba = get_lba(i);
		uid = get_uid(i);

		lpn = lba / SECTORS_PER_PAGE;
		offset = lba % SECTORS_PER_PAGE;
		load_sot(lpn);
		BOOL8 ok = sot_authenticate(lpn, offset, 1, uid);
		/*  uart_printf("vp (bank, vpn) = (%u, %u); expected_vp (bank, vpn) = (%u, %u)\r\n",
			    vp.bank, vp.vpn, expected_vp.bank, expected_vp.vpn);*/
		BUG_ON("authentication failuire", !ok);
	}

	uart_print("SOT passed the unit test ^_^");
}
#endif
