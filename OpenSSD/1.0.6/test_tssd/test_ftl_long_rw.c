#include "jasmine.h"
#if OPTION_FTL_TEST
#include "test_ftl_rw_common.h"

/* FTL read thread evokes this function to verify the result in SATA read buf */
static BOOL8 read_res_0xFFFFFFFF = FALSE;
void ftl_verify(UINT32 const lpn, UINT8 const sect_offset,
		UINT8 const num_sectors, UINT32 const sata_rd_buf)
{
	debug("verify lpn = %u, sect_offset = %u, num_sectors = %u",
		lpn, sect_offset, num_sectors);

	UINT32 curr_lba = lpn * SECTORS_PER_PAGE + sect_offset;
	UINT8 sect_i = sect_offset, sect_end = sect_offset + num_sectors;
	while (sect_i < sect_end) {
		UINT32 expected_sect_val = read_res_0xFFFFFFFF ?
						0xFFFFFFFF : curr_lba;
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
	UINT32	read_percent; /* 0 - 100 */
} rw_case_t;

#define declare_rw_case(name)			\
	rw_case_t name = {			\
		.min_lba = 0,			\
		.max_lba = MAX_LBA,		\
		.min_req_size = 1,		\
		.max_req_size = 128,		\
		.max_num_reqs = MAX_UINT32,	\
		.max_wr_bytes = 256 * MB,	\
		.read_percent = 0		\
	}

void do_flash_read(UINT32 const lba, UINT8 const req_sectors)
{
	debug("do_flash_read: lba = %u, req_sectors = %u",
			lba, req_sectors);

	/* Put SATA cmds into queue */
#if OPTION_ACL
	UINT32 session_key = 0;
	while(eventq_put(lba, req_sectors, session_key, READ))
#else
	while(eventq_put(lba, req_sectors, READ))
#endif
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
#if OPTION_ACL
	/* TODO: use more complicated session key pattern */
	UINT32 session_key = 0;
	while(eventq_put(lba, req_sectors, session_key, WRITE))
#else
	while(eventq_put(lba, req_sectors, WRITE))
#endif
		ftl_main();
	/* Make sure it is accepted and proccessed */
	while (!ftl_all_sata_cmd_accepted())
		ftl_main();
}

void do_long_rw_test(rw_case_t *rw_case)
{
	UINT32  total_sectors = 0;
	timer_reset();

	rw_case->read_percent = MIN(rw_case->read_percent, 100);

	/* first write sequentiallly */
	srand(RAND_SEED);
	UINT32 lba = rw_case->min_lba,
	       req_size = (rw_case->min_req_size + rw_case->max_req_size) / 2;
	UINT32 num_reqs = 0, wr_bytes = 0;
	BOOL8 is_read;
	read_res_0xFFFFFFFF = TRUE;
	while (wr_bytes < rw_case->max_wr_bytes &&
		lba < rw_case->max_lba &&
		num_reqs < rw_case->max_num_reqs) {
		/* do flash write */
		is_read = (rand() % 100) < rw_case->read_percent;
		if (is_read)
			do_flash_read(lba, req_size);
		else {
			do_flash_write(lba, req_size);

			num_reqs++;
			wr_bytes += req_size * BYTES_PER_SECTOR;
		}

		/* for next */
		lba += req_size;

		/* update statisitcs */
		total_sectors += req_size;
	}
	finish_all();

	/* then read sequentially */
	srand(RAND_SEED);
	read_res_0xFFFFFFFF = FALSE;
	lba = rw_case->min_lba;
	num_reqs = 0;
	UINT32 rd_bytes = 0;
	while (rd_bytes < rw_case->max_wr_bytes &&
		lba < rw_case->max_lba &&
		num_reqs < rw_case->max_num_reqs) {
		/* skip read cmd */
		is_read = (rand() % 100) < rw_case->read_percent;
		if (is_read) goto next;

		/* check write cmd */
		do_flash_read(lba, req_size);

		num_reqs++;
		rd_bytes += req_size * BYTES_PER_SECTOR;
next:
		/* for next */
		lba += req_size;
		/* update statisitcs */
		total_sectors += req_size;
	}
	finish_all();

	UINT32	seconds = timer_ellapsed_us() / 1000 / 1000;
	UINT32	mb = total_sectors / 2048;
	uart_print("Done.");
	uart_print("Summary: %u seconds used; %u MB data written and read.",
			seconds, mb);
}

void ftl_test()
{
	uart_print("Start FTL long r/w test");

	declare_rw_case(rw_case);
	rw_case.min_req_size = 8;
	rw_case.max_req_size = 8;
	rw_case.min_lba = 655650;
	rw_case.read_percent = 50;

	do_long_rw_test(&rw_case);

	uart_print("FTL passed unit test ^_^");
}

#endif
