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

extern UINT32	g_num_ftl_write_tasks_submitted;
extern UINT32	g_num_ftl_read_tasks_submitted;

extern BOOL8 	eventq_put(UINT32 const lba, UINT32 const num_sectors,
			   UINT32 const cmd_type);

#define DEBUG_FTL
#ifdef DEBUG_FTL
	#define debug(format, ...)	uart_print(format, ##__VA_ARGS__)
#else
	#define debug(format, ...)
#endif

/* ===========================================================================
 * DRAM buffers for verifying flash
 * =========================================================================*/

#define REQ_LBA_BUF_ADDR	TEMP_BUF_ADDR
#define REQ_SIZE_BUF_ADDR	HIL_BUF_ADDR
SETUP_BUF(req_lba, 		REQ_LBA_BUF_ADDR, 	SECTORS_PER_PAGE);
SETUP_BUF(req_size,		REQ_SIZE_BUF_ADDR, 	SECTORS_PER_PAGE);
#define MAX_NUM_REQS		(BYTES_PER_PAGE / sizeof(UINT32))
#define MAX_REQ_SIZE		256
UINT32	req_pos;

#define FLA_LBA_BUF_ADDR	COPY_BUF(0)
#define FLA_VAL_BUF_ADDR	COPY_BUF(NUM_COPY_BUFFERS / 2)
SETUP_BUF(fla_lba, 		FLA_LBA_BUF_ADDR,	NUM_COPY_BUFFERS / 2);
SETUP_BUF(fla_val, 		FLA_VAL_BUF_ADDR, 	NUM_COPY_BUFFERS / 2);
#define FLA_BUF_SIZE		(NUM_COPY_BUFFERS / 2 * BYTES_PER_PAGE / sizeof(UINT32))
UINT32	fla_pos;

/* assume the length of request is 256 sectors on average */
#define REQS_VERIFY_THREASHOLD	(FLA_BUF_SIZE / MAX_REQ_SIZE)
UINT32	fla_val_missing_count, fla_val_verified_count;
UINT32	test_total_sectors;

static void setup_test(rw_test_t *test)
{
	uart_printf("Running %u... ", test->name);

	req_pos = fla_pos = 0;
	fla_val_missing_count = fla_val_verified_count = 0;
	test_total_sectors = 0;

	init_req_lba_buf(0);
	init_req_size_buf(0);
	init_fla_lba_buf(0xFFFFFFFF);
	init_fla_val_buf(0xFFFFFFFF);

	timer_reset();
}

static void teardown_test(rw_test_t *test)
{
	UINT32	seconds = timer_ellapsed_us() / 1000 / 1000;
	UINT32	mb = test_total_sectors / 2048;

	uart_print("Done.");
	uart_print("Summary:")
	uart_print("\t%u seconds used; "
			"\t%u MB data written and read; "
			"\t%u sectors verified, %u sectors missing.",
			seconds, mb,
			fla_val_missing_count, fla_val_verified_count);
}

static BOOL8 time_to_verify()
{
	return req_pos >= REQS_VERIFY_THREASHOLD;
}

static void request_push(UINT32 const lba, UINT32 const req_size)
{
	BUG_ON("can't push more requests", req_pos >= MAX_NUM_REQS);

	set_req_lba(req_pos, lba);
	set_req_size(req_pos, req_size);
	req_pos++;
}

static BOOL8 request_pop(UINT32 *lba, UINT32 *req_size)
{
	if (req_pos == 0) return FALSE;

	*lba = get_req_lba(req_pos);
	*req_size = get_req_size(req_pos);
	req_pos--;
	return TRUE;
}

static UINT32 flash_find(UINT32 const lba)
{
	UINT32 pos = mem_search_equ_dram(FLA_LBA_BUF_ADDR,
			sizeof(UINT32), FLA_BUF_SIZE, lba);
	return pos;
}

static void flash_set(UINT32 const lba, UINT32 const val)
{
	UINT32	old_fla_pos = flash_find(lba);
	if (old_fla_pos < FLA_BUF_SIZE)
		set_fla_val(old_fla_pos, val);
	else {
		set_fla_lba(fla_pos, lba);
		set_fla_val(fla_pos, val);
		fla_pos = (fla_pos + 1) % FLA_BUF_SIZE;
	}
}

static BOOL8 flash_get(UINT32 const lba, UINT32 *val)
{
	UINT32	fla_pos	= flash_find(lba);
	if (fla_pos >= FLA_BUF_SIZE) return FALSE;

	*val = get_fla_val(fla_pos);
	return TRUE;
}

static void flash_set_page(UINT32 const lpn, UINT32 const sect_offset,
		UINT32 const num_sectors, UINT32 vals[SECTORS_PER_PAGE])
{
	UINT32	lba = lpn * SECTORS_PER_PAGE + sect_offset;
	UINT32	sect_end = sect_offset + num_sectors;
	for (UINT32 sect_i = sect_offset; sect_i < sect_end; sect_i++, lba++)
		flash_set(lba, vals[sect_i]);
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

static void set_random_vals(UINT32 vals[SECTORS_PER_PAGE],
			    UINT32 sect_offset,
			    UINT32 num_sectors)
{
	while (num_sectors--) vals[sect_offset++] = rand();
}

static void do_flash_write(UINT32 const lba, UINT32 const req_sectors)
{
	ftl_main();

	UINT32 lpn          = lba / SECTORS_PER_PAGE;
	UINT32 sect_offset  = lba % SECTORS_PER_PAGE;
	UINT32 num_sectors;
	UINT32 remain_sects = req_sectors;
	UINT32 sata_buf_id  = g_num_ftl_write_tasks_submitted % NUM_SATA_WR_BUFFERS;
	UINT32 sata_buf     = SATA_WR_BUF_PTR(sata_buf_id);

	/* prepare SATA buffer by iterating pages in the request */
	while (remain_sects) {
		if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
		    num_sectors = remain_sects;
		else
		    num_sectors = SECTORS_PER_PAGE - sect_offset;

		UINT32	sector_vals[SECTORS_PER_PAGE] = {0};
		set_random_vals(sector_vals, sect_offset, num_sectors);
		/* UINT32	base_val = lpn * SECTORS_PER_PAGE; */
		/* set_vals(sector_vals, base_val, */
		/* 	 sect_offset, num_sectors); */
		fill_buffer(sata_buf, sect_offset, num_sectors, sector_vals);

		flash_set_page(lpn, sect_offset, num_sectors, sector_vals);

		lpn++;
		sect_offset   = 0;
		remain_sects -= num_sectors;
		sata_buf_id   = (sata_buf_id + 1) % NUM_SATA_WR_BUFFERS;
		sata_buf      = SATA_WR_BUF_PTR(sata_buf_id);
	}

	// write to flash
	while(eventq_put(lba, req_sectors, WRITE))
		ftl_main();
	accept_all();
	/* finish_all(); */
}

static void do_flash_verify(UINT32 lba, UINT32 const req_sectors)
{
	// read from flash
	while(eventq_put(lba, req_sectors, READ)) ftl_main();
	finish_all();

	UINT32 lpn          = lba / SECTORS_PER_PAGE;
	UINT32 sect_offset  = lba % SECTORS_PER_PAGE;
	UINT32 remain_sects = req_sectors;
	UINT32 num_sectors;
	UINT32 num_bufs_rd  = COUNT_BUCKETS(req_sectors + sect_offset, SECTORS_PER_PAGE);
	UINT32 sata_buf_id  = (g_num_ftl_read_tasks_submitted - num_bufs_rd) % NUM_SATA_RD_BUFFERS;
	UINT32 sata_buf     = SATA_RD_BUF_PTR(sata_buf_id);
	UINT32 val;

	/* verify data by iterating each page in the SATA buffer */
	while (remain_sects) {
		if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
			num_sectors = remain_sects;
		else
			num_sectors = SECTORS_PER_PAGE - sect_offset;

		UINT32	sect_end = sect_offset + num_sectors;
		while (sect_offset < sect_end) {
			if (flash_get(lba, &val)) {
				BUG_ON("data in read buffer is not as expected",
					is_buff_wrong(sata_buf, val,
						      sect_offset, 1));
			}

			sect_offset++;
			lba++;
		}

		lpn++;
		sect_offset   = 0;
		remain_sects -= num_sectors;
		sata_buf_id   = (sata_buf_id + 1) % NUM_SATA_RD_BUFFERS;
		sata_buf      = SATA_RD_BUF_PTR(sata_buf_id);
	}
}

/* ===========================================================================
 *  Random and Sequential R/W Tests
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
	NUM_RW_TEST_TYPES
} rw_test_type_t;

typedef struct {
	char*			name;
	rw_test_type_t		type;
	rw_test_params_t	params;
} rw_test_t;

typedef void (*rw_test_runner_t)(rw_test_params_t *params);

static void seq_rw_test(rw_test_params_t *params);
static void rnd_rw_test(rw_test_params_t *params);

static rw_test_runner_t rw_test_runners[NUM_RW_TEST_TYPES] = {
	seq_rw_test_runner,
	rnd_rw_test_runner
};

static void rnd_rw_test(rw_test_params_t *params)
{
	UINT32	wr_bytes = 0,
		num_reqs = 0;
	UINT32	lba, req_size;
	request_clear();
	while (wr_bytes < params->max_wr_bytes &&
	       num_reqs < params->max_num_reqs) {
		lba = random(params->min_lba, params->max_lba);
		req_size = random(params->min_req_size, params->max_req_size);
		if (lba + req_size > params->max_lba)
			req_size = params->max_lba - lba + 1;
		do_flash_write(lba, req_size);

		request_push(lba, req_size);
		if (time_to_verify()) {
			while (request_pop(&lba, &req_size))
				do_flash_verify(lba, req_size);
		}

		num_reqs++;
		wr_bytes += req_size * BYTES_PER_SECTOR;
		test_total_sectors += req_size;
	}
	/* check remaining requests that are not verified yet */
	finish_all();
	while (request_pop(&lba, &req_size))
		do_flash_verify(lba, req_size);
}

static void seq_rw_test(rw_test_params_t *params)
{
	UINT32	lba = params->min_lba,
		wr_bytes = 0,
		num_reqs = 0;
	UINT32	req_size;
	request_clear();
	while (lba < max_lba &&
	       wr_bytes < params->max_wr_bytes &&
	       num_reqs < params->max_num_reqs) {
		req_size = random(params->min_req_size, params->max_req_size);
		do_flash_write(lba, req_size);
		request_push(lba, req_size);

		if (time_to_verify()) {
			while (request_pop(&lba, &req_size))
				do_flash_verify(lba, req_size);
		}

		num_reqs++;
		lba += req_size;
		max_wr_bytes += req_size * BYTES_PER_SECTOR;
		test_total_sectors += req_size;
	}
	/* check remaining requests that are not verified yet */
	finish_all();
	while (request_pop(&lba, &req_size))
		do_flash_verify(lba, req_size);
}

/* ===========================================================================
 * Entry Point
 * =========================================================================*/

#define	MAX_UINT32	0xFFFFFFFF
#define KB		1024
#define MB		(KB * KB)
#define RAND_SEED	1234567

void ftl_test()
{
	uart_print("Start testing FTL unit test");

	init_dram();
	srand(RAND_SEED);

	/* Prepare sequential rw tests */
	rw_test_t seq_rw_test = {
		.name = "sequential r/w test",
		.type = SEQ_RW_TEST,
		.params = {
			.min_lba = 0,
			.max_lba = MAX_UINT32,
			.min_req_size = 1,
			.max_req_size = 256,
			/* .max_num_reqs = MAX_UINT32, */
			.max_num_reqs = 8,
			.max_wr_bytes = 256 * MB
		}
	};

	/* Prepare random rw tests */
	rw_test_t rnd_rw_test_dense = {
		.name = "dense random r/w test",
		.type = RND_RW_TEST,
		.params = {
			.min_lba = 0,
			.max_lba = 2048,	/* first 1MB */
			.min_req_size = 1,
			.max_req_size = 256,
			/* .max_num_reqs = MAX_UINT32, */
			.max_num_reqs = 8,
			.max_wr_bytes = 256 * MB
		}
	};

	rw_test_t rnd_rw_test_sparse = {
		.name = "sparse random r/w test",
		.type = RND_RW_TEST,
		.params = {
			.min_lba = 0,
			.max_lba = MAX_LBA,
			.min_req_size = 1,
			.max_req_size = 256,
			/* .max_num_reqs = MAX_UINT32, */
			.max_num_reqs = 8,
			.max_wr_bytes = 256 * MB
		}
	};

	/* Run all tests */
	rw_test_t* rw_tests[]	= {
		&seq_rw_test,

		&rnd_rw_test_dense,
		&rnd_rw_test_sparse
	};

	UINT32	num_rw_tests = sizeof(rw_tests) / sizeof(rw_tests[0]);
	for (UINT32 test_i = 0; test_i < num_rw_tests; test_i++) {
		rw_test_t *test = rw_tests[test_i];

		setup(test);
		(*rw_test_runners[test->type])(test->params);
		teardown(test)
	}

	uart_print("FTL passed unit test ^_^");
}

#endif
