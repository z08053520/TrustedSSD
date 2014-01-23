/* ===========================================================================
 * Unit test for write buffer 
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "dram.h"
#include "pmt.h"
#include "test_util.h"
#include "write_buffer.h"
#include "gc.h"
#include "flash_util.h"
#include <stdlib.h>

#define LBA_BUF		TEMP_BUF_ADDR
#define VAL_BUF		HIL_BUF_ADDR
#define BUF_SIZE	(BYTES_PER_PAGE / sizeof(UINT32))
/* #define BUF_SIZE	256 */
SETUP_BUF(val,		VAL_BUF,	SECTORS_PER_PAGE);
SETUP_BUF(lba,		LBA_BUF,	SECTORS_PER_PAGE);

#define RAND_SEED	1234567

static UINT32 sata_wr_buf_id = 0;
static UINT32 sata_rd_buf_id = 0;

static UINT32 flush_count = 0;

static void test_write_in_sector()
{
	uart_print("Test writing in sector...");

	srand(RAND_SEED);

	init_lba_buf(0xFFFFFFFF);
	init_val_buf(0);

	uart_print("Write randomly...");
	UINT32 buf_size = 0;
	while (buf_size < BUF_SIZE) {
		UINT32 lpn 	= rand() % NUM_VPAGES;
		UINT8  offset	= rand() % SECTORS_PER_PAGE;
		UINT8  num_sectors = random(1, SECTORS_PER_PAGE - offset);
		if (num_sectors > (BUF_SIZE - buf_size))
			num_sectors = BUF_SIZE - buf_size;

		UINT32 val 	= rand();		
		mem_set_dram(SATA_WR_BUF_PTR(sata_wr_buf_id) 
				+ offset * BYTES_PER_SECTOR,
			     val, num_sectors * BYTES_PER_SECTOR);
		
		/* uart_printf("buf_size = %u, lpn = %u, offset = %u, num_sectors = %u, val = %u\r\n", */ 
		/* 	    buf_size, lpn, offset, num_sectors, val); */
	
		UINT32 lba 	= lpn * SECTORS_PER_PAGE + offset,
		       lba_end 	= lba + num_sectors;
		while (lba < lba_end) {
			UINT32 old_idx = mem_search_equ_dram(
						LBA_BUF,
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

			BUG_ON("buf overflow!", buf_size > BUF_SIZE);
		}

		if (write_buffer_is_full()) {
			UINT8	bank = fu_get_idle_bank();
			UINT32	vpn  = gc_allocate_new_vpn(bank);
			vp_t	vp   = {.bank = bank, .vpn = vpn};
			
			UINT32	buf  = FTL_WR_BUF(bank);
			UINT32 	lspns[SUB_PAGES_PER_PAGE];
			sectors_mask_t buf_mask;
			mem_set_dram(buf, 0xFFFFFFFF, BYTES_PER_PAGE);
			write_buffer_flush(buf, lspns, &buf_mask);

			/* uart_printf("lspns = ["); */
			/* UINT8 i = 0; */
			/* for (i = 0; i < SUB_PAGES_PER_PAGE; i++) */
			/* 	uart_printf("%u ", lspns[i]); */
			/* uart_print("]"); */

			UINT8 sp_i;
			for (sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++) {
				if (lspns[sp_i] == NULL_LSPN) continue;

				vp_t old_vp;
				pmt_fetch(lspns[sp_i], &old_vp);
				if (old_vp.vpn != 0) {
					nand_page_ptread(old_vp.bank,
							 old_vp.vpn / PAGES_PER_VBLK,
							 old_vp.vpn % PAGES_PER_VBLK,
							 0, BYTES_PER_PAGE,
							 COPY_BUF(old_vp.bank),
							 RETURN_WHEN_DONE);

					sectors_mask_t copy_mask = 
						init_mask(sp_i * SECTORS_PER_SUB_PAGE,
						     	  SECTORS_PER_SUB_PAGE);
					copy_mask &= ~buf_mask;
					fu_copy_buffer(buf, COPY_BUF(old_vp.bank),
						       copy_mask);
				}

				pmt_update(lspns[sp_i], vp);
				/* uart_printf("pmt_update: lspn = %u, bank = %u, vpn = %u\r\n", */ 
				/* 	    lspns[sp_i], vp.bank, vp.vpn); */
			}
			/* uart_printf("fu_write_page: bank = %u, vpn = %u\r\n", */ 
			/* 	    vp.bank, vp.vpn); */
			fu_write_page(vp, buf);

			flush_count++;
			if (flush_count % NUM_BANKS == 0)
				flash_finish();
		}

		write_buffer_put(lpn, offset, num_sectors, 
				 SATA_WR_BUF_PTR(sata_wr_buf_id));
		
		sata_wr_buf_id = (sata_wr_buf_id + 1) 
				   % NUM_SATA_WR_BUFFERS;
	}
	flash_finish();

	uart_print("Verify the result by reading from flash "
		   "or checking buffer...");
	UINT32 buf_idx = 0;
	while (buf_idx < BUF_SIZE) {
		UINT32 	lba 	= get_lba(buf_idx);
		UINT32 	val	= get_val(buf_idx);

		UINT32 	lpn 	= lba / SECTORS_PER_PAGE; 	
		UINT32 	buf;
		sectors_mask_t mask;
		write_buffer_get(lpn, &buf, &mask);
		
		UINT32 	offset 	= lba % SECTORS_PER_PAGE; 

		// Data is in write buffer, then...
		if (buf && ((mask & (1ULL << offset)) != 0)) {
			/* uart_printf("[mem] i = %u, lpn = %u, lba = %u, val = %u\r\n", */ 
	    			   /* buf_idx, lpn, lba, val); */

			BUG_ON("data read from **buffer** is not as expected",
			 	is_buff_wrong(buf, val, offset, 1));
		}
		// Read from flash to verify data
		else {
			
			UINT32 	lspn	= lba / SECTORS_PER_SUB_PAGE;
			vp_t 	vp;
			pmt_fetch(lspn, &vp);
			
			/* uart_printf("[fla] i = %u, lpn = %u, lspn = %u, lba = %u, val = %u\r\n", */ 
	    			   /* buf_idx, lba / SECTORS_PER_PAGE, lspn, lba, val); */

			/* uart_printf("nand_page_ptread: bank = %u, vpn = %u, offset = %u\r\n", */
			/* 	    vp.bank, vp.vpn, offset); */

			BUG_ON("page should have been written in flash",
			       vp.vpn == 0);

			nand_page_ptread(vp.bank,
				         vp.vpn / PAGES_PER_VBLK,
				         vp.vpn % PAGES_PER_VBLK,
					 offset, 1,
				         SATA_RD_BUF_PTR(sata_rd_buf_id),
					 RETURN_WHEN_DONE);
			BUG_ON("data read from **flash** is not as expected",
			 	is_buff_wrong(SATA_RD_BUF_PTR(sata_rd_buf_id), 
					     val, offset, 1));
		}
		
		buf_idx++;

		sata_rd_buf_id = (sata_rd_buf_id + 1) 
				  % NUM_SATA_RD_BUFFERS;
	}

	uart_print("Done ^_^");
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


