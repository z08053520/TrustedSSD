/* ===========================================================================
 * Unit test for write buffer 
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "dram.h"
#include "pmt.h"
#include "test_util.h"
#include "write_buffer.h"
#include <stdlib.h>

/* LSPN and LBA share the same DRAM buffer */
#define LSPN_BUF	TEMP_BUF_ADDR
#define LBA_BUF		TEMP_BUF_ADDR
#define VAL_BUF		HIL_BUF_ADDR
#define BUF_SIZE	(BYTES_PER_PAGE / sizeof(UINT32))
/* #define BUF_SIZE	256 */	
SETUP_BUF(lspn,		LSPN_BUF,	SECTORS_PER_PAGE);
SETUP_BUF(val,		VAL_BUF,	SECTORS_PER_PAGE);
SETUP_BUF(lba,		LBA_BUF,	SECTORS_PER_PAGE);

#define RAND_SEED	123456

extern UINT32 g_ftl_read_buf_id, g_ftl_write_buf_id;

static void test_write_in_sector()
{
	uart_print("Test writing in sector...");

	srand(RAND_SEED);

	init_lba_buf(0xFFFFFFFF);
	init_val_buf(0);

	uart_print("Write randomly...");
	UINT32 buf_size = 0;
	while (buf_size < BUF_SIZE) {
		UINT32 lpn 	= rand() % NUM_LPAGES;
		UINT8  offset	= rand() % SECTORS_PER_PAGE;
		UINT8  num_sectors = random(1, SECTORS_PER_PAGE - offset);
		/* UINT8  num_sectors = 1; */

		UINT32 val 	= rand();		
		mem_set_dram(SATA_WR_BUF_PTR(g_ftl_write_buf_id) 
				+ offset * BYTES_PER_SECTOR,
			     val,
			     num_sectors * BYTES_PER_SECTOR);
		/* uart_print("-----------------------------------------------"); */	
		/* uart_printf("buf_size = %u, lpn = %u, val = %u, offset = %u, num_sectors = %u\r\n", */ 
		/* 	   buf_size, lpn, val, offset, num_sectors); */
	
		UINT32 lba = lpn * SECTORS_PER_PAGE + offset,
		       lba_end = lba + num_sectors;
		while (lba < lba_end && buf_size < BUF_SIZE) {
			UINT32 old_idx = mem_search_equ_dram(LBA_BUF,
						sizeof(UINT32), 
						BUF_SIZE,
						lba);
			if (old_idx < BUF_SIZE) {
				set_val(old_idx, val);
			}
			else {
				set_lba(buf_size, lba);
				set_val(buf_size, val);
				buf_size++;
			}
			lba++;
		}
		
		write_buffer_put(lpn, offset, num_sectors, 
				 SATA_WR_BUF_PTR(g_ftl_write_buf_id));
		
		g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) 
				   % NUM_SATA_WR_BUFFERS;
	}
	flash_finish();

	uart_print("Verify the result by reading from flash "
		   "or checking buffer...");
	UINT32 buf_idx = 0;
	while (buf_idx < BUF_SIZE) {
		UINT32 lba 	= get_lba(buf_idx);
		UINT32 val	= get_val(buf_idx);

		UINT32 lspn	= lba / SECTORS_PER_SUB_PAGE;

		vp_t vp;
		pmt_fetch(lspn, &vp);
		
		// Read from flash to verify data
		if (vp.vpn) {
			UINT32 offset = lba % SECTORS_PER_PAGE; 
			UINT32 num_sectors = 1;

			/* uart_printf("i = %u, lba = %u, val = %u, offset = %u\r\n", */ 
	    			   /* buf_idx, lba, val, offset); */

			nand_page_ptread(vp.bank,
				         vp.vpn / PAGES_PER_VBLK,
				         vp.vpn % PAGES_PER_VBLK,
					 offset, num_sectors,
				         SATA_RD_BUF_PTR(g_ftl_read_buf_id),
					 RETURN_WHEN_DONE);
			BUG_ON("data read from **flash** is not as expected",
			 	is_buff_wrong(SATA_RD_BUF_PTR(g_ftl_read_buf_id), 
					     val, offset, num_sectors));
		}
		// Data is still in write buffer, so...
		else {
			UINT8  offset_in_sp = lba % SECTORS_PER_SUB_PAGE;
			UINT32 buff;
			write_buffer_get(lspn, offset_in_sp, 1, &buff);
			BUG_ON("should be in buffer, but actually not", buff == NULL);
			/* uart_printf("i = %u, lba = %u, val = %u\r\n", */ 
	    			   /* buf_idx, lba, val); */

			BUG_ON("data read from **buffer** is not as expected",
			 	is_buff_wrong(buff, val, 
					      offset_in_sp, 1));
		}
		
		buf_idx++;

		g_ftl_read_buf_id = (g_ftl_read_buf_id + 1) 
				  % NUM_SATA_RD_BUFFERS;
	}

	uart_print("Done ^_^");
}

static void test_write_in_sub_page()
{
	uart_print("Test writing in sub page...");

	srand(RAND_SEED);

	init_lspn_buf(0xFFFFFFFF);
	init_val_buf(0);

	uart_print("Write randomly...");
	UINT32 buf_size = 0;
	while (buf_size < BUF_SIZE) {
		UINT32 lpn 	= rand() % NUM_VPAGES;
		UINT32 sp_idx   = rand() % SUB_PAGES_PER_PAGE;
		UINT32 num_sps	= random(1, SUB_PAGES_PER_PAGE - sp_idx);
		
		UINT32 val 	= rand();		
		mem_set_dram(SATA_WR_BUF_PTR(g_ftl_write_buf_id) 
				+ sp_idx * BYTES_PER_SUB_PAGE,
			     val,
			     num_sps * BYTES_PER_SUB_PAGE);
		
		//uart_printf("buf_size = %u, lpn = %u, val = %u, sp_idx = %u, num_sps = %u\r\n", 
		//	   buf_size, lpn, val, sp_idx, num_sps);
	
		UINT32 lspn_base = lpn * SUB_PAGES_PER_PAGE,
		       lspn	 = lspn_base + sp_idx, 
		       lspn_end  = lspn_base + sp_idx + num_sps;
		while (lspn < lspn_end && buf_size < BUF_SIZE) {
			UINT32 old_idx = mem_search_equ_dram(LSPN_BUF,
						sizeof(UINT32), 
						BUF_SIZE,
						lspn);
			if (old_idx < BUF_SIZE) {
				set_val(old_idx, val);
			}
			else {
				set_lspn(buf_size, lspn);
				set_val(buf_size, val);
				buf_size++;
			}
			lspn++;
		}
		
		UINT8  offset 	   = sp_idx * SECTORS_PER_SUB_PAGE; 
		UINT8  num_sectors = num_sps * SECTORS_PER_SUB_PAGE; 
		write_buffer_put(lpn, offset, num_sectors, 
				 SATA_WR_BUF_PTR(g_ftl_write_buf_id));
		
		g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) 
				   % NUM_SATA_WR_BUFFERS;
	}

	uart_print("Verify the result by reading from flash "
		   "or checking buffer...");
	UINT32 buf_idx = 0;
	while (buf_idx < BUF_SIZE) {
		UINT32 lspn 	= get_lspn(buf_idx);
		UINT32 val	= get_val(buf_idx);

		vp_t vp;
		pmt_fetch(lspn, &vp);
		
		// Read from flash to verify data
		if (vp.vpn) {
			UINT32 offset = lspn % SUB_PAGES_PER_PAGE 
				      * SECTORS_PER_SUB_PAGE; 
			UINT32 num_sectors = SECTORS_PER_SUB_PAGE;

//			uart_printf("i = %u, lspn = %u, val = %u, offset = %u\r\n", 
//	    			   buf_idx, lspn, val, offset);

			nand_page_ptread(vp.bank,
				         vp.vpn / PAGES_PER_VBLK,
				         vp.vpn % PAGES_PER_VBLK,
					 offset, num_sectors,
				         SATA_RD_BUF_PTR(g_ftl_read_buf_id),
					 RETURN_WHEN_DONE);
			BUG_ON("data read from **flash** is not as expected",
			 	is_buff_wrong(SATA_RD_BUF_PTR(g_ftl_read_buf_id), 
					     val, offset, num_sectors));
		}
		// Data is still in write buffer, so...
		else {
			UINT32 buff;
			write_buffer_get(lspn, 0, SECTORS_PER_SUB_PAGE, &buff);

//			uart_printf("i = %u, lspn = %u, val = %u\r\n", 
//	    			   buf_idx, lspn, val);

			BUG_ON("data read from **buffer** is not as expected",
			 	is_buff_wrong(buff, val, 
					     0, SECTORS_PER_SUB_PAGE));
		}
		
		buf_idx++;

		g_ftl_read_buf_id = (g_ftl_read_buf_id + 1) 
				  % NUM_SATA_RD_BUFFERS;
	}

	uart_print("Done");
}

void ftl_test()
{
	uart_print("Start testing write buffer...");

	/* test_write_in_sub_page(); */
	uart_print("");
	test_write_in_sector();

	uart_print("Write buffer passed unit test ^_^");
}

#endif


