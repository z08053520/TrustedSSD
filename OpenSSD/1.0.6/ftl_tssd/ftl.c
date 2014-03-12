#include "ftl.h"
#include "dram.h"
#include "bad_blocks.h"
#include "pmt.h"
#include "gc.h"
#include "page_cache.h"
#include "flash_util.h"
#include "read_buffer.h"
#include "write_buffer.h"
#include "thread.h"
#include "ftl_read_thread.h"
#include "ftl_write_thread.h"
#include "scheduler.h"
#if OPTION_ACL
	#include "acl.h"
#endif
#if OPTION_FDE
	#include "fde.h"
#endif

/* ========================================================================= *
 * Macros, Data Structure and Gloal Variables
 * ========================================================================= */

#define _KB 	1024
#define _MB 	(_KB * _KB)
#define PRINT_SIZE(name, size)	do {\
	if ((size) < _KB)\
		uart_print("Size of %s == %ubytes", (name), (size));\
	else if ((size) < _MB)\
		uart_print("Size of %s == %uKB", (name), (size) / _KB);\
	else\
		uart_print("Size of %s == %uMB", (name), (size) / _MB);\
} while(0);

/* ========================================================================= *
 * Private Functions
 * ========================================================================= */

static void sanity_check(void)
{
	BUG_ON("Address of SATA buffers must be a integer multiple of "
	       "SATA_BUF_PAGE_SIZE, which is set as BYTES_PER_PAGE when started",
			SATA_RD_BUF_ADDR   % BYTES_PER_PAGE != 0 ||
			SATA_WR_BUF_ADDR   % BYTES_PER_PAGE != 0 ||
			COPY_BUF_ADDR % BYTES_PER_PAGE != 0);
}

static void print_info(void)
{
	uart_print("TrustedSSD FTL");

	uart_print("=== Memory Configuration ===");
	uart_print("max LBA == %u", MAX_LBA);
	PRINT_SIZE("DRAM", 		DRAM_SIZE);
	PRINT_SIZE("page cache", 	PC_BYTES);
	PRINT_SIZE("bad block bitmap",	BAD_BLK_BMP_BYTES);
	PRINT_SIZE("non SATA buffer size",		NON_SATA_BUF_BYTES);
	uart_print("# of SATA read buffers == %u",	NUM_SATA_RD_BUFFERS);
	PRINT_SIZE("SATA read buffers", 		SATA_RD_BUF_BYTES);
	uart_print("# of SATA write buffers == %n",	NUM_SATA_WR_BUFFERS);
	PRINT_SIZE("SATA write buffers",		SATA_WR_BUF_BYTES);
	PRINT_SIZE("page",	BYTES_PER_PAGE);
	PRINT_SIZE("sub-page",	BYTES_PER_SUB_PAGE);
	uart_print("# of GTD entries = %u, size of GTD = %uKB, # of GTD pages = %u",
			GTD_ENTRIES, GTD_BYTES, GTD_PAGES);
	uart_print("# of PMT entries == %u, # of PMT pages == %u, # of PMT sub-pages == %u",
			PMT_ENTRIES, PMT_PAGES, PMT_SUB_PAGES);
	uart_print("");
}

/* ========================================================================= *
 * Public API
 * ========================================================================= */

void ftl_open(void) {
	print_info();

	led(0);
    	sanity_check();

	disable_irq();
	flash_clear_irq();

	/* the initialization order indicates the dependencies between modules */
	gtd_init();

	bb_init();
	gc_init();

	task_engine_init();

	page_cache_init();
	read_buffer_init();
	write_buffer_init();

	pmt_init();
#if OPTION_FDE
	fde_init();
#endif

	ftl_read_task_register();
	ftl_write_task_register();
	page_cache_load_task_register();
	page_cache_flush_task_register();

	flash_clear_irq();
	// This example FTL can handle runtime bad block interrupts and read fail (uncorrectable bit errors) interrupts
	SETREG(INTR_MASK, FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);
	SETREG(FCONF_PAUSE, FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);

	enable_irq();

	uart_print("ftl_open done\r\n");
}


static CMD_T sata_cmd = {.lba = 0, .sector_count = 0, .cmd_type = 0};

BOOL8 ftl_all_sata_cmd_accepted()
{
	return sata_cmd.sector_count == 0 && !sata_has_next_rw_cmd();
}

extern UINT32	g_num_ftl_read_tasks_submitted;
extern UINT32	g_num_ftl_write_tasks_submitted;

BOOL8 ftl_main(void)
{
	while (thread_can_allocate(1)) {
		/* Make sure we have a SATA request to process */
		if (sata_cmd.sector_count == 0) {
			if (!sata_has_next_rw_cmd()) break;
			sata_get_next_rw_cmd(&sata_cmd);

			/* uart_print("> new %s cmd: lba = %u, sector_count = %u", */
			/* 	    sata_cmd.cmd_type == READ ? "READ" : "WRITE", */
			/* 	    sata_cmd.lba, sata_cmd.sector_count); */
		}

#if OPTION_FTL_TEST == 0
		/* Check whether SATA buffer is ready */
		if (sata_cmd.cmd_type == READ) {
			UINT32	next_read_buf_id =
					(g_num_ftl_read_tasks_submitted + 1) %
					NUM_SATA_RD_BUFFERS;
			if (next_read_buf_id == GETREG(SATA_RBUF_PTR)) break;
		}
		else {
			UINT32	write_buf_id	=
					g_num_ftl_write_tasks_submitted %
					NUM_SATA_WR_BUFFERS;
			if (write_buf_id == GETREG(SATA_WBUF_PTR)) break;
		}
#endif

		/* Process one page at a time */
		UINT32 	lpn 	= sata_cmd.lba / SECTORS_PER_PAGE;
		UINT8	offset 	= sata_cmd.lba % SECTORS_PER_PAGE;
		UINT8	num_sectors =
				offset + sata_cmd.sector_count <= SECTORS_PER_PAGE ?
					sata_cmd.sector_count :
					SECTORS_PER_PAGE - offset;
		/* uart_printf("> %s one page: lpn = %u, offset = %u, num_sectors = %u\r\n", */
		/* 	   sata_cmd.cmd_type == READ ? "READ" : "WRITE", */
		/* 	   lpn, offset, num_sectors); */


		/* Submit a new task */
		thread_t *thread = thread_allocate();
		ASSERT(thread != NULL);
#if	OPTION_ACL
		UINT32	uid = acl_skey2uid(sata_cmd.session_key);
		if (sata_cmd.cmd_type == READ)
			ftl_read_thread_init(thread, uid, lpn, offset, num_sectors);
		else
			ftl_write_thread_init(thread, uid, lpn, offset, num_sectors);
#else
		if (sata_cmd.cmd_type == READ)
			ftl_read_thread_init(thread, lpn, offset, num_sectors);
		else
			ftl_write_thread_init(thread, lpn, offset, num_sectors);
#endif
		enqueue(thread);

		sata_cmd.lba += num_sectors;
		sata_cmd.sector_count -= num_sectors;
	}

	/* scheduler runs all threads enqueud */
	schedule();

	BOOL8 idle = ftl_all_sata_cmd_accepted();
	return idle;
}

void ftl_flush(void) {
	INFO("ftl", "ftl_flush is called");
}

void ftl_isr(void) {
	// interrupt service routine for flash interrupts
	UINT32 bank;
	UINT32 bsp_intr_flag;

	for (bank = 0; bank < NUM_BANKS; bank++)
	{
		while (BSP_FSM(bank) != BANK_IDLE);

		bsp_intr_flag = BSP_INTR(bank);

		if (bsp_intr_flag == 0)
		{
			continue;
		}

		UINT32 fc = GETREG(BSP_CMD(bank));

		CLR_BSP_INTR(bank, bsp_intr_flag);

		if (bsp_intr_flag & FIRQ_DATA_CORRUPT)
		{
//			g_read_fail_count++;
			WARNING("warning", "flash read failure");
		}

		if (bsp_intr_flag & (FIRQ_BADBLK_H | FIRQ_BADBLK_L))
		{
			if (fc == FC_COL_ROW_IN_PROG || fc == FC_IN_PROG || fc == FC_PROG)
			{
//				g_program_fail_count++;
				WARNING("warning", "flash program failure");
			}
			else
			{
				ASSERT(fc == FC_ERASE);
//				g_erase_fail_count++;
				WARNING("warning", "flash erase failure");
			}
		}
	}

	// clear the flash interrupt flag at the interrupt controller
	SETREG(APB_INT_STS, INTR_FLASH);
}
