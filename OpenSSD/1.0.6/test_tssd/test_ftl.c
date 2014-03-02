/* ===========================================================================
 * Unit test for FTL
 * =========================================================================*/

#include "jasmine.h"
#if OPTION_FTL_TEST
#include "dram.h"
#include "ftl.h"
#include "misc.h"
#include "test_util.h"
#include "task_engine.h"
#include "ftl_read_task.h"
#include "ftl_write_task.h"
#include <stdlib.h>

UINT32	num_ftl_write_tasks_submitted = 0;
UINT32	num_ftl_read_tasks_submitted = 0;

extern BOOL8 	eventq_put(UINT32 const lba, UINT32 const num_sectors,
#if OPTION_ACL
				UINT32 const session_key,
#endif
				UINT32 const cmd_type);

/* #define DEBUG_FTL */
#ifdef DEBUG_FTL
	#define debug(format, ...)	uart_print(format, ##__VA_ARGS__)
#else
	#define debug(format, ...)
#endif

/* ===========================================================================
 * Test Type
 * =========================================================================*/

typedef struct {
	UINT32	min_lba, max_lba;
	UINT32	min_req_size, max_req_size; /* in sectors */
	UINT32	max_num_reqs;
	UINT32	max_wr_bytes;
} rw_test_params_t;

typedef enum {
	SEQ_RW_TEST,
	RND_RW_TEST,
	SPARSE_RW_TEST,
	NUM_RW_TEST_TYPES
} rw_test_type_t;

typedef struct {
	char*			name;
	rw_test_type_t		type;
	rw_test_params_t	params;
} rw_test_t;

typedef void (*rw_test_runner_t)(rw_test_params_t *params);

static void seq_rw_test_runner(rw_test_params_t *params);
static void rnd_rw_test_runner(rw_test_params_t *params);
static void sparse_rw_test_runner(rw_test_params_t *params);

static rw_test_runner_t rw_test_runners[NUM_RW_TEST_TYPES] = {
	seq_rw_test_runner,
	rnd_rw_test_runner,
	sparse_rw_test_runner
};

/* ===========================================================================
 * DRAM buffers for verifying flash
 * =========================================================================*/

#define REQ_LBA_BUF_ADDR	TEMP_BUF_ADDR
#define REQ_SIZE_BUF_ADDR	HIL_BUF_ADDR
SETUP_BUF(req_lba, 		REQ_LBA_BUF_ADDR, 	SECTORS_PER_PAGE);
SETUP_BUF(req_size,		REQ_SIZE_BUF_ADDR, 	SECTORS_PER_PAGE);
#define MAX_NUM_REQS		(BYTES_PER_PAGE / sizeof(UINT32))
UINT32	req_buf_size;

#define MAX_REQ_SIZE		256

#define VAL_BUF_ADDR		_COPY_BUF(0)
#define VAL_BUF_BYTES		(NUM_COPY_BUFFERS / 2 * BYTES_PER_PAGE)
#define VAL_BUF_SECTORS		(NUM_COPY_BUFFERS / 2 * SECTORS_PER_PAGE)
#define MAX_NUM_VALS		(VAL_BUF_BYTES / sizeof(UINT32))
SETUP_BUF(val,			VAL_BUF_ADDR,	VAL_BUF_SECTORS);
UINT32	val_buf_size;

#if OPTION_ACL
#define SKEY_BUF_ADDR		_COPY_BUF(NUM_COPY_BUFFERS / 2)
SETUP_BUF(skey,			SKEY_BUF_ADDR,	VAL_BUF_SECTORS);
#endif

UINT32	test_total_sectors;

static void setup(rw_test_t *test)
{
	uart_printf("Running %s... ", test->name);

	test_total_sectors = 0;

	req_buf_size = 0;
	init_req_lba_buf(0);
	init_req_size_buf(0);

	val_buf_size = 0;
	init_val_buf(0xFFFFFFFF);
#if OPTION_ACL
	init_skey_buf(0);
#endif

	timer_reset();
}

static void teardown(rw_test_t *test)
{
	UINT32	seconds = timer_ellapsed_us() / 1000 / 1000;
	UINT32	mb = test_total_sectors / 2048;

	uart_print("Done.");
	uart_print("Summary: %u seconds used; %u MB data written and read.",
			seconds, mb);
}

static BOOL8 time_to_verify()
{
	return req_buf_size >= MAX_NUM_REQS || val_buf_size >= MAX_NUM_VALS;
}

static void request_push(UINT32 const lba, UINT32 const req_size)
{
	BUG_ON("can't push more requests",
		req_buf_size >= MAX_NUM_REQS);

	set_req_lba(req_buf_size, lba);
	set_req_size(req_buf_size, req_size);
	req_buf_size++;
}

static BOOL8 request_pop(UINT32 *lba, UINT32 *req_size)
{
	if (req_buf_size == 0) {
		val_buf_size = 0;
		return FALSE;
	}

	req_buf_size--;
	*lba = get_req_lba(req_buf_size);
	*req_size = get_req_size(req_buf_size);
	return TRUE;
}

/* ===========================================================================
 *  Fake SATA R/W requests
 * =========================================================================*/

extern BOOL8 ftl_all_sata_cmd_accepted();

static void accept_all()
{
	do {
		ftl_main();
	} while(!ftl_all_sata_cmd_accepted());
}

static void finish_all()
{
	BOOL8 idle;
	do {
		idle = ftl_main();
	} while(!idle);
}

#define VAL_PER_REQ		1
#define VAL_PER_LBA		0

static void do_flash_write(UINT32 const lba, UINT32 const req_sectors,
				BOOL8 const val_per_req)
{
	ftl_main();

	UINT32 lpn          = lba / SECTORS_PER_PAGE;
	UINT32 sect_offset  = lba % SECTORS_PER_PAGE;
	UINT32 num_sectors;
	UINT32 remain_sects = req_sectors;
	UINT32 sata_buf_id  = num_ftl_write_tasks_submitted % NUM_SATA_WR_BUFFERS;
	UINT32 sata_buf     = SATA_WR_BUF_PTR(sata_buf_id);

#if OPTION_ACL
	UINT32 session_key  = rand();
#endif

	UINT32 val = 0;;
	if (val_per_req) {
		val = rand();
		set_val(val_buf_size, val);
#if OPTION_ACL
		set_skey(val_buf_size, session_key);
#endif
		val_buf_size++;
	}

	/* prepare SATA buffer by iterating pages in the request */
	while (remain_sects) {
		if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
		    num_sectors = remain_sects;
		else
		    num_sectors = SECTORS_PER_PAGE - sect_offset;

		debug("write lpn = %u, offset = %u, num_sectors = %u",
			lpn, sect_offset, num_sectors);

		UINT32	sector_vals[SECTORS_PER_PAGE] = {0};
		if (val_per_req) {
			clear_vals(sector_vals, val);
		}
		else {
			UINT32 sect_i = sect_offset,
			       sect_end = sect_offset + num_sectors,
			       tmp_lba = lpn * SECTORS_PER_PAGE + sect_i;
			while (sect_i < sect_end) {
				val = rand();
				sector_vals[sect_i] = val;
				set_val(tmp_lba, val);
#if OPTION_ACL
				set_skey(tmp_lba, session_key);
#endif
				sect_i++;
				tmp_lba++;
			}
		}
		fill_buffer(sata_buf, sect_offset, num_sectors, sector_vals);

		lpn++;
		sect_offset   = 0;
		remain_sects -= num_sectors;
		sata_buf_id   = (sata_buf_id + 1) % NUM_SATA_WR_BUFFERS;
		sata_buf      = SATA_WR_BUF_PTR(sata_buf_id);
		num_ftl_write_tasks_submitted++;
	}

	// write to flash
#if OPTION_ACL
	/* if (req_buf_size % 500 == 0) { */
	/* 	uart_print("%u) write lba = %u, req_sectors = %u, skey = %u", */
	/* 		req_buf_size, lba, req_sectors, session_key); */
	/* } */
	while(eventq_put(lba, req_sectors, session_key, WRITE))
#else
	while(eventq_put(lba, req_sectors, WRITE))
#endif
		ftl_main();
	/* accept_all(); */
	/* finish_all(); */
}

static void do_flash_read(UINT32 lba, UINT32 const req_sectors
#if OPTION_ACL
				,UINT32 const session_key
#endif
		)
{
	// read from flash
#if OPTION_ACL
	while(eventq_put(lba, req_sectors, session_key, READ))
#else
	while(eventq_put(lba, req_sectors, READ))
#endif
		ftl_main();
}

static void _do_flash_verify(UINT32 lba, UINT32 const req_sectors,
#if OPTION_ACL
				UINT32 const session_key,
#endif
				BOOL8 const val_per_req)
{
	debug("verify lba = %u, req_sectors = %u",
		lba, req_sectors);
#if OPTION_ACL
	debug("with session_key = %u", session_key);
#endif

	// read from flash
#if OPTION_ACL
	while(eventq_put(lba, req_sectors, session_key, READ))
#else
	while(eventq_put(lba, req_sectors, READ))
#endif
		ftl_main();
	finish_all();

	UINT32 lpn          = lba / SECTORS_PER_PAGE;
	UINT32 sect_offset  = lba % SECTORS_PER_PAGE;
	UINT32 remain_sects = req_sectors;
	UINT32 num_sectors;
	UINT32 sata_buf_id  = num_ftl_read_tasks_submitted % NUM_SATA_RD_BUFFERS;
	UINT32 sata_buf     = SATA_RD_BUF_PTR(sata_buf_id);
	UINT32 val = 0;
	BOOL8 wrong;

	if (val_per_req) {
		/* Invariant: req_buf_size is always the id of the
		 * current request to be verified */
		val = get_val(req_buf_size);
	}

	/* verify data by iterating each page in the SATA buffer */
	while (remain_sects) {
		if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
			num_sectors = remain_sects;
		else
			num_sectors = SECTORS_PER_PAGE - sect_offset;

		if (val_per_req) {
			wrong = is_buff_wrong(sata_buf, val,
						sect_offset, num_sectors);
			BUG_ON("data in read buffer is not as expected", wrong);
		}
		else {
			UINT32	sect_end = sect_offset + num_sectors;
			while (sect_offset < sect_end) {
				val = get_val(lba);
				wrong = is_buff_wrong(sata_buf, val,
							sect_offset, 1);
				BUG_ON("data in read buffer is not as expected",
					wrong);

				sect_offset++;
				lba++;
			}
		}

		lpn++;
		sect_offset   = 0;
		remain_sects -= num_sectors;
		sata_buf_id   = (sata_buf_id + 1) % NUM_SATA_RD_BUFFERS;
		sata_buf      = SATA_RD_BUF_PTR(sata_buf_id);
		num_ftl_read_tasks_submitted++;
	}
}

#if OPTION_ACL
static void do_flash_verify(UINT32 lba, UINT32 const req_sectors,
				BOOL8 const val_per_req)
{
	UINT32 skey = 0;
	if (val_per_req) {
		/* Invariant: req_buf_size is always the current
		 * request to be verified */
		skey = get_skey(req_buf_size);
		_do_flash_verify(lba, req_sectors, skey, VAL_PER_REQ);
		return;
	}

	/* For val per LBA */
	UINT32 tmp_skey;
	UINT32 num_sectors = 0;
	UINT32 lba_begin = lba, lba_end = lba + req_sectors;
	while (lba < lba_end) {
		tmp_skey = get_skey(lba);
		debug("skey = %u for lba = %u", tmp_skey, lba);
		if (tmp_skey != skey) {
			if (num_sectors) {
				_do_flash_verify(lba_begin, num_sectors,
							skey, VAL_PER_LBA);
				num_sectors = 0;
				lba_begin = lba;
			}
			skey = tmp_skey;
		}
		lba++;
		num_sectors++;
	}
	_do_flash_verify(lba_begin, num_sectors, skey, VAL_PER_LBA);
}
#else
#define do_flash_verify(lba, req_sectors, val_per_req)		\
		_do_flash_verify(lba, req_sectors, val_per_req)
#endif

/* ===========================================================================
 *  Random and Sequential R/W Tests
 * =========================================================================*/

static void seq_rw_test_runner(rw_test_params_t *params)
{
	UINT32	lba = params->min_lba,
		wr_bytes = 0,
		num_reqs = 0;
	UINT32	req_size;
	while (lba < params->max_lba &&
	       wr_bytes < params->max_wr_bytes &&
	       num_reqs < params->max_num_reqs) {
		req_size = random(params->min_req_size, params->max_req_size);
		/* req_size = 1; */

		do_flash_write(lba, req_size, VAL_PER_REQ);
		request_push(lba, req_size);

		if (time_to_verify()) {
			finish_all();
			while (request_pop(&lba, &req_size)) {
				/* if (req_buf_size % 500 == 0) { */
				/* 	uart_print("%u, %u] read lba = %u, req_size = %u", */
				/* 		req_buf_size, num_reqs, lba, req_size); */
				/* } */
				do_flash_verify(lba, req_size, VAL_PER_REQ);
			}
		}

		num_reqs++;
		lba += req_size;
		wr_bytes += req_size * BYTES_PER_SECTOR;
		test_total_sectors += req_size;
	}
	/* check remaining requests that are not verified yet */
	finish_all();
	while (request_pop(&lba, &req_size)) {
		/* if (req_buf_size % 500 == 0) { */
		/* 	uart_print("%u, %u] read lba = %u, req_size = %u", */
		/* 		req_buf_size, num_reqs, lba, req_size); */
		/* } */
		do_flash_verify(lba, req_size, VAL_PER_REQ);
	}
}

#define MAX_LBA_FOR_RND_TEST	(MAX_NUM_VALS - 1)

static void rnd_rw_test_runner(rw_test_params_t *params)
{
	if (params->max_lba > MAX_LBA_FOR_RND_TEST)
		params->max_lba = MAX_LBA_FOR_RND_TEST;

	UINT32	wr_bytes = 0,
	        num_reqs = 0;
	UINT32	lba, req_size;
	while (wr_bytes < params->max_wr_bytes &&
	       num_reqs < params->max_num_reqs) {
		lba = random(params->min_lba, params->max_lba);
		req_size = random(params->min_req_size, params->max_req_size);
		if (lba + req_size - 1> params->max_lba)
			req_size = params->max_lba  - lba + 1;

		do_flash_write(lba, req_size, VAL_PER_LBA);
		request_push(lba, req_size);

		if (time_to_verify()) {
			finish_all();
			while (request_pop(&lba, &req_size)) {
				do_flash_verify(lba, req_size, VAL_PER_LBA);
			}
		}

		num_reqs++;
		wr_bytes += req_size * BYTES_PER_SECTOR;
		test_total_sectors += req_size;
	}
	/* check remaining requests that are not verified yet */
	finish_all();
	while (request_pop(&lba, &req_size)) {
		do_flash_verify(lba, req_size, VAL_PER_LBA);
	}
}

#define SPARSE_LBA_INTERVAL	(PMT_ENTRIES_PER_SUB_PAGE * SECTORS_PER_SUB_PAGE)
#define MAX_NUM_SPARSE_REQS	(NUM_LSECTORS / SPARSE_LBA_INTERVAL)

static void sparse_rw_test_runner(rw_test_params_t *params)
{
	if (params->max_num_reqs > MAX_NUM_SPARSE_REQS)
		params->max_num_reqs = MAX_NUM_SPARSE_REQS;

	// DEBUG
	show_debug_msg = FALSE;

	UINT32	lba, base_lba = 0,
		wr_bytes = 0;
	UINT32	req_size;
	UINT32	num_reqs = 0;

	while (wr_bytes < params->max_wr_bytes &&
	       num_reqs < params->max_num_reqs) {
		lba = rand() % SPARSE_LBA_INTERVAL;
		req_size = random(params->min_req_size, params->max_req_size);
		if (lba + req_size > SPARSE_LBA_INTERVAL)
			req_size = SPARSE_LBA_INTERVAL - lba;
		lba += base_lba;

		do_flash_write(lba, req_size, VAL_PER_REQ);
		request_push(lba, req_size);

		if (time_to_verify()) {
			finish_all();
			while (request_pop(&lba, &req_size)) {
				/* if (req_buf_size % 500 == 0) { */
				/* 	uart_print("%u, %u] read lba = %u, req_size = %u", */
				/* 		req_buf_size, num_reqs, lba, req_size); */
				/* } */
				do_flash_verify(lba, req_size, VAL_PER_REQ);
			}
			BUG_ON("request queue is not empty!",
				req_buf_size != 0);
		}

		num_reqs++;
		base_lba += SPARSE_LBA_INTERVAL;
		wr_bytes += req_size * BYTES_PER_SECTOR;
		test_total_sectors += req_size;
	}
	/* check remaining requests that are not verified yet */
	finish_all();
	while (request_pop(&lba, &req_size)) {
		/* if (req_buf_size % 250 == 0) { */
		/* 	uart_print("%u, %u] read lba = %u, req_size = %u", */
		/* 		req_buf_size, num_reqs, lba, req_size); */
		/* } */
		do_flash_verify(lba, req_size, VAL_PER_REQ);
	}
}
/* ===========================================================================
 * Entry Point
 * =========================================================================*/

#define	MAX_UINT32	0xFFFFFFFF
#define KB		1024
#define MB		(KB * KB)
#define GB		(MB * KB)
#define RAND_SEED	1234567

void ftl_test()
{
	uart_print("Start testing FTL unit test");
	uart_print("max num reqs = %u, max num vals = %u",
			MAX_NUM_REQS, MAX_NUM_VALS);

	srand(RAND_SEED);

	/* Prepare sequential rw tests */
	rw_test_t seq_rw_test = {
		.name = "sequential r/w test",
		.type = SEQ_RW_TEST,
		.params = {
			.min_lba = 64 * (GB / 512),
			.max_lba = MAX_UINT32,
			.min_req_size = 1,
			.max_req_size = 256,
			/* .max_req_size = 1, */
			.max_num_reqs = MAX_UINT32,
			/* .max_num_reqs = 9, */
			.max_wr_bytes = 512 * MB
		}
	};

	/* Prepare random rw tests */
	rw_test_t rnd_rw_test= {
		.name = "random r/w test",
		.type = RND_RW_TEST,
		.params = {
			.min_lba = 0,
			.max_lba = 2048,	/* first 1MB */
			.min_req_size = 1,
			.max_req_size = 256,
			/* .max_req_size = 1, */
			.max_num_reqs = MAX_UINT32,
			/* .max_num_reqs = 1, */
			.max_wr_bytes = 512 * MB
		}
	};

	rw_test_t sparse_rw_test = {
		.name = "sparse r/w test",
		.type = SPARSE_RW_TEST,
		.params = {
			.min_lba = 0,
			.max_lba = MAX_LBA,
			.min_req_size = 1,
			.max_req_size = 256,
			/* .max_req_size = 1, */
			.max_num_reqs = MAX_UINT32,
			/* .max_num_reqs = 1024, */
			/* .max_wr_bytes = 512 * MB */
			.max_wr_bytes = 512 * MB
		}
	};

	/* Run all tests */
	rw_test_t* rw_tests[]	= {
		&seq_rw_test,
		&rnd_rw_test,
		&sparse_rw_test
	};

	UINT32	num_rw_tests = sizeof(rw_tests) / sizeof(rw_tests[0]);
	for (UINT32 test_i = 0; test_i < num_rw_tests; test_i++) {
		rw_test_t *test = rw_tests[test_i];

		setup(test);
		(*rw_test_runners[test->type])(&test->params);
		teardown(test);
	}

	uart_print("FTL passed unit test ^_^");
}

#endif
