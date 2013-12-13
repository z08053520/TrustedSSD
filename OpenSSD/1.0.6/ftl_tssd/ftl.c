#include "ftl.h"
#include "cmt.h"
#include "gtd.h"
#include "pmt.h"
#include "gc.h"
#include "buffer_cache.h"

/* ========================================================================= *
 * Macros, Data Structure and Gloal Variables 
 * ========================================================================= */

UINT32 g_ftl_read_buf_id;
UINT32 g_ftl_write_buf_id;

typedef struct {

} ftl_metadata;

#define _KB 	1024
#define _MB 	(_KB * _KB)
#define PRINT_SIZE(name, size)	do {\
	if (size < _KB)\
		uart_printf("Size of %s == %dbytes\r\n", name, size);\
	else if (size < _MB)\
		uart_printf("Size of %s == %dKB\r\n", name, size / _KB);\
	else\
		uart_printf("Size of %s == %dMB\r\n", name, size / _MB);\
} while(0);

/* ========================================================================= *
 * Private Functions 
 * ========================================================================= */

static void sanity_check(void)
{
	UINT32 dram_requirement = RD_BUF_BYTES + WR_BUF_BYTES + COPY_BUF_BYTES
		+ FTL_BUF_BYTES + HIL_BUF_BYTES + TEMP_BUF_BYTES 
		+ BAD_BLK_BMP_BYTES + BC_BYTES;

	BUG_ON("DRAM is over-utilized", dram_requirement >= DRAM_SIZE);
	BUG_ON("ftl_metadata is too larget", sizeof(ftl_metadata) > BYTES_PER_PAGE);
	BUG_ON("Address of SATA buffers must be a integer multiple of " 
	       "SATA_BUF_PAGE_SIZE, which is set as BYTES_PER_PAGE when started", 
			RD_BUF_ADDR   % BYTES_PER_PAGE != 0 || 
			WR_BUF_ADDR   % BYTES_PER_PAGE != 0 || 
			COPY_BUF_ADDR % BYTES_PER_PAGE != 0);
}

/* this private function is used by unit test so it is not declared as static */
UINT32 get_vpn(UINT32 const lpn)
{
	UINT32 vpn;
	UINT32 victim_lpn, victim_vpn; 
	BOOL32 victim_dirty;
  
	// if lpn is not cached, the new entry has to be loaded
	if (cmt_get(lpn, &vpn)) {	
		// and if cache is full, a entry has to be evicted
		if (cmt_is_full()) {	
			cmt_evict(&victim_lpn, &victim_vpn, &victim_dirty);

			// need to write back
			if (victim_dirty)
				pmt_update(victim_lpn, victim_vpn);
		}
		pmt_fetch(lpn, &vpn);
		cmt_add(lpn, vpn);
	}
	return vpn;
}

static void read_page  (UINT32 const lpn, 
			UINT32 const sect_offset, 
			UINT32 const num_sectors_to_read)
{
    	UINT32 bank, vpn;
	UINT32 page_buff;
	UINT32 next_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_RD_BUFFERS;

	INFO("ftl>read>input", "read %d sectors at logical page %d with offset %d", 
			num_sectors_to_read, lpn, sect_offset);
  
	// try to read from cache
	bc_get(lpn, &page_buff, BC_BUF_TYPE_USR);
	if (page_buff) {
		INFO("ftl>read>logic", "read from buff cache %d", BC_BUF_IDX(page_buff));

		// wait for the next buffer to get SATA transfer done
		#if OPTION_FTL_TEST == 0
		while (next_read_buf_id == GETREG(SATA_RBUF_PTR));
		#endif

		bc_fill(lpn, sect_offset, num_sectors_to_read, BC_BUF_TYPE_USR);
		INFO("ftl>read_page", 
	     	     "page_buff = %d, sect_offset = %d, num_sectors_to_read = %d", 
	             page_buff, sect_offset, num_sectors_to_read);
		mem_copy(RD_BUF_PTR(g_ftl_read_buf_id) + BYTES_PER_SECTOR * sect_offset,
			 page_buff + BYTES_PER_SECTOR * sect_offset,
			 BYTES_PER_SECTOR * num_sectors_to_read);
		goto mem_xfer_done;
	}
	
	vpn  = get_vpn(lpn);
			
	// read from flash
	if (vpn != NULL)
	{
		bank = lpn2bank(lpn);
		INFO("ftl>read>logic", "read from flash, bank = %d, vpn = %d", bank, vpn);
		nand_page_ptread_to_host(bank,
					 vpn / PAGES_PER_BLK,
					 vpn % PAGES_PER_BLK,
					 sect_offset,
					 num_sectors_to_read);
		return;
	}
	// the logical page has never been written to 
	else
	{
		// wait for the next buffer to get SATA transfer done
		#if OPTION_FTL_TEST == 0
		while (next_read_buf_id == GETREG(SATA_RBUF_PTR));
		#endif

		INFO("ftl>read>logic", "read non-existing page");
            	// Send 0xFF...FF to host when the host request to read the 
		// sector that has never been written.
		mem_set_dram(RD_BUF_PTR(g_ftl_read_buf_id) 
				+ sect_offset * BYTES_PER_SECTOR,
				0xFFFFFFFF, 
				num_sectors_to_read * BYTES_PER_SECTOR);
	}

mem_xfer_done:
	// wait for flash finish to avoid race condition when updating
	// BM_STACK_LIMIT
	flash_finish();

	// read buffer is ready for SATA transfer
	SETREG(BM_STACK_RDSET, next_read_buf_id);
	SETREG(BM_STACK_RESET, 0x02);

	g_ftl_read_buf_id = next_read_buf_id;
}

static void write_page (UINT32 const lpn, 
			UINT32 const sect_offset, 
			UINT32 const num_sectors_to_write)
{
	UINT32 buff_addr;
	UINT32 target_addr, src_addr, num_bytes; 

	INFO("ftl>write>input", "write %d sectors at logical page %d with offset %d", 
			num_sectors_to_write, lpn, sect_offset);

	bc_get(lpn, &buff_addr, BC_BUF_TYPE_USR);
	if (buff_addr == NULL) {
		// make sure lpn->vpn mapping is in CMT
		// this is a requirement of putting a lpn into buffer cache 
		get_vpn(lpn);
		bc_put(lpn, &buff_addr, BC_BUF_TYPE_USR);

		INFO("ftl>read>logic", "cache miss");
		/* init buffer */ 
		/* this init code should not be necessary
		target_addr = buff_addr;
		num_bytes   = sect_offset * BYTES_PER_SECTOR;
		mem_set_dram(target_addr, 0xFFFFFFFF, num_bytes);

		target_addr = buff_addr + (sect_offset + 
				num_sectors_to_write) * BYTES_PER_SECTOR;
		num_bytes   = (SECTORS_PER_PAGE - sect_offset - 
				num_sectors_to_write) * BYTES_PER_SECTOR;
		mem_set_dram(target_addr, 0xFFFFFFFF, num_bytes);
		*/
	}
	else
		INFO("ftl>read>logic", "cache hit");

	INFO("ftl>read>logic", "write to buff cache %d", BC_BUF_IDX(buff_addr));

	// wait for SATA transfer completion
	#if OPTION_FTL_TEST == 0
	while (g_ftl_write_buf_id == GETREG(SATA_WBUF_PTR));
	#endif

	// copy from SATA circular buffer to buffer cache 
	target_addr = buff_addr + sect_offset * BYTES_PER_SECTOR;
	src_addr    = WR_BUF_PTR(g_ftl_write_buf_id) + sect_offset * BYTES_PER_SECTOR;
	num_bytes   = num_sectors_to_write * BYTES_PER_SECTOR;

	INFO("ftl>write_page", 
	     "targe_addr = %d, src_addr = %d, sect_offset = %d, num_sectors_to_write = %d, num_bytes = %d", 
	     target_addr, src_addr, sect_offset, num_sectors_to_write, num_bytes);
	mem_copy(target_addr, src_addr, num_bytes);

	bc_set_valid_sectors(lpn, sect_offset, num_sectors_to_write, BC_BUF_TYPE_USR);
	bc_set_dirty(lpn, BC_BUF_TYPE_USR);

	g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;	

	SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);
	SETREG(BM_STACK_RESET, 0x01);
}

static void print_info(void)
{
	uart_printf("TrustedSSD FTL (compiled at %s %s)\r\n", __TIME__, __DATE__);

	uart_print("=== Memory Configuration ===");
	PRINT_SIZE("DRAM", 	DRAM_SIZE);
	uart_printf("# of cache buffers == %d\r\n", NUM_BC_BUFFERS);	
	PRINT_SIZE("cache", 	BC_BYTES);
	PRINT_SIZE("bad block bitmap",	BAD_BLK_BMP_BYTES); 
	PRINT_SIZE("non R/W buffers", 	NON_RW_BUF_BYTES);
	uart_printf("# of read buffers == %d\r\n", NUM_RD_BUFFERS);
	PRINT_SIZE("read buffers", 	RD_BUF_BYTES);
	uart_printf("# of write buffers == %d\r\n", NUM_WR_BUFFERS);
	PRINT_SIZE("write buffers", 	WR_BUF_BYTES);
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
	bb_init();
	cmt_init();
	gtd_init();

	gc_init();

	bc_init();

	pmt_init();

	g_ftl_read_buf_id = g_ftl_write_buf_id = 0;

	flash_clear_irq();
	// This example FTL can handle runtime bad block interrupts and read fail (uncorrectable bit errors) interrupts
	SETREG(INTR_MASK, FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);
	SETREG(FCONF_PAUSE, FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);

	enable_irq();

	uart_print("ftl_open done\r\n");
}

void ftl_read(UINT32 const lba, UINT32 const num_sectors) 
{
	UINT32 remain_sects, num_sectors_to_read;
    	UINT32 lpn, sect_offset;

    	lpn          = lba / SECTORS_PER_PAGE;
    	sect_offset  = lba % SECTORS_PER_PAGE;
    	remain_sects = num_sectors;

/*  	uart_printf("ftl read: g_ftl_read_buf_id=%d, SATA_RBUF_PTR=%d, BM_READ_LIMIT=%d\r\n", 
			g_ftl_read_buf_id, GETREG(SATA_RBUF_PTR), GETREG(BM_READ_LIMIT)); */

	while (remain_sects != 0)
	{
        	if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
            		num_sectors_to_read = remain_sects;
        	else
            		num_sectors_to_read = SECTORS_PER_PAGE - sect_offset;

		read_page(lpn, sect_offset, num_sectors_to_read);

		sect_offset   = 0;
		remain_sects -= num_sectors_to_read;
		lpn++;
    	}
}

void ftl_write(UINT32 const lba, UINT32 const num_sectors) 
{
	UINT32 remain_sects, num_sectors_to_write;
    	UINT32 lpn, sect_offset;

	lpn          = lba / SECTORS_PER_PAGE;
	sect_offset  = lba % SECTORS_PER_PAGE;
 	remain_sects = num_sectors;

	/*  uart_printf("ftl write: g_ftl_write_buf_id=%d, SATA_WBUF_PTR=%d, BM_WRITE_LIMIT=%d\r\n", 
			g_ftl_write_buf_id, GETREG(SATA_WBUF_PTR), GETREG(BM_WRITE_LIMIT));*/

    	while (remain_sects != 0)
    	{
		if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
			num_sectors_to_write = remain_sects;
		else
			num_sectors_to_write = SECTORS_PER_PAGE - sect_offset;
		
		write_page(lpn, sect_offset, num_sectors_to_write);

		sect_offset   = 0;
		remain_sects -= num_sectors_to_write;
		lpn++;
	}
}

void ftl_test_write(UINT32 const lba, UINT32 const num_sectors) {

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
