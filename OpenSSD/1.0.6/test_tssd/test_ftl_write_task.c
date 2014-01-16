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
#include "ftl_task.h"
#include "ftl_write_task.h"
#include <stdlib.h>

extern UINT32 	g_num_ftl_write_tasks_submitted;
extern void 	ftl_write_task_force_flush();

#define current_sata_buf_id	(g_num_ftl_write_tasks_submitted % NUM_SATA_WR_BUFFERS)

static void 	write(UINT32 const lpn, UINT8 const offset, 
		      UINT8  const num_sectors, UINT32 *sector_vals)
{
	UINT32	buf_id	= current_sata_buf_id;
	UINT32	wr_buf	= SATA_WR_BUF_PTR(buf_id);
	mem_set_dram(wr_buf, 0, BYTES_PER_PAGE);	
	fill_buffer(wr_buf, offset, num_sectors, sector_vals);

	while(!task_can_allocate(1)) task_engine_run();

	task_t	*task = task_allocate(); 
	ftl_write_task_init(task, lpn, offset, num_sectors);
	task_engine_submit(task);
}

static void 	verify(UINT32 const lpn, UINT8 const offset, 
		       UINT8  const num_sectors, UINT32 *sector_vals)
{
	UINT32	rd_buf	 = TEMP_BUF_ADDR;
	UINT32	lspn0	 = lpn2lspn(lpn);

	UINT8	sp_begin = begin_subpage(offset),
		sp_end	 = end_subpage(offset + num_sectors);
	UINT8	sp_i;
	for (sp_i = sp_begin; sp_i < sp_end; sp_i++) {
		vp_t	vp;
		pmt_fetch(lspn0 + sp_i, &vp);
		BUG_ON("not written to flash yet!", vp.vpn == 0);

		vsp_t	vsp = {
			.bank = vp.bank, .vspn = vpn2vspn(vp.vpn) + sp_i
		};
		fu_read_sub_page(vsp, rd_buf, SYNC);
	}

	UINT8	sect_i, sect_end = offset + num_sectors;
	for (sect_i = offset; sect_i < sect_end; sect_i++) {
		BOOL8	wrong = is_buff_wrong(rd_buf, sectors_vals[sect_i], 
					      sect_i, 1);
		BUG_ON("data written to flash is not as expected", wrong);
	}
}

static void	write_whole_page()
{
	uart_printf("Test writing a 32KB page...");

	UINT32 	lpn = 1000;
	UINT8	offset = 0, num_sectors = SECTORS_PER_PAGE;		
	UINT32	sector_vals[SECTORS_PER_PAGE];
	set_vals(sector_vals, 1000, offset, num_sectors);

	write(lpn, offset, num_sectors, sector_vals);
	ftl_write_task_force_flush();
	verify(lpn, offset, num_sectors, sector_vals);

	uart_print("Done");
}

static void	write_partial_page()
{
	uart_printf("Test writing a 8KB page that spans across three sub-pages...");

	UINT32 	lpn = 2000;
	UINT8	offset = 3, num_sectors = 2 * SECTORS_PER_SUB_PAGE;		
	UINT32	sector_vals[SECTORS_PER_PAGE];
	clear_vals(sector_vals, 0xFFFFFFFF);
	set_vals(sector_vals, 2000, offset, num_sectors);

	write(lpn, offset, num_sectors, sector_vals);
	ftl_write_task_force_flush();
	verify(lpn, offset, num_sectors, sector_vals);

	uart_print("Done");
}

static void write_whole_page_then_partial_page()
{
	uart_printf("Test writing a 32KB page first, then overwrite it with a 31KB page...");

	UINT32 	lpn = 3000;
	UINT8	offset, num_sectors;
	UINT32	sector_vals[SECTORS_PER_PAGE];

	offset = 0, num_sectors = SECTORS_PER_PAGE;
	set_vals(sectors_vals, 3000, offset, num_sectors);
	write(lpn, offset, num_sectors, sector_vals);

	offset = 1, num_sectors = 62;		
	set_vals(sector_vals, 4000, offset, num_sectors);
	write(lpn, offset, num_sectors, sector_vals);

	ftl_write_task_force_flush();
	verify(lpn, 0, SECTORS_PER_PAGE, sector_vals);

	uart_print("Done");
}

static void write_partial_page_then_whole_page()
{
	uart_printf("Test writing a 4KB page first, then overwrite it with a 32KB page...");

	UINT32 	lpn = 5000;
	UINT8	offset, num_sectors;
	UINT32	sector_vals[SECTORS_PER_PAGE];

	offset = 1, num_sectors = 8;		
	clear_vals(sector_vals, 0xFFFFFFFF);
	set_vals(sector_vals, 5000, offset, num_sectors);
	write(lpn, offset, num_sectors, sector_vals);

	offset = 0, num_sectors = SECTORS_PER_PAGE;
	set_vals(sectors_vals, 6000, offset, num_sectors);
	write(lpn, offset, num_sectors, sector_vals);

	ftl_write_task_force_flush();
	verify(lpn, 0, SECTORS_PER_PAGE, sector_vals);

	uart_print("Done");
}

void ftl_test()
{
	uart_print("Start testing ftl_write_task...");

	write_whole_page();
	write_partial_page();
	write_whole_page_then_partial_page();
	write_partial_page_then_whole_page();

	uart_print("ftl_write_task passed the unit test ^_^");
}

#endif
