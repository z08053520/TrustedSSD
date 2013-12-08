/* ===========================================================================
 * Unit test for FTL 
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "ftl.h"
#include "misc.h"
#include "test_util.h"
#include <stdlib.h>

#define RAND_SEED	12345
#define MAX_LPA		(NUM_LSECTORS * 7 / 8)
#define MAX_NUM_SECTORS 512
#define MAX_NUM_REQS	1024

extern UINT32 		g_ftl_read_buf_id, g_ftl_write_buf_id;

static void do_flash_write(UINT32 const lba, UINT32 const req_sectors, 
			   UINT32 const sata_val) 
{
	UINT32 lpn          = lba / SECTORS_PER_PAGE;
	UINT32 sect_offset  = lba % SECTORS_PER_PAGE;
	UINT32 num_sectors;
	UINT32 remain_sects = req_sectors;
	UINT32 sata_buf_id  = g_ftl_write_buf_id;
	UINT32 sata_buf     = WR_BUF_PTR(sata_buf_id);

	// prepare SATA buffer by iterating pages in the request
	while (remain_sects) {
		if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
		    num_sectors = remain_sects;
		else
		    num_sectors = SECTORS_PER_PAGE - sect_offset;

		mem_set_dram(sata_buf + BYTES_PER_SECTOR * sect_offset,
			     sata_val,  BYTES_PER_SECTOR * num_sectors);
		
		lpn++;
		sect_offset   = 0;
		remain_sects -= num_sectors;
		sata_buf_id   = (sata_buf_id + 1) % NUM_WR_BUFFERS;
		sata_buf      = WR_BUF_PTR(sata_buf_id);
	}

	// write to flash
	ftl_write(lba, req_sectors);
}

static void do_flash_verify(UINT32 const lba, UINT32 const req_sectors, UINT32 const sata_val) 
{
	// read from flash 
	ftl_read(lba, req_sectors);

	UINT32 lpn          = lba / SECTORS_PER_PAGE;
	UINT32 sect_offset  = lba % SECTORS_PER_PAGE;
	UINT32 remain_sects = req_sectors;
	UINT32 num_sectors;
	UINT32 num_bufs_rd  = COUNT_BUCKETS(req_sectors + sect_offset, SECTORS_PER_PAGE);
	UINT32 sata_buf_id  = g_ftl_read_buf_id - num_bufs_rd;
	UINT32 sata_buf     = RD_BUF_PTR(sata_buf_id);
	
	// verify data by iterating each page in the SATA buffer 
	while (remain_sects) {
		if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
		    num_sectors = remain_sects;
		else
		    num_sectors = SECTORS_PER_PAGE - sect_offset;

		// verify data in buffer
		BUG_ON("data in read buffer is not as expected", 
			is_buff_wrong(sata_buf, sata_val, 
				      sect_offset, num_sectors));

		lpn++;
		sect_offset   = 0;
		remain_sects -= num_sectors;
		sata_buf_id   = (sata_buf_id + 1) % NUM_RD_BUFFERS;
		sata_buf      = RD_BUF_PTR(sata_buf_id);
	}
}

#define NUM_SEQ_REQ_SIZES	5
#define KB2SEC(N)		(N * 1024 / 512)

static void seq_rw_test() 
{
	uart_print("sequential read/write test");

	UINT32 num_requests	= 8;
	/* 512B -> 4KB -> 7KB -> 17KB -> 32KB  */
	UINT32 req_sizes[NUM_SEQ_REQ_SIZES] = {
		1, KB2SEC(4), KB2SEC(7), KB2SEC(17), KB2SEC(32)
	};
	UINT32 lba, req_sectors, val;
	UINT32 i, j;

	BUG_ON("too many requests to run", num_requests > MAX_NUM_REQS);

	// sequential write
	lba = 0;
	for (j = 0; j < NUM_SEQ_REQ_SIZES; j++) {
		req_sectors = req_sizes[j];
		for (i = 0; i < num_requests; i++) {
			val 	= lba * 2;
			do_flash_write(lba, req_sectors, val);

			lba += req_sectors;
		}
	}

	// seqential read and verify
	lba = 0;
	for (j = 0; j < NUM_SEQ_REQ_SIZES; j++) {
		req_sectors = req_sizes[j];
		for (i = 0; i < num_requests; i++) {
			val 	= lba * 2;
			do_flash_verify(lba, req_sectors, val);

			lba += req_sectors;
		}
	}

	uart_print("done");
}

static void rnd_rw_test()
{
	uart_print("random read/write test");
	
	UINT32 num_requests = 8;
	UINT32 lba, req_size, val;
	UINT32 lbas[MAX_NUM_REQS], req_sizes[MAX_NUM_REQS];
	UINT32 i;

	BUG_ON("too many requests to run", num_requests > MAX_NUM_REQS);

	// random write
	for (i = 0; i < num_requests; i++) {
		lba 	  = random(0, MAX_LPA); 
		req_size  = random(1, MAX_NUM_SECTORS); 
		val 	  = lba + 7;

		do_flash_write(lba, req_size, val);

		lbas[i]      = lba;
		req_sizes[i] = req_size;
	}

	// random read and verify
	for (i = 0; i < num_requests; i++) {
		lba	 = lbas[i];
		req_size = req_sizes[i];
		val 	 = lba + 7;

		do_flash_verify(lba, req_size, val);
	}

	uart_print("done");
}

void ftl_test()
{
	uart_print("Start testing FTL unit test ^_^");

	srand(RAND_SEED);

	seq_rw_test();

	rnd_rw_test();

	uart_print("FTL passed unit test ^_^");
}

#endif
