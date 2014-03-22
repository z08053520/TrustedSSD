#include "jasmine.h"
#if OPTION_FTL_TEST
#include "test_ftl_rw_common.h"

/* DRAM buffer to remember the value of every sector from 0 to BUF_SIZE */
#define SECTOR_VAL_BUF_ADDR	HIL_BUF_ADDR
SETUP_BUF(sector_val, 		SECTOR_VAL_BUF_ADDR, 	(2 * SECTORS_PER_PAGE));
#define BUF_SIZE		(2 * BYTES_PER_PAGE / sizeof(UINT32))

/* #define HIL_NEXT_BUF_ADDR	(HIL_BUF_ADDR + BYTES_PER_PAGE) */
/* #if BYTES_PER_PAGE != (TEMP_BUF_ADDR - HIL_BUF_ADDR) */
/* 	#error hil and temp buffer is not together */
/* #endif */

/* FTL read thread evokes this function to verify the result in SATA read buf */
void ftl_verify(UINT32 const lpn, UINT8 const sect_offset,
		UINT8 const num_sectors, UINT32 const sata_rd_buf)
{
	debug("verify lpn = %u, sect_offset = %u, num_sectors = %u",
		lpn, sect_offset, num_sectors);

	UINT32 curr_lba = lpn * SECTORS_PER_PAGE + sect_offset;
	UINT8 sect_i = sect_offset, sect_end = sect_offset + num_sectors;
	while (sect_i < sect_end) {
		if (curr_lba >= BUF_SIZE) {
			debug("warning: cannot verify sector %u", curr_lba);
			break;
		}

		UINT32 expected_sect_val = get_sector_val(curr_lba);
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
		.max_lba = BUF_SIZE - 1,	\
		.min_req_size = 1,		\
		.max_req_size = 128,		\
		.max_num_reqs = MAX_UINT32,	\
		.max_wr_bytes = 512 * MB	\
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
			UINT32 sect_val = rand();
			mem_set_dram(sata_buf + BYTES_PER_SECTOR * sect_i,
					sect_val, BYTES_PER_SECTOR);
			/* remember write requests to verify later */
			set_sector_val(curr_lba, sect_val);

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

void do_rnd_rw_test(rw_case_t *rw_case)
{
	UINT32  total_sectors = 0;
	timer_reset();

	uart_print("random r/w range: from sector 0 to sector %u, a total of %uMB",
			BUF_SIZE - 1, BUF_SIZE / 2048);
	rw_case->max_lba = MIN(rw_case->max_lba, BUF_SIZE - 1);

	/* first write randomly */
	UINT32 lba, req_size;
	UINT32 num_reqs = 0, wr_bytes = 0;
	while (wr_bytes < rw_case->max_wr_bytes &&
		num_reqs < rw_case->max_num_reqs) {
		/* do flash write */
		lba = random(rw_case->min_lba, rw_case->max_lba);
		req_size = random(rw_case->min_req_size, rw_case->max_req_size);
		if (lba + req_size > rw_case->max_lba + 1)
			req_size = rw_case->max_lba + 1 - lba;
		do_flash_write(lba, req_size);

		/* for next */
		num_reqs++;
		wr_bytes += req_size * BYTES_PER_SECTOR;

		/* update statisitcs */
		total_sectors += req_size;
	}
	finish_all();

	/* then read randomly */
	UINT32 rd_bytes = 0;
	for (UINT32 req_i = 0; req_i < num_reqs; req_i++) {
		lba = random(rw_case->min_lba, rw_case->max_lba);
		req_size = random(rw_case->min_req_size, rw_case->max_req_size);
		if (lba + req_size > rw_case->max_lba + 1)
			req_size = rw_case->max_lba + 1 - lba;

		do_flash_read(lba, req_size);

		total_sectors += req_size;
		rd_bytes += req_size * BYTES_PER_SECTOR;
		if (rd_bytes > rw_case->max_wr_bytes) break;
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
	uart_print("Start FTL rnd r/w test");

	srand(RAND_SEED);
	init_sector_val_buf(0xFFFFFFFF);

	declare_rw_case(rw_case);
	/* rw_case.max_num_reqs = ; */
	/* rw_case.max_req_size = 64; */
	/* rw_case.max_req_size = 128; */

	do_rnd_rw_test(&rw_case);

	uart_print("FTL passed unit test ^_^");
}

#endif
