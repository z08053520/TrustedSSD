/* ===========================================================================
 * Unit test for FTL 
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "ftl.h"
#include "test_util.h"
#include <stdlib.h>

#define RAND_SEED	12345
#define MAX_LPA		(NUM_LSECTORS * 7 / 8)
#define MAX_NUM_SECTORS 1024

extern UINT32 		g_ftl_read_buf_id, g_ftl_write_buf_id;

void ftl_test()
{
	UINT32 i, num_requests = 8;
	UINT32 lba, req_sectors;

	UINT32 remain_sects, num_sectors;
    	UINT32 lpn, sect_offset;

	UINT32 sata_val, sata_buf, sata_buf_id;
	UINT32 num_bufs_rw;

	uart_print("Start testing FTL unit test ^_^");

	srand(RAND_SEED);

	for(i = 0; i < num_requests; i++) {
		// randomly decide write requst
		lba 	     = rand() % MAX_LPA;  
		req_sectors  = rand() % MAX_NUM_SECTORS + 1;
	
		// init SATA buf by iterating each page in the request
		lpn          = lba / SECTORS_PER_PAGE;
		sect_offset  = lba % SECTORS_PER_PAGE;
		remain_sects = req_sectors;
		sata_buf_id  = g_ftl_write_buf_id;
		sata_buf     = WR_BUF_PTR(sata_buf_id);
		num_bufs_rw = 0;
		while (remain_sects) {
			if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
			    num_sectors = remain_sects;
			else
			    num_sectors = SECTORS_PER_PAGE - sect_offset;
	
			sata_val     = lpn;
			// prepare fake SATA buffer
			mem_set_dram(sata_buf + BYTES_PER_SECTOR * sect_offset,
				     sata_val,  BYTES_PER_SECTOR * num_sectors);
			
			lpn++;
			sect_offset   = 0;
			remain_sects -= num_sectors;
			sata_buf_id   = (sata_buf_id + 1) % NUM_WR_BUFFERS;
			sata_buf      = WR_BUF_PTR(sata_buf_id);
			num_bufs_rw ++;
		}

		// write to flash
		ftl_write(lba, req_sectors);

		// read from flash 
		ftl_read(lba, req_sectors);

		// verify data by iterating each page in the SATA buffer 
		lpn          = lba / SECTORS_PER_PAGE;
		sect_offset  = lba % SECTORS_PER_PAGE;
		remain_sects = req_sectors;
		sata_buf_id  = g_ftl_read_buf_id - num_bufs_rw;
		sata_buf     = RD_BUF_PTR(sata_buf_id);
		while (remain_sects) {
			if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
			    num_sectors = remain_sects;
			else
			    num_sectors = SECTORS_PER_PAGE - sect_offset;

			sata_val = lpn;
			// verify data in buffer
			BUG_ON("data in read buffer is not as expected", 
				is_buff_wrong(sata_buf, sata_val, 
					      sect_offset, num_sectors));

			lpn++;
			sect_offset   = 0;
			remain_sects -= num_sectors;
			sata_buf_id   = (sata_buf_id + 1) % NUM_WR_BUFFERS;
			sata_buf      = WR_BUF_PTR(sata_buf_id);
		}
	}

	uart_print("FTL passed unit test ^_^");
}

#endif


