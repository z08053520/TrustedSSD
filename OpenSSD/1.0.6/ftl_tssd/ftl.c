#include "ftl.h"
#include "gtd.h"
#include "page_lock.h"
#include "dram.h"
#include "bad_blocks.h"
#include "gc.h"
#include "read_buffer.h"
#include "write_buffer.h"
#include "scheduler.h"
#include "ftl_thread.h"
#include "pmt_thread.h"
#include "sata_manager.h"
#if OPTION_ACL
	#include "acl.h"
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
	uart_print("# of SATA write buffers == %u",	NUM_SATA_WR_BUFFERS);
	PRINT_SIZE("SATA write buffers",		SATA_WR_BUF_BYTES);
	PRINT_SIZE("page",	BYTES_PER_PAGE);
	PRINT_SIZE("sub-page",	BYTES_PER_SUB_PAGE);
	uart_print("# of GTD entries = %u, size of GTD = %uKB, # of GTD pages = %u",
			GTD_ENTRIES, GTD_BYTES / 1024, GTD_PAGES);
	uart_print("# of PMT entries == %u, # of PMT sub-pages == %u",
			PMT_ENTRIES, PMT_SUB_PAGES);
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

	page_lock_init();
	bb_init();
	gc_init();

	read_buffer_init();
	write_buffer_init();

	/* Run PMT thread */
	thread_t* pmt_thread = thread_allocate();
	pmt_thread_init(pmt_thread);
	enqueue(pmt_thread);

	flash_clear_irq();
	// This example FTL can handle runtime bad block interrupts and read fail (uncorrectable bit errors) interrupts
	SETREG(INTR_MASK, FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);
	SETREG(FCONF_PAUSE, FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);

	enable_irq();

	uart_print("ftl_open done \r\n");
}


static CMD_T sata_cmd = {.lba = 0, .sector_count = 0, .cmd_type = 0};

BOOL8 ftl_all_sata_cmd_accepted()
{
	return sata_cmd.sector_count == 0 && !sata_has_next_rw_cmd();
}

/* Dummy FTL */
#if 0

UINT32 g_ftl_read_buf_id;
UINT32 g_ftl_write_buf_id;

void ftl_read(UINT32 const lba, UINT32 const total_sectors)
{
	UINT32 num_sectors_to_read;

	UINT32 lpage_addr		= lba / SECTORS_PER_PAGE;	// logical page address
	UINT32 sect_offset 		= lba % SECTORS_PER_PAGE;	// sector offset within the page
	UINT32 sectors_remain	= total_sectors;

	while (sectors_remain != 0)	// one page per iteration
	{
		if (sect_offset + sectors_remain < SECTORS_PER_PAGE)
		{
			num_sectors_to_read = sectors_remain;
		}
		else
		{
			num_sectors_to_read = SECTORS_PER_PAGE - sect_offset;
		}

		UINT32 next_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_SATA_RD_BUFFERS;

		while (next_read_buf_id == GETREG(SATA_RBUF_PTR));	// wait if the read buffer is full (slow host)

		SETREG(BM_STACK_RDSET, next_read_buf_id);	// change bm_read_limit
		SETREG(BM_STACK_RESET, 0x02);				// change bm_read_limit

		g_ftl_read_buf_id = next_read_buf_id;

		sect_offset = 0;
		sectors_remain -= num_sectors_to_read;
		lpage_addr++;
	}
}

void ftl_write(UINT32 const lba, UINT32 const total_sectors)
{
	UINT32 num_sectors_to_write;

	UINT32 sect_offset = lba % SECTORS_PER_PAGE;
	UINT32 remain_sectors = total_sectors;

	while (remain_sectors != 0)
	{
		if (sect_offset + remain_sectors >= SECTORS_PER_PAGE)
		{
			num_sectors_to_write = SECTORS_PER_PAGE - sect_offset;
		}
		else
		{
			num_sectors_to_write = remain_sectors;
		}

		while (g_ftl_write_buf_id == GETREG(SATA_WBUF_PTR));	// bm_write_limit should not outpace SATA_WBUF_PTR

		g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_SATA_WR_BUFFERS;		// Circular buffer

		SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);	// change bm_write_limit
		SETREG(BM_STACK_RESET, 0x01);				// change bm_write_limit

		sect_offset = 0;
		remain_sectors -= num_sectors_to_write;
	}
}

BOOL8 ftl_main(void)
{
	while (sata_has_next_rw_cmd()) {
		sata_get_next_rw_cmd(&sata_cmd);
		if (sata_cmd.cmd_type == READ)
			ftl_read(sata_cmd.lba, sata_cmd.sector_count);
		else
			ftl_write(sata_cmd.lba, sata_cmd.sector_count);
	}
	return TRUE;
}
#endif

BOOL8 ftl_main(void)
{
	while (thread_can_allocate(1)) {
		/* Make sure we have a SATA request to process */
		if (sata_cmd.sector_count == 0) {
			if (!sata_has_next_rw_cmd()) break;
			sata_get_next_rw_cmd(&sata_cmd);
			ASSERT(sata_cmd.sector_count > 0);

			/* uart_print("!!! %s cmd: lba = %u, sector_count = %u", */
			/* 	    sata_cmd.cmd_type == READ ? "READ" : "WRITE", */
			/* 	    sata_cmd.lba, sata_cmd.sector_count); */
		}


		/* Check whether SATA buffer is ready */
		if (sata_cmd.cmd_type == READ &&
			!sata_manager_can_accept_read_task()) break;
		if (sata_cmd.cmd_type == WRITE &&
			!sata_manager_can_accept_write_task()) break;

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
		thread_t *ftl_thread = thread_allocate();
		ASSERT(ftl_thread != NULL);
		ftl_cmd_t ftl_cmd = {
			.lpn = lpn,
			.sect_offset = offset,
			.num_sectors = num_sectors
#if OPTION_ACL
			,.uid = acl_skey2uid(sata_cmd.session_key)
#endif
		};
		if (sata_cmd.cmd_type == READ)
			ftl_read_thread_init(ftl_thread, &ftl_cmd);
		else
			ftl_write_thread_init(ftl_thread, &ftl_cmd);
		enqueue(ftl_thread);

		sata_cmd.lba += num_sectors;
		sata_cmd.sector_count -= num_sectors;
	}

	/* scheduler runs all threads enqueud */
	schedule();

	BOOL8 idle = sata_manager_are_all_tasks_finished()
			&& ftl_all_sata_cmd_accepted();
	return idle;
}

void ftl_flush(void) {
	INFO("ftl", "ftl_flush is called");
}

void ftl_isr(void) {
	uart_print("BSP interrupt occured...");

	// interrupt service routine for flash interrupts
	UINT32 bank;
	UINT32 bsp_intr_flag;

	for (bank = 0; bank < NUM_BANKS; bank++)
	{
		while (BSP_FSM(bank) != BANK_IDLE);

		bsp_intr_flag = BSP_INTR(bank);

		if (bsp_intr_flag == 0) continue;

		UINT32 fc = GETREG(BSP_CMD(bank));
		// BSP clear
		CLR_BSP_INTR(bank, bsp_intr_flag);

		if (bsp_intr_flag & FIRQ_DATA_CORRUPT)
		{
//			g_read_fail_count++;
			uart_print("warning: flash read failure");
		}

		if (bsp_intr_flag & (FIRQ_BADBLK_H | FIRQ_BADBLK_L))
		{
			if (fc == FC_COL_ROW_IN_PROG || fc == FC_IN_PROG || fc == FC_PROG)
			{
//				g_program_fail_count++;
				uart_print("warning: flash program failure");
			}
			else
			{
				ASSERT(fc == FC_ERASE);
//				g_erase_fail_count++;
				uart_print("warning: flash erase failure");
			}
		}
	}

	// clear the flash interrupt flag at the interrupt controller
	SETREG(APB_INT_STS, INTR_FLASH);
}
