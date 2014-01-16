/* ===========================================================================
 * Unit test for FTL read task 
 * =========================================================================*/
#include "jasmine.h"
#if OPTION_FTL_TEST
#include "dram.h"
#include "gc.h"
#include "pmt.h"
#include "write_buffer.h"
#include "test_util.h"
#include "flash_util.h"
#include "task_engine.h"
#include "ftl_read_task.h"
#include <stdlib.h>

extern UINT32 	g_num_ftl_read_tasks_submitted;

#define current_sata_buf_id	(g_num_ftl_read_tasks_submitted % NUM_SATA_RD_BUFFERS)

static void verify(UINT32 const lpn, 
		   UINT8  const offset, 
		   UINT8  const num_sectors, 
		   UINT32 *sector_vals)
{
	BUG_ON("excceed one page", offset + num_sectors > SECTORS_PER_PAGE);

	UINT32	sata_buf_id	= current_sata_buf_id;

	task_t	*task = task_allocate(); 
	ftl_read_task_init(task, lpn, offset, num_sectors);
	task_engine_submit(task);	

	BOOL8	idle;
	do {
		idle = task_engine_run();
	} while (!idle);

	UINT32	sata_buf	= SATA_RD_BUF_PTR(sata_buf_id);
	UINT8	sect_i, sect_end = offset + num_sectors;
	for (sect_i = offset; sect_i < sect_end; sect_i++) {
		BOOL8 wrong = is_buff_wrong(sata_buf, sector_vals[sect_i],
			     		    sect_i, 1);
		BUG_ON("Data in SATA read buffer is not as expected", wrong);
	}

	task_deallocate(task);
}

/* read a logical page that has never been written before */
static void read_non_existing_page()
{
	uart_printf("Test reading non-existing page...");

	UINT32 	lpn = 123456;
	UINT8	offset = 0, num_sectors = SECTORS_PER_PAGE;		
	UINT32	sector_vals[SECTORS_PER_PAGE];
	clear_vals(sector_vals, 0xFFFFFFFF);

	verify(lpn, offset, num_sectors, sector_vals);

	uart_print("Done");
}

/* read a logical page that is stored on one physical page */
static void read_one_to_one_page()
{
	uart_printf("Test reading one-to-one page...");

	UINT32	lpn = 123;
	UINT8	offset = 0, num_sectors = SECTORS_PER_PAGE;
	UINT32 	sector_vals[SECTORS_PER_PAGE];
	
	set_vals(sector_vals, 0, offset, num_sectors);
	fill_buffer(TEMP_BUF_ADDR, offset, num_sectors, sector_vals);

	UINT8	bank = 1;
	UINT32	vpn  = gc_allocate_new_vpn(bank);
	vp_t	vp   = {.bank = bank, .vpn = vpn};
	fu_write_page(vp, TEMP_BUF_ADDR);
	flash_finish();

	UINT32	lspn_base = lpn * SUB_PAGES_PER_PAGE;
	UINT8	sp_i;
	for (sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++) {
		UINT32 lspn = lspn_base + sp_i;
		pmt_update(lspn, vp);
	}

	verify(lpn, offset, num_sectors, sector_vals);
	
	uart_print("Done");
}

/* read a logical page that is stored on several physical pages */
static void read_one_to_many_page()
{
	uart_printf("Test reading one-to-many page...");

	UINT32	lpn = 1234;
	UINT8	offset = 0, num_sectors = SECTORS_PER_PAGE;
	UINT32 	sector_vals[SECTORS_PER_PAGE];

	/* every sub-page of this page is stored in different physical pages */
	UINT32	lspn_base = lpn * SUB_PAGES_PER_PAGE; 
	UINT8	sp_i;
	for (sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++) {
		UINT8	bank = sp_i;
		UINT32	vpn  = gc_allocate_new_vpn(bank);
		vp_t	vp   = {.bank = bank, .vpn = vpn};
	
		clear_vals(sector_vals, 0xFFFFFFFF);
		UINT8	offset = sp_i * SECTORS_PER_SUB_PAGE,
			num_sectors = SECTORS_PER_SUB_PAGE;
		set_vals(sector_vals, 0, offset, num_sectors);

		fill_buffer(COPY_BUF(bank), 0, SECTORS_PER_PAGE, sector_vals);

		fu_write_page(vp, COPY_BUF(bank));

		UINT32	lspn = lspn_base + sp_i;
		pmt_update(lspn, vp);
	}
	flash_finish();

	set_vals(sector_vals, 0, 0, SECTORS_PER_PAGE);
	verify(lpn, offset, num_sectors, sector_vals);
	
	uart_print("Done");
}

/* read a logical page that is in write buffer */
static void read_in_buffer_page()
{
	uart_printf("Test reading in-buffer page...");

	UINT32	lpn = 12345;
	UINT8	offset = 3, num_sectors = 21;
	UINT32 	sector_vals[SECTORS_PER_PAGE];

	clear_vals(sector_vals, 0);
	set_vals(sector_vals, 0, offset, num_sectors);
	fill_buffer(TEMP_BUF_ADDR, offset, num_sectors, sector_vals);

	write_buffer_put(lpn, offset, num_sectors, TEMP_BUF_ADDR);

	clear_vals(sector_vals, 0xFFFFFFFF);
	set_vals(sector_vals, 0, offset, num_sectors);
	verify(lpn, offset, num_sectors, sector_vals);

	uart_print("Done");
}

/* read a logical page that is in a combination of the above situations 
 *
 * 	Status of each sub-page
 * 		#0	never written
 * 		#1	in vp1
 * 		#2	in vp2
 * 		#3	in vp2 
 * 		#4	in write buffer entirely
 * 		#5	in write buffer partially
 * 		#6	first in vp1, then overwritten by write buffer  
 * 		#7	harf in vp2, then the other harf is overwritten by write buffer
 *
 * */
static void read_mixed_page()
{
	uart_printf("Test reading mixed page...");

	UINT32	lpn = 4321, lspn_base = lpn * SUB_PAGES_PER_PAGE;
	UINT8	offset, num_sectors;
	UINT32	sector_vals[SECTORS_PER_PAGE];
	UINT32	sector_vals_final[SECTORS_PER_PAGE];
	UINT32	buf;

	vp_t	vp;
	
	clear_vals(sector_vals_final, 0xFFFFFFFF);

	/* Write vp1 */	
	clear_vals(sector_vals, 1000);

	num_sectors = SECTORS_PER_SUB_PAGE;
	/* Set subpage #1 */
	offset = 1 * SECTORS_PER_SUB_PAGE;
	set_vals(sector_vals, 1000, offset, num_sectors);
	set_vals(sector_vals_final, 1000, offset, num_sectors);
	/* Set subpage #6 */
	offset = 6 * SECTORS_PER_SUB_PAGE;
	set_vals(sector_vals, 1000, offset, num_sectors);	
	/* Write */
	vp.bank = 5;
	vp.vpn  = gc_allocate_new_vpn(vp.bank);
	buf 	= SATA_WR_BUF_PTR(vp.bank);
	fill_buffer(buf, 0, SECTORS_PER_PAGE, sector_vals);

	fu_write_page(vp, buf);

	pmt_update(lspn_base + 1, vp);
	pmt_update(lspn_base + 6, vp);

	/* Write vp2 */
	clear_vals(sector_vals, 2000);
	/* Set subpage #2 and #3 */
	offset = 2 * SECTORS_PER_SUB_PAGE;
	num_sectors = 2 * SECTORS_PER_SUB_PAGE;
	set_vals(sector_vals, 2000, offset, num_sectors);
	set_vals(sector_vals_final, 2000, offset, num_sectors);
	/* Set subpage #7 */
	offset = 7 * SECTORS_PER_SUB_PAGE;
	num_sectors = SECTORS_PER_SUB_PAGE / 2;
	set_vals(sector_vals, 2000, offset, num_sectors);	
	set_vals(sector_vals_final, 2000, offset, num_sectors);
	/* Write */
	vp.bank = 6;
	vp.vpn  = gc_allocate_new_vpn(vp.bank);
	buf 	= SATA_WR_BUF_PTR(vp.bank);
	fill_buffer(buf, 0, SECTORS_PER_PAGE, sector_vals);

	fu_write_page(vp, buf);

	pmt_update(lspn_base + 2, vp);
	pmt_update(lspn_base + 3, vp);
	pmt_update(lspn_base + 7, vp);

	/* Write to write buffer */	
	buf = TEMP_BUF_ADDR;
	/* Set subpage #4 */
	offset = 4 * SECTORS_PER_SUB_PAGE;
	num_sectors = SECTORS_PER_SUB_PAGE;
	set_vals(sector_vals, 3000, offset, num_sectors);
	set_vals(sector_vals_final, 3000, offset, num_sectors);
	fill_buffer(buf, offset, num_sectors, sector_vals); 	
	write_buffer_put(lpn, offset, num_sectors, buf);
	/* Set subpage #5 */
	offset = 5 * SECTORS_PER_SUB_PAGE + 3;
	num_sectors = 3;
	set_vals(sector_vals, 3000, offset, num_sectors);	
	set_vals(sector_vals_final, 3000, offset, num_sectors);
	fill_buffer(buf, offset, num_sectors, sector_vals); 	
	write_buffer_put(lpn, offset, num_sectors, buf);
	/* Set subpage #6 */
	offset = 6 * SECTORS_PER_SUB_PAGE;
	num_sectors = SECTORS_PER_SUB_PAGE;
	set_vals(sector_vals, 3000, offset, num_sectors);
	set_vals(sector_vals_final, 3000, offset, num_sectors);
	fill_buffer(buf, offset, num_sectors, sector_vals); 	
	write_buffer_put(lpn, offset, num_sectors, buf);
	/* Set subpage #7 */
	offset = 7 * SECTORS_PER_SUB_PAGE + SECTORS_PER_SUB_PAGE / 2;
	num_sectors = SECTORS_PER_SUB_PAGE / 2;
	set_vals(sector_vals, 3000, offset, num_sectors);
	set_vals(sector_vals_final, 3000, offset, num_sectors);
	fill_buffer(buf, offset, num_sectors, sector_vals); 	
	write_buffer_put(lpn, offset, num_sectors, buf);

	flash_finish();

	verify(lpn, 0, SECTORS_PER_PAGE, sector_vals_final);

	uart_print("Done");
}

void ftl_test()
{
	uart_print("Start testing ftl_read_task...");

	read_non_existing_page();
	read_one_to_one_page();
	read_one_to_many_page();
	read_in_buffer_page();
	read_mixed_page();

	uart_print("ftl_read_task passed the unit test ^_^");
}

#endif
