/* ===========================================================================
 * Unit test for FTL 
 * =========================================================================*/

#include "jasmine.h"
#if OPTION_FTL_TEST
#include "ftl.h"
#include "misc.h"
#include "test_util.h"
#include <stdlib.h>

#define RAND_SEED	1234
#define MAX_NUM_SECTORS 512
//#define MAX_NUM_SECTORS SECTORS_PER_PAGE
#define MAX_NUM_REQS	MAX_LBA_BUF_ENTRIES

extern UINT32 		g_ftl_read_buf_id, g_ftl_write_buf_id;

/* ===========================================================================
 * Performance Report 
 * =========================================================================*/

#define PERF_REPORT_THRESHOLD	(16 * 1024 * 1024) 	/* 16MB */
static UINT32 pm_total_bytes;

static void perf_monitor_reset() 
{
	pm_total_bytes = 0;
	timer_reset();
}

static void perf_monitor_report()
{
	UINT32 time_us 	  = timer_ellapsed_us();
	UINT32 throughput = pm_total_bytes / time_us;
  
	uart_printf("Transferred %d bytes (~%dMB) in %dus (~%dms), "
		    "throughput %dMB/s\r\n",
		    pm_total_bytes, pm_total_bytes / 1024 /1024,
		    time_us, time_us / 1000, 
		    throughput);
}

static void perf_monitor_update(UINT32 const num_sectors)
{
	pm_total_bytes += BYTES_PER_SECTOR * num_sectors;
	
	if (pm_total_bytes >= PERF_REPORT_THRESHOLD) {
		perf_monitor_report();

		pm_total_bytes = 0;
		timer_reset();
	}
}

/* ===========================================================================
 * DRAM buffer to store lbas and req_sizes 
 * =========================================================================*/

#define SETUP_BUF(name, addr, sectors)	\
		const UINT32 __BUF_##name##_BYTES = (BYTES_PER_SECTOR * sectors);\
		static void init_##name##_buf() {\
			mem_set_dram(addr, 0, __BUF_##name##_BYTES);\
		}\
		static UINT32 get_##name(UINT32 const i) {\
			UINT32 offset = sizeof(UINT32) * i;\
			BUG_ON("out of bound", offset >= __BUF_##name##_BYTES);\
			return read_dram_32(addr + offset);\
		}\
		static void set_##name(UINT32 const i, UINT32 const val) {\
			UINT32 offset = sizeof(UINT32) * i;\
			BUG_ON("out of bound", offset >= __BUF_##name##_BYTES);\
			write_dram_32(addr + offset, val);\
		}

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
	init_lba_buf();
	init_req_size_buf();
	init_val_buf();
}

/* ===========================================================================
 * Fake SATA R/W requests 
 * =========================================================================*/

static void do_flash_write(UINT32 const lba, UINT32 const req_sectors, 
			   UINT32 const sata_val, BOOL8 use_val_buf) 
{
	UINT32 lpn          = lba / SECTORS_PER_PAGE;
	UINT32 sect_offset  = lba % SECTORS_PER_PAGE;
	UINT32 num_sectors;
	UINT32 remain_sects = req_sectors;
	UINT32 sata_buf_id  = g_ftl_write_buf_id;
	UINT32 sata_buf     = WR_BUF_PTR(sata_buf_id);
	UINT32 tmp_lba, sect_limit;

	// prepare SATA buffer by iterating pages in the request
	while (remain_sects) {
		if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
		    num_sectors = remain_sects;
		else
		    num_sectors = SECTORS_PER_PAGE - sect_offset;

		mem_set_dram(sata_buf + BYTES_PER_SECTOR * sect_offset,
			     sata_val,  BYTES_PER_SECTOR * num_sectors);
	
		if (use_val_buf) {
			for (tmp_lba = lpn * SECTORS_PER_PAGE + sect_offset,
			     sect_limit = sect_offset + num_sectors;
			     sect_offset < sect_limit; 
			     sect_offset++, tmp_lba++) 
				set_val(tmp_lba, sata_val);
		}

		lpn++;
		sect_offset   = 0;
		remain_sects -= num_sectors;
		sata_buf_id   = (sata_buf_id + 1) % NUM_WR_BUFFERS;
		sata_buf      = WR_BUF_PTR(sata_buf_id);
	}

	// write to flash
	ftl_write(lba, req_sectors);
}

static void do_flash_verify(UINT32 const lba, UINT32 const req_sectors, 
			    UINT32 sata_val,  BOOL8 const use_val_buf) 
{
	// read from flash 
	ftl_read(lba, req_sectors);
	flash_finish();

	UINT32 lpn          = lba / SECTORS_PER_PAGE;
	UINT32 sect_offset  = lba % SECTORS_PER_PAGE;
	UINT32 remain_sects = req_sectors;
	UINT32 num_sectors;
	UINT32 num_bufs_rd  = COUNT_BUCKETS(req_sectors + sect_offset, SECTORS_PER_PAGE);
	UINT32 sata_buf_id  = g_ftl_read_buf_id >= num_bufs_rd ? 
					g_ftl_read_buf_id - num_bufs_rd : 
					NUM_RD_BUFFERS + g_ftl_read_buf_id - num_bufs_rd;	
	UINT32 sata_buf     = RD_BUF_PTR(sata_buf_id);
	UINT32 tmp_lba, sect_limit;
	
	
	// verify data by iterating each page in the SATA buffer 
	while (remain_sects) {
		if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
		    num_sectors = remain_sects;
		else
		    num_sectors = SECTORS_PER_PAGE - sect_offset;

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
		sata_buf_id   = (sata_buf_id + 1) % NUM_RD_BUFFERS;
		sata_buf      = RD_BUF_PTR(sata_buf_id);
	}
}

/* ===========================================================================
 * Tests for sequential and random R/W 
 * =========================================================================*/

#define NUM_SEQ_REQ_SIZES	5
#define KB2SEC(N)		(N * 1024 / 512)

static void seq_rw_test() 
{
	uart_print("sequential read/write test");

	UINT32 num_requests	= MAX_NUM_REQS;
	//UINT32 num_requests	= NUM_BC_BUFFERS;
	/* 512B -> 4KB -> 7KB -> 17KB -> 32KB  */
	UINT32 req_sizes[NUM_SEQ_REQ_SIZES] = {
		1, KB2SEC(4), KB2SEC(7), KB2SEC(17), KB2SEC(32)
	};
	UINT32 lba, req_sectors, val;
	UINT32 i, j;

	uart_print("sequential write");
	perf_monitor_reset();
	lba = 0;
	for (j = 0; j < NUM_SEQ_REQ_SIZES; j++) {
		req_sectors = req_sizes[j];
		for (i = 0; i < num_requests; i++) {
			val 	= lba;
			do_flash_write(lba, req_sectors, val, FALSE);

			lba += req_sectors;
			perf_monitor_update(req_sectors);
		}
	}

	uart_print("seqential read and verify");
	perf_monitor_reset();
	lba = 0;
	for (j = 0; j < NUM_SEQ_REQ_SIZES; j++) {
		req_sectors = req_sizes[j];
		for (i = 0; i < num_requests; i++) {
			val 	= lba;
			do_flash_verify(lba, req_sectors, val, FALSE);

			lba += req_sectors;
			perf_monitor_update(req_sectors);	
		}
	}

	uart_print("done");
}

static void rnd_rw_test()
{
	uart_print("random read/write test");
	
	UINT32 num_requests = MAX_NUM_REQS;
	UINT32 lba, req_size, val;
	UINT32 i;

	BUG_ON("too many requests to run", num_requests > MAX_NUM_REQS);

	uart_printf("max lba limited by val buf = %u\r\n", MAX_LBA_LIMIT_BY_VAL_BUF);

	uart_print("random write");
	perf_monitor_reset();
	for (i = 0; i < num_requests; i++) {
		lba 	  = random(0, MAX_LBA_LIMIT_BY_VAL_BUF);
		val 	  = lba;
		req_size  = random(1, MAX_NUM_SECTORS); 
		if (lba + req_size > MAX_LBA_LIMIT_BY_VAL_BUF)
			req_size = MAX_LBA_LIMIT_BY_VAL_BUF - lba + 1;

		do_flash_write(lba, req_size, val, TRUE);

		set_lba(i, lba);
		set_req_size(i, req_size);
		
		perf_monitor_update(req_size);	
	}

	uart_print("random read and verify");
	perf_monitor_reset();
	for (i = 0; i < num_requests; i++) {
		lba	 = get_lba(i);
		req_size = get_req_size(i);

		do_flash_verify(lba, req_size, 0, TRUE);
		
		perf_monitor_update(req_size);	
	}

	uart_print("done");
}

void ftl_test()
{
	uart_print("Start testing FTL unit test");

	init_dram();

	srand(RAND_SEED);
	seq_rw_test();
	rnd_rw_test();

	uart_print("FTL passed unit test ^_^");
}

#endif
