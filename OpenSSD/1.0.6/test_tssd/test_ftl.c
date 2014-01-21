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
#include "write_buffer.h"
#include <stdlib.h>

#include "flash_util.h"
#include "pmt.h"

#define RAND_SEED	1234567
#define MAX_NUM_SECTORS 512
//#define MAX_NUM_SECTORS SECTORS_PER_PAGE
#define MAX_NUM_REQS	MAX_LBA_BUF_ENTRIES
/* #define MAX_NUM_REQS	16 */	

extern UINT32	g_num_ftl_write_tasks_submitted;
extern UINT32	g_num_ftl_read_tasks_submitted;

extern BOOL8 	eventq_put(UINT32 const lba, UINT32 const num_sectors, 
			   UINT32 const cmd_type);

/* ===========================================================================
 * DRAM buffer to store lbas and req_sizes 
 * =========================================================================*/

#define LBA_BUF_ADDR		TEMP_BUF_ADDR
#define LBA_BUF_SECTORS		(SECTORS_PER_PAGE / 2)
#define MAX_LBA_BUF_ENTRIES	(LBA_BUF_SECTORS * BYTES_PER_SECTOR / sizeof(UINT32))

#define REQ_SIZE_BUF_ADDR	(LBA_BUF_ADDR + BYTES_PER_SECTOR * LBA_BUF_SECTORS)
#define REQ_SIZE_BUF_SECTORS	(SECTORS_PER_PAGE / 2)

#define VAL_BUF_ADDR		HIL_BUF_ADDR
#define VAL_BUF_SECTORS		SECTORS_PER_PAGE

#define MAX_LBA_LIMIT_BY_VAL_BUF \
				(VAL_BUF_SECTORS * BYTES_PER_SECTOR / sizeof(UINT32) - 1)

SETUP_BUF(lba, 		LBA_BUF_ADDR, 		LBA_BUF_SECTORS);
SETUP_BUF(req_size, 	REQ_SIZE_BUF_ADDR, 	REQ_SIZE_BUF_SECTORS);
SETUP_BUF(val, 		VAL_BUF_ADDR, 		VAL_BUF_SECTORS);

static void init_dram()
{
	init_lba_buf(0);
	init_req_size_buf(0);
	init_val_buf(0xFFFFFFFF);
}

/* ===========================================================================
 * Fake SATA R/W requests 
 * =========================================================================*/

static void finish_all()
{
	BOOL8 idle;
	do {
		idle = ftl_main();
	} while(!idle);
}

extern BOOL8 ftl_all_sata_cmd_accepted();

static void accept_all()
{
	do {
		ftl_main();
	} while(!ftl_all_sata_cmd_accepted());
}

static void do_flash_write(UINT32 const lba, UINT32 const req_sectors, 
			   UINT32 const sata_val, BOOL8 use_val_buf) 
{
	ftl_main();

	UINT32 lpn          = lba / SECTORS_PER_PAGE;
	UINT32 sect_offset  = lba % SECTORS_PER_PAGE;
	UINT32 num_sectors;
	UINT32 remain_sects = req_sectors;
	UINT32 sata_buf_id  = g_num_ftl_write_tasks_submitted % NUM_SATA_WR_BUFFERS;
	UINT32 sata_buf     = SATA_WR_BUF_PTR(sata_buf_id);
	UINT32 tmp_lba, sect_limit;

	// prepare SATA buffer by iterating pages in the request
	while (remain_sects) {
		if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
		    num_sectors = remain_sects;
		else
		    num_sectors = SECTORS_PER_PAGE - sect_offset;

		/* mem_set_dram(sata_buf + BYTES_PER_SECTOR * sect_offset, */
		/* 	     sata_val,  BYTES_PER_SECTOR * num_sectors); */
	
		/* uart_printf("> lpn = %u, tmp_lba = %u, offset = %u, num_sectors = %u\r\n", */ 
		/* 	    lpn, lpn * SECTORS_PER_PAGE + sect_offset, sect_offset, num_sectors); */
		/* UINT8 count = 0; */
		/* uart_printf("vals = ["); */
		/* UINT8 i; */
		/* for (i = sect_offset; i < sect_offset + num_sectors; i++) { */
		/* 	if (i != sect_offset && count % 10 == 0) uart_print(""); */
		/* 	uart_printf("%u ", sector_vals[i]); */
		/* 	count++; */
		/* } */
		/* uart_print("]"); */

		UINT32 sector_vals[SECTORS_PER_PAGE];
		if (use_val_buf) {
			clear_vals(sector_vals, lpn);
			set_vals(sector_vals, lpn * SECTORS_PER_PAGE, sect_offset, num_sectors);

			for (tmp_lba = lpn * SECTORS_PER_PAGE + sect_offset,
			     sect_limit = sect_offset + num_sectors;
			     sect_offset < sect_limit; 
			     sect_offset++, tmp_lba++) {
				/* set_val(tmp_lba, sata_val); */
				set_val(tmp_lba, sector_vals[sect_offset]);
				/* if (tmp_lba == 1083) */
				/* 	uart_printf("lba = %u, req_sectors = %u\r\n", */ 
				/* 		    lba, req_sectors); */
			}
		}
		else {
			clear_vals(sector_vals, sata_val);
		}
		fill_buffer(sata_buf, 0, SECTORS_PER_PAGE, sector_vals);

		lpn++;
		sect_offset   = 0;
		remain_sects -= num_sectors;
		sata_buf_id   = (sata_buf_id + 1) % NUM_SATA_WR_BUFFERS;
		sata_buf      = SATA_WR_BUF_PTR(sata_buf_id);
	}

	// write to flash
	while(eventq_put(lba, req_sectors, WRITE))
		ftl_main();
	/* accept_all(); */
	/* finish_all(); */
}

static void do_flash_verify(UINT32 const lba, UINT32 const req_sectors, 
			    UINT32 sata_val,  BOOL8 const use_val_buf) 
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
	UINT32 tmp_lba, sect_limit;	
	
	// verify data by iterating each page in the SATA buffer 
	while (remain_sects) {
		if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
			num_sectors = remain_sects;
		else
			num_sectors = SECTORS_PER_PAGE - sect_offset;
		
		/* uart_printf("> lpn = %u, offset = %u, num_sectors = %u\r\n", */ 
		/* 	    lpn, sect_offset, num_sectors); */

		if (!use_val_buf) {
			// verify data in buffer
			BUG_ON("data in read buffer is not as expected (1)", 
				is_buff_wrong(sata_buf, sata_val, 
					      sect_offset, num_sectors));
		}
		else {
			for (tmp_lba = lpn * SECTORS_PER_PAGE + sect_offset, 
			     sect_limit = sect_offset + num_sectors;
			     sect_offset < sect_limit; 
			     sect_offset++, tmp_lba++) 
			{
				sata_val = get_val(tmp_lba);
				
				BUG_ON("data in read buffer is not as expected (2)", 
					is_buff_wrong(sata_buf, sata_val, 
						      sect_offset, 1));
			}
		}

		lpn++;
		sect_offset   = 0;
		remain_sects -= num_sectors;
		sata_buf_id   = (sata_buf_id + 1) % NUM_SATA_RD_BUFFERS;
		sata_buf      = SATA_RD_BUF_PTR(sata_buf_id);
	}
}

/* ===========================================================================
 * Tests for sequential and random R/W 
 * =========================================================================*/

/* #define NUM_SEQ_REQ_SIZES	1 */
#define NUM_SEQ_REQ_SIZES	5
#define KB2SEC(N)		(N * 1024 / 512)

static void seq_rw_test() 
{
	uart_print("sequential read/write test");

	UINT32 num_requests	= MAX_NUM_REQS;
	//UINT32 num_requests	= NUM_BC_BUFFERS;
	/* 512B -> 4KB -> 8KB -> 16KB -> 32KB  */
	UINT32 req_sizes[NUM_SEQ_REQ_SIZES] = {
		1, KB2SEC(4), KB2SEC(8), KB2SEC(16), KB2SEC(32)
		/* 1 */
	};
	UINT32 lba, req_sectors, val;
	UINT32 i, j;
	UINT32 total_sectors;

	uart_print("sequential write");
	lba = 0;
	for (j = 0; j < NUM_SEQ_REQ_SIZES; j++) {
		req_sectors = req_sizes[j];
		uart_printf("req_sectors = %u\r\n", req_sectors);
		total_sectors = 0;
		perf_monitor_reset();
		for (i = 0; i < num_requests; i++) {
			/* uart_printf("i = %u\r\n", i); */
			val 	= lba;
			do_flash_write(lba, req_sectors, val, FALSE);

			lba += req_sectors;
			total_sectors += req_sectors;
		}
		finish_all();
		perf_monitor_update(total_sectors);
	}

	uart_print("seqential read and verify");
	lba = 0;
	for (j = 0; j < NUM_SEQ_REQ_SIZES; j++) {
		req_sectors = req_sizes[j];
		uart_printf("req_sectors = %u\r\n", req_sectors);
		total_sectors = 0;
		perf_monitor_reset();
		for (i = 0; i < num_requests; i++) {
			/* uart_printf("i = %u\r\n", i); */
			val 	= lba;
			do_flash_verify(lba, req_sectors, val, FALSE);

			lba += req_sectors;
			total_sectors += req_sectors;
		}
		perf_monitor_update(total_sectors);	
	}

	uart_print("done");
}

static void rnd_rw_test()
{
	uart_print("random read/write test");
	
	UINT32 num_requests = MAX_NUM_REQS;
	/* UINT32 num_requests = 120; */
	UINT32 lba, req_size, val;
	UINT32 i;
	UINT32 total_sectors;

	BUG_ON("too many requests to run", num_requests > MAX_NUM_REQS);

	uart_printf("max lba limited by val buf = %u\r\n", MAX_LBA_LIMIT_BY_VAL_BUF);
	uart_printf("there are %u requests\r\n", num_requests);

	uart_print("random write");
	perf_monitor_reset();
	total_sectors = 0;
	for (i = 0; i < num_requests; i++) {
		lba 	  = random(0, MAX_LBA_LIMIT_BY_VAL_BUF);
		val 	  = lba;
		/* req_size  = random(1, MAX_NUM_SECTORS); */ 
		req_size  = random(1, 64); 
		if (lba + req_size > MAX_LBA_LIMIT_BY_VAL_BUF)
			req_size = MAX_LBA_LIMIT_BY_VAL_BUF - lba + 1;

		/* if (i<200) continue; */
		
		/* uart_printf("> i = %u, lba = %u, req_size = %u, val = %u\r\n", */ 
		/* 	    i, lba, req_size, val); */

		do_flash_write(lba, req_size, val, TRUE);

		set_lba(i, lba);
		set_req_size(i, req_size);
	
		total_sectors += req_size;
	}
	finish_all();
	perf_monitor_update(total_sectors);	
	
	uart_print("check write operation by issuing flash cmds directly");
	lba = 0;
	while (lba < MAX_LBA_LIMIT_BY_VAL_BUF) {
		BOOL8	is_subpage_written = FALSE;
		UINT8	offset;
		for (offset = 0; offset < SECTORS_PER_SUB_PAGE; offset++)
			if (get_val(lba + offset) != 0xFFFFFFFF) {
				is_subpage_written = TRUE;
				break;
			}
		if (!is_subpage_written) goto next;

		UINT32 res_buf = COPY_BUF(0);
		mem_set_dram(res_buf, 1111, BYTES_PER_PAGE);

		UINT8	sp_offset = lba % SECTORS_PER_PAGE;

		UINT32	lspn = lba / SECTORS_PER_SUB_PAGE;
		vp_t	vp;
		pmt_fetch(lspn, &vp);
		
		/* uart_printf("lba %u, lspn %u is in vpn %u\r\n", */ 
		/* 	    lba, lspn, vp.vpn); */

		if (vp.vpn != NULL) {
			vsp_t	vsp = {
				.bank = vp.bank, 
				.vspn = vp.vpn * SUB_PAGES_PER_PAGE 
				      + sp_offset / SECTORS_PER_SUB_PAGE
			};
			fu_read_sub_page(vsp, res_buf, FU_SYNC);
		}

		UINT32	lpn = lba / SECTORS_PER_PAGE;
		UINT32	wr_buf;
		sectors_mask_t valid_sectors;
		write_buffer_get(lpn, &wr_buf, &valid_sectors);
		if (wr_buf != NULL)
			fu_copy_buffer(res_buf, wr_buf, valid_sectors);	

		/* uart_printf("result buf = ["); */
		/* for (offset = 0; offset < SECTORS_PER_SUB_PAGE; offset++) { */
		/* 	uart_printf("%u ", read_dram_32( */
		/* 				res_buf */ 
		/* 				+ (sp_offset + offset) * BYTES_PER_SECTOR)); */
		/* } */
		/* uart_print("]"); */

		for (offset = 0; offset < SECTORS_PER_SUB_PAGE; offset++) {
			UINT32	val = get_val(lba + offset);
			if (val == 0xFFFFFFFF) {
				/* uart_printf("> sector %u = 0xFFFFFFFF\r\n", lba + offset); */
				continue;
			}

			/* uart_printf("> expecting val %u for sector %u; actual val %u\r\n", */ 
			/* 	    lba + offset, val, */ 
			/* 	    read_dram_32(res_buf + */ 
			/* 		    (sp_offset + offset) * BYTES_PER_SECTOR)); */

			BOOL8	wrong = is_buff_wrong(res_buf, 
						      val, sp_offset + offset, 1);
			BUG_ON("written data is not as expected", wrong);
		}
next:
		lba += SECTORS_PER_SUB_PAGE;
	}
	uart_print("write operations are validated ^_^");


	uart_print("random read and verify");
	perf_monitor_reset();
	total_sectors = 0;
	for (i = 0; i < num_requests; i++) {
		lba	 = get_lba(i);
		req_size = get_req_size(i);
		
		/* if (i<13) continue; */
		/* uart_printf("> i = %u, lba = %u, req_size = %u\r\n", */ 
		/* 	    i, lba, req_size); */

		do_flash_verify(lba, req_size, 0, TRUE);
		total_sectors += req_size;		
	}
	perf_monitor_update(total_sectors);	

	uart_print("done");
}

static void long_seq_rw_test() 
{
	uart_print("long sequential read/write test");

	const UINT32 req_size_pattern[] = {
		//1, 7, 15, 6, 23, 125, 67,	/* in sectors */
		32,
		0 /* end */
	};
	const UINT32 total_bytes = 256 * 1024 * 1024; /* 1GB */

	UINT32 lba, req_sectors, val;
	UINT32 pattern_i;
	UINT32 num_bytes_so_far;

	uart_print("long sequential write");
	perf_monitor_reset();
	lba = 0;
	pattern_i = 0;
	num_bytes_so_far = 0;
	while (num_bytes_so_far < total_bytes) {
		req_sectors 	  = req_size_pattern[pattern_i];
		val		  = lba;
		do_flash_write(lba, req_sectors, val, FALSE);

		lba 		 += req_sectors;
		pattern_i 	  = req_size_pattern[pattern_i+1] > 0 ? 
					pattern_i + 1 : 0;
		num_bytes_so_far += req_sectors * BYTES_PER_SECTOR;
	}
	finish_all();
	perf_monitor_update(num_bytes_so_far / BYTES_PER_SECTOR);

	uart_print("long seqential read and verify");
	perf_monitor_reset();
	lba = 0;
	pattern_i = 0;
	num_bytes_so_far = 0;
	while (num_bytes_so_far < total_bytes) {
		req_sectors 	  = req_size_pattern[pattern_i];
		val		  = lba;
		do_flash_verify(lba, req_sectors, val, FALSE);

		lba 		 += req_sectors;
		pattern_i 	  = req_size_pattern[pattern_i+1] > 0 ? 
					pattern_i + 1 : 0;
		num_bytes_so_far += req_sectors * BYTES_PER_SECTOR;
	}
	perf_monitor_update(num_bytes_so_far / BYTES_PER_SECTOR);
	uart_print("done");
}

void ftl_test()
{
	uart_print("Start testing FTL unit test");

	perf_monitor_set_output_threshold(1024 * 1024);

	init_dram();

	srand(RAND_SEED);

	seq_rw_test();
	rnd_rw_test();
	
  	long_seq_rw_test();	
	long_seq_rw_test();

	uart_print("FTL passed unit test ^_^");
}

#endif
