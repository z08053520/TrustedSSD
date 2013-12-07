/* ===========================================================================
 * Unit test for FTL 
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "ftl.h"
#include <stdlib.h>

#define RAND_SEED	12345
#define MAX_LPA		(NUM_LSECTORS * 7 / 8)
#define MAX_NUM_SECTORS 1024

extern UINT32 		g_ftl_read_buf_id, g_ftl_write_buf_id;

static BOOL8 is_buff_wrong(UINT32 buff_addr, UINT32 val,
			   UINT8 offset, UINT8 num_sectors)
{
	if (offset >= SECTORS_PER_PAGE || num_sectors == 0) return FALSE;	
/*  	
	UINT32 i;
	UINT32 v;
	UINT32 j;

	INFO("is_buff_wrong", "Print the first/last five UINT32 sata_values in every sata_valid sectors of buffer: from %d, to %d",
				offset, offset + num_sectors);
	for(j = 0; j < num_sectors; j++) {
		uart_printf("sector %d: ", offset + j);
		for(i = 0; i < 5; i++) {
			v = read_dram_32(buff_addr + BYTES_PER_SECTOR * (offset+j) + i * sizeof(UINT32));
			if (i) uart_printf(", ");
			uart_printf("%d", v);
		}
		uart_printf("... ");
		for(i = 5; i > 0; i--) {
			v = read_dram_32(buff_addr + BYTES_PER_SECTOR * (offset+j+1) - i * sizeof(UINT32));
			if (i != 5) uart_printf(", ");
			uart_printf("%d", v);
		}
		uart_print("");
	}
*/	
	buff_addr    	    = buff_addr + BYTES_PER_SECTOR * offset;
	UINT32 buff_entries = BYTES_PER_SECTOR * num_sectors / sizeof(UINT32);

    	UINT32 min_idx  = mem_search_min_max(
				buff_addr,    sizeof(UINT32), 
				buff_entries, MU_CMD_SEARCH_MIN_DRAM);
	UINT32 min_val  = read_dram_32(buff_addr + min_idx * sizeof(UINT32));
	if (min_val != val) {
		uart_printf("expect min val to be %d but was %d, at position %d\r\n", val, min_val, min_idx);
		return TRUE;
	}
    	
	UINT32 max_idx  = mem_search_min_max(
				buff_addr,    sizeof(UINT32),
                              	buff_entries, MU_CMD_SEARCH_MAX_DRAM);
	UINT32 max_val  = read_dram_32(buff_addr + max_idx * sizeof(UINT32));
	if (max_val != val) {
		uart_printf("expect max val to be %d but was %d, at position %d\r\n", val, min_val);
		return TRUE;
	}

	return FALSE;
}

void ftl_test()
{
	UINT32 i, num_requests = 8;
	UINT32 lba, req_sectors;

	UINT32 remain_sects, num_sectors;
    	UINT32 lpn, sect_offset;

	UINT32 sata_val, sata_buf, sata_buf_id;
	UINT32 num_bufs_rw;

	srand(RAND_SEED);

	for(i = 0; i < num_requests; i++) {
		// randomly decide write requst
		lba 	     = rand() % MAX_LPA;  
		req_sectors  = rand() % MAX_NUM_SECTORS + 1;
		sata_val     = i;
	
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
}

#endif


