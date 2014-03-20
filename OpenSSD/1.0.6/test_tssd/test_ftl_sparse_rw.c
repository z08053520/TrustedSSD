#include "jasmine.h"
#if OPTION_FTL_TEST
#include "dram.h"
#include "ftl.h"
#include "pmt_thread.h"
#include "scheduler.h"
#include "misc.h"
#include "test_util.h"
#include <stdlib.h>

/* #define DEBUG_FTL */
#ifdef DEBUG_FTL
	#define debug(format, ...)	uart_print(format, ##__VA_ARGS__)
#else
	#define debug(format, ...)
#endif

extern BOOL8 eventq_put(UINT32 const lba, UINT32 const num_sectors,
			UINT32 const cmd_type);
extern BOOL8 ftl_all_sata_cmd_accepted();

/* DRAM buffer to remember every requests first issued and then verified */
#define REQ_LBA_BUF_ADDR	TEMP_BUF_ADDR
#define REQ_SIZE_BUF_ADDR	HIL_BUF_ADDR
SETUP_BUF(req_lba, 		REQ_LBA_BUF_ADDR, 	SECTORS_PER_PAGE);
SETUP_BUF(req_size,		REQ_SIZE_BUF_ADDR, 	SECTORS_PER_PAGE);
#define MAX_NUM_REQS		(BYTES_PER_PAGE / sizeof(UINT32))

/* FTL read thread evokes this function to verify the result in SATA read buf */
void ftl_verify(UINT32 const lpn, UINT8 const sect_offset,
		UINT8 const num_sectors, UINT32 const sata_rd_buf)
{
	debug("verify lpn = %u, sect_offset = %u, num_sectors = %u",
		lpn, sect_offset, num_sectors);

	UINT32 curr_lba = lpn * SECTORS_PER_PAGE + sect_offset;
	UINT8 sect_i = sect_offset, sect_end = sect_offset + num_sectors;
	while (sect_i < sect_end) {
		UINT32 expected_sect_val = curr_lba;
		BUG_ON("Verification failed",
			is_buff_wrong(sata_rd_buf, expected_sect_val,
					sect_i, 1));

		curr_lba++;
		sect_i++;
	}
}

typedef struct {
	UINT32	min_lba, max_lba;
	UINT32	min_req_size, max_req_size;
	UINT32	max_num_reqs, max_wr_bytes;
} rw_case_t;

#define declare_rw_case(name)			\
	rw_case_t name = {			\
		.min_lba = 0,			\
		.max_lba = MAX_LBA,		\
		.min_req_size = 1,		\
		.max_req_size = 64,		\
		.max_num_reqs = MAX_NUM_REQS,	\
		.max_wr_bytes = 512 * MB	\
	}

void finish_all()
{
	BOOL8 idle;
	do {
		idle = ftl_main();
	} while(!idle);
}

void do_flash_read(UINT32 const lba, UINT8 const req_sectors)
{
	debug("do_flash_read: lba = %u, req_sectors = %u",
			lba, req_sectors);

	/* Put SATA cmds into queue */
	while(eventq_put(lba, req_sectors, READ))
		ftl_main();
	/* Make sure it is accepted and proccessed */
	while (!ftl_all_sata_cmd_accepted())
		ftl_main();
}

static UINT32 num_ftl_write_tasks_submitted = 0;

void do_flash_write(UINT32 const lba, UINT8 const req_sectors)
{
	debug("do_flash_write: lba = %u, req_sectors = %u",
			lba, req_sectors);

	/* Prepare SATA write buffer */
	UINT32 lpn          = lba / SECTORS_PER_PAGE;
	UINT32 sect_offset  = lba % SECTORS_PER_PAGE;
	UINT32 num_sectors;
	UINT32 remain_sects = req_sectors;
	UINT32 sata_buf_id  = num_ftl_write_tasks_submitted % NUM_SATA_WR_BUFFERS;
	UINT32 sata_buf     = SATA_WR_BUF_PTR(sata_buf_id);
	/* iterate pages in the request */
	while (remain_sects) {
		if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
		    num_sectors = remain_sects;
		else
		    num_sectors = SECTORS_PER_PAGE - sect_offset;

		debug("write lpn = %u, sect_offset = %u, num_sectors = %u",
			lpn, sect_offset, num_sectors);

		/* set values in SATA write buffer */
		UINT32 curr_lba = lpn * SECTORS_PER_PAGE + sect_offset;
		UINT32 sect_i = sect_offset,
		       sect_end = sect_offset + num_sectors;
		while (sect_i < sect_end) {
			UINT32 sect_val = curr_lba;
			mem_set_dram(sata_buf + BYTES_PER_SECTOR * sect_i,
					sect_val, BYTES_PER_SECTOR);

			sect_i++;
			curr_lba++;
		}

		lpn++;
		sect_offset   = 0;
		remain_sects -= num_sectors;
		sata_buf_id   = (sata_buf_id + 1) % NUM_SATA_WR_BUFFERS;
		sata_buf      = SATA_WR_BUF_PTR(sata_buf_id);
		num_ftl_write_tasks_submitted++;
	}

	/* Put SATA write cmd */
	while(eventq_put(lba, req_sectors, WRITE))
		ftl_main();
	/* Make sure it is accepted and proccessed */
	while (!ftl_all_sata_cmd_accepted())
		ftl_main();
}

#define SPARSE_INTERVAL		(PMT_ENTRIES_PER_SUB_PAGE * SECTORS_PER_PAGE)
#define NUM_INTERVALS		(MAX_LBA / SPARSE_INTERVAL)

void do_sparse_rw_test(rw_case_t *rw_case)
{
	UINT32  total_sectors = 0;
	timer_reset();

	rw_case->max_lba = MAX_LBA;
	rw_case->max_num_reqs = MIN(rw_case->max_num_reqs, MAX_NUM_REQS);

	/* first write sparsely */
	UINT32 lba, req_size;
	UINT32 num_reqs = 0, wr_bytes = 0;
	while (wr_bytes < rw_case->max_wr_bytes &&
		num_reqs < rw_case->max_num_reqs) {
		/* do flash write */
		lba = (rand() % NUM_INTERVALS) * SPARSE_INTERVAL +
			(rand() % SPARSE_INTERVAL);
		req_size = random(rw_case->min_req_size,
				rw_case->max_req_size);
		ASSERT(lba <= MAX_LBA);
		if (lba + req_size > MAX_LBA + 1)
			req_size = MAX_LBA + 1 - lba;

		do_flash_write(lba, req_size);

		/* remember write requests to verify later */
		set_req_lba(num_reqs, lba);
		set_req_size(num_reqs, req_size);

		/* for next */
		num_reqs++;
		wr_bytes += req_size * BYTES_PER_SECTOR;

		/* update statisitcs */
		total_sectors += req_size;
	}
	finish_all();

	/* then read sparsely */
	for (UINT32 req_i = 0; req_i < num_reqs; req_i++) {
		lba = get_req_lba(req_i);
		req_size = get_req_size(req_i);
		do_flash_read(lba, req_size);

		total_sectors += req_size;
	}
	finish_all();

	UINT32	seconds = timer_ellapsed_us() / 1000 / 1000;
	UINT32	mb = total_sectors / 2048;
	uart_print("Done.");
	uart_print("Summary: %u seconds used; %u MB data written and read.",
			seconds, mb);
}

#define MAX_UINT32	0xFFFFFFFF
#define KB		1024
#define MB		(KB * KB)
#define GB		(MB * KB)
#define RAND_SEED	123456

void ftl_test()
{
	uart_print("Start FTL sparse r/w test");

	srand(RAND_SEED);
	init_req_lba_buf(0);
	init_req_size_buf(0);

	declare_rw_case(rw_case);
	/* rw_case.max_num_reqs = 1024; */
	/* rw_case.min_req_size = 64; */
	/* rw_case.max_req_size = 64; */
	/* rw_case.max_req_size = 128; */

	do_sparse_rw_test(&rw_case);

	uart_print("FTL passed unit test ^_^");
}

#endif
