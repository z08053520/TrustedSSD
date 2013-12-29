#include "ftl.h"
#include "dram.h"
#include "bad_blocks.h"
#include "pmt.h"
#include "gc.h"
#include "page_cache.h"
#include "flash_util.h"
#include "write_buffer.h"
#include "read_buffer.h"
#if OPTION_ACL
#include "acl.h"
#endif

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
	UINT32 dram_requirement = SATA_RD_BUF_BYTES + SATA_WR_BUF_BYTES + COPY_BUF_BYTES
		+ FTL_BUF_BYTES + HIL_BUF_BYTES + TEMP_BUF_BYTES 
		+ BAD_BLK_BMP_BYTES + BC_BYTES;

	BUG_ON("DRAM is over-utilized", dram_requirement >= DRAM_SIZE);
	BUG_ON("ftl_metadata is too larget", sizeof(ftl_metadata) > BYTES_PER_PAGE);
	BUG_ON("Address of SATA buffers must be a integer multiple of " 
	       "SATA_BUF_PAGE_SIZE, which is set as BYTES_PER_PAGE when started", 
			SATA_RD_BUF_ADDR   % BYTES_PER_PAGE != 0 || 
			SATA_WR_BUF_ADDR   % BYTES_PER_PAGE != 0 || 
			COPY_BUF_ADDR % BYTES_PER_PAGE != 0);
}

/* this private function is used by unit test so it is not declared as static */
vp_t get_vp(UINT32 const lspn)
{
	vp_t vp;	
	pmt_fetch(lspn, &vp);	
	return vp;
}

static void mem_read_done(UINT32 const next_read_buf_id)
{
	// wait for flash finish to avoid race condition when updating
	// BM_READ_LIMIT
	flash_finish();

	// read buffer is ready for SATA transfer
	SETREG(BM_STACK_RDSET, next_read_buf_id);
	SETREG(BM_STACK_RESET, 0x02);

	g_ftl_read_buf_id = next_read_buf_id;
}

/*
static void read_empty_page (UINT32 const sect_offset, 
			     UINT32 const num_sectors_to_read)
{
	UINT32 next_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_SATA_RD_BUFFERS;

	// wait for the next buffer to get SATA transfer done
	#if OPTION_FTL_TEST == 0
	while (next_read_buf_id == GETREG(SATA_RBUF_PTR));
	#endif

	// Send 0xFF...FF to host when the host request to read the 
	// sector that has never been written.
	mem_set_dram(SATA_RD_BUF_PTR(g_ftl_read_buf_id) 
			+ sect_offset * BYTES_PER_SECTOR,
			0xFFFFFFFF, 
			num_sectors_to_read * BYTES_PER_SECTOR);

	mem_read_done(next_read_buf_id);
}

static BOOL8 try_read_from_cache (UINT32 const lpn,
				  UINT32 const sect_offset,
				  UINT32 const num_sectors_to_read)
{
	UINT32 next_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_SATA_RD_BUFFERS;
	UINT32 page_buff = NULL;
	UINT32 vpn;
	
	// is page in cache?
	bc_get(lpn, &page_buff, BC_BUF_TYPE_USR);

	if (page_buff) {
		bc_fill(lpn, sect_offset, num_sectors_to_read, BC_BUF_TYPE_USR);
	}
	else { 
		if (num_sectors_to_read > SECTORS_PER_PAGE / 2 || 
	    		(cmt_get(lpn, &vpn) != 0 && cmt_get(lpn-1, &vpn) != 0))
			return FALSE;

		// heuristic to optimize small reads: 
		// load uncached page into cache if its previous page or itself was 
		// visited lately
		get_vpn(lpn);
		bc_put(lpn, &page_buff, BC_BUF_TYPE_USR);
		bc_fill_full_page(lpn, BC_BUF_TYPE_USR);
	}
	
	// wait for the next buffer to get SATA transfer done
	#if OPTION_FTL_TEST == 0
	while (next_read_buf_id == GETREG(SATA_RBUF_PTR));
	#endif

	mem_copy(SATA_RD_BUF_PTR(g_ftl_read_buf_id) + BYTES_PER_SECTOR * sect_offset,
		 page_buff + BYTES_PER_SECTOR * sect_offset,
		 BYTES_PER_SECTOR * num_sectors_to_read);
	
	mem_read_done(next_read_buf_id);
	return TRUE;
}
*/

/* segment is contiguous sub pages that are stored in the same page */
typedef struct _segment {
	vp_t   vp;
	UINT8  sector_offset;
	UINT8  num_sectors;
} segment_t;

/* align LBA to the boundary of sub pages */
#define align_lba_to_sp(lba) (lba / SECTORS_PER_SUB_PAGE * SECTORS_PER_SUB_PAGE)

static void read_page(UINT32 const lpn, 
		      UINT32 const sector_offset, 
		      UINT32 const num_sectors_to_read)
{	
	UINT32 next_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_SATA_RD_BUFFERS;

	segment_t segment[SUB_PAGES_PER_PAGE];
	UINT8   num_segments 	= 0;

	#if OPTION_FTL_TEST == 0
	while (next_read_buf_id == GETREG(SATA_RBUF_PTR));
	#endif

	UINT32	buff;
	UINT32 	lspn 		= lpn * SUB_PAGES_PER_PAGE + sector_offset / SECTORS_PER_SUB_PAGE;
	UINT32	sectors_remain	= num_sectors_to_read;
	UINT32  sector_i  	= sector_offset;
	UINT32  offset_in_sp, num_sectors_in_sp;
	vp_t	vp;
	// iterate sub pages
	while (sectors_remain) {
		// alignment to sub pages
		offset_in_sp = (sector_i % SECTORS_PER_SUB_PAGE);
		if (offset_in_sp + sectors_remain < SECTORS_PER_SUB_PAGE)
			num_sectors_in_sp = sectors_remain;
		else
			num_sectors_in_sp = SECTORS_PER_SUB_PAGE - offset_in_sp;

		// try to read from write buffer first
		write_buffer_get(lspn, offset_in_sp, num_sectors_in_sp, &buff);
		if (buff) {
			mem_copy(SATA_RD_BUF_PTR(g_ftl_read_buf_id) 
					+ sector_i * BYTES_PER_SECTOR, 
				 buff + offset_in_sp * BYTES_PER_SECTOR, 
				 num_sectors_in_sp * BYTES_PER_SECTOR);
			goto next_sub_page;
		}

		vp = get_vp(lspn);
		// read buffer can handle vpn == 0 case
		read_buffer_get(vp, &buff);
		if (buff) {
			// TODO: we can make this more efficient by merge 
			// memory copies from the same buffer
			mem_copy(SATA_RD_BUF_PTR(g_ftl_read_buf_id) 
					+ sector_i * BYTES_PER_SECTOR,
				 buff + sector_i * BYTES_PER_SECTOR,
				 num_sectors_in_sp * BYTES_PER_SECTOR);
			goto next_sub_page;;
		}

		if (num_segments == 0 || 
		    vp_not_equal(segment[num_segments-1].vp, vp)) {
			segment[num_segments].vp = vp;
			segment[num_segments].sector_offset = sector_i;
			segment[num_segments].num_sectors = 0;
			num_segments++;
		}
		segment[num_segments-1].num_sectors += num_sectors_in_sp;

next_sub_page:
		lspn ++;
		sector_i += num_sectors_in_sp;
		sectors_remain -= num_sectors_in_sp;
	}

	// if no need to do any flash read
	if (num_segments == 0) {
		mem_read_done(next_read_buf_id);
		return;
	}

	segment_t* seg;
	if (num_segments > 1) {
		buff = SATA_RD_BUF_PTR(g_ftl_read_buf_id);

		UINT8 segment_i;
		for (segment_i = 0; segment_i < num_segments - 1; segment_i++) {
			seg = & segment[segment_i];
			vp  = seg->vp;
			nand_page_ptread(vp.bank,
					 vp.vpn / PAGES_PER_VBLK,
					 vp.vpn % PAGES_PER_VBLK,
					 seg->sector_offset,
					 seg->num_sectors,
					 buff,
					 RETURN_ON_ISSUE);
		}
		flash_finish();
	}

	seg = & segment[num_segments-1];
	vp  = seg->vp;
	nand_page_ptread_to_host(vp.bank,
				 vp.vpn / PAGES_PER_VBLK,
				 vp.vpn % PAGES_PER_VBLK,
				 seg->sector_offset,
				 seg->num_sectors);
}
/* 
static void read_page  (UINT32 const lpn, 
			UINT32 const sect_offset, 
			UINT32 const num_sectors_to_read)
{
    	UINT32 bank, vpn;

	INFO("ftl>read>input", "read %d sectors at logical page %d with offset %d", 
			num_sectors_to_read, lpn, sect_offset);

	// if page in cache
	if (try_read_from_cache(lpn, sect_offset, num_sectors_to_read))
		return;
	
	vpn  = get_vpn(lpn);
	// if page is empty
	if (vpn == NULL)  {
		read_empty_page(sect_offset, num_sectors_to_read);
		return;
	}

	// if page is in flash
	bank = lpn2bank(lpn);
	nand_page_ptread_to_host(bank,
				 vpn / PAGES_PER_BLK,
				 vpn % PAGES_PER_BLK,
				 sect_offset,
				 num_sectors_to_read);
}

static void write_full_page_to_flash (UINT32 const lpn)
{
	UINT32 bank 	= lpn2bank(lpn);
	UINT32 old_vpn  = get_vpn(lpn);
	UINT32 new_vpn  = old_vpn ? gc_replace_old_vpn(bank, old_vpn) :
				    gc_allocate_new_vpn(bank) ;

	// FIXME: this waiting may not be necessary
	// wait for SATA transfer completion
	#if OPTION_FTL_TEST == 0
	while (g_ftl_write_buf_id == GETREG(SATA_WBUF_PTR));
	#endif

	nand_page_program_from_host(bank,
                                    new_vpn / PAGES_PER_VBLK,
                                    new_vpn % PAGES_PER_VBLK);
	
	// Update lpn->vpn in CMT
	cmt_update(lpn, new_vpn);
}
*/
static UINT32 write_count =0;
static void write_page(UINT32 const lpn, 
		       UINT32 const sect_offset, 
		       UINT32 const num_sectors_to_write)
{
	// FIXME: this waiting may not be necessary
	// Wait for SATA transfer completion
	#if OPTION_FTL_TEST == 0
	while (g_ftl_write_buf_id == GETREG(SATA_WBUF_PTR));
	#endif
	
	// Write full page to flash directly
	if (num_sectors_to_write == SECTORS_PER_PAGE) {
		write_buffer_drop(lpn);

		UINT8 bank = fu_get_idle_bank();
		UINT32 vpn = gc_allocate_new_vpn(bank);
		nand_page_program_from_host(bank, 
				  	    vpn / PAGES_PER_VBLK, 
				  	    vpn % PAGES_PER_VBLK);

		vp_t   vp = {.bank = bank, .vpn = vpn};
		UINT32 base_lspn = lpn * SUB_PAGES_PER_PAGE;
		UINT8 sp;
		for (sp = 0; sp < SUB_PAGES_PER_PAGE; sp++)
			pmt_update(base_lspn + sp, vp);
		return;
	}


	// Put partial page to write buffer to merge before writing back
	write_buffer_put(lpn, sect_offset, num_sectors_to_write, 
			 SATA_WR_BUF_PTR(g_ftl_write_buf_id));

	// Update SATA write buffer pointer
	g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_SATA_WR_BUFFERS;

	// FIXME: add timeout
#warning slow!
//	write_count++;
//	if (write_count % NUM_BANKS == 0) {
		// Wait for flash finish to avoid race condition when updating
		// BM_WRITE_LIMIT
		flash_finish();
		SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);
		SETREG(BM_STACK_RESET, 0x01);
//	}
}

/*  
static void write_page(UINT32 const lpn, 
		       UINT32 const sect_offset, 
		       UINT32 const num_sectors_to_write)
{
	UINT32 buff_addr;
	UINT32 target_addr, src_addr, num_bytes; 

	INFO("ftl>write>input", "write %d sectors at logical page %d with offset %d", 
			num_sectors_to_write, lpn, sect_offset);

	bc_get(lpn, &buff_addr, BC_BUF_TYPE_USR);
	if (buff_addr == NULL) {
		// Optimization for large sequential writes
  		if (num_sectors_to_write == SECTORS_PER_PAGE) {
			write_full_page_to_flash(lpn);
			return;
		}

		// make sure lpn->vpn mapping is in CMT
		// this is a requirement of putting a lpn into buffer cache 
		get_vpn(lpn);
		bc_put(lpn, &buff_addr, BC_BUF_TYPE_USR);
	}

	// FIXME: this waiting may not be necessary
	// wait for SATA transfer completion
	#if OPTION_FTL_TEST == 0
	while (g_ftl_write_buf_id == GETREG(SATA_WBUF_PTR));
	#endif

	// copy from SATA circular buffer to buffer cache 
	target_addr = buff_addr + sect_offset * BYTES_PER_SECTOR;
	src_addr    = SATA_WR_BUF_PTR(g_ftl_write_buf_id) + sect_offset * BYTES_PER_SECTOR;
	num_bytes   = num_sectors_to_write * BYTES_PER_SECTOR;

	mem_copy(target_addr, src_addr, num_bytes);

	bc_set_valid_sectors(lpn, sect_offset, num_sectors_to_write, BC_BUF_TYPE_USR);
	bc_set_dirty(lpn, BC_BUF_TYPE_USR);

	g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_SATA_WR_BUFFERS;	

	// wait for flash finish to avoid race condition when updating
	// BM_WRITE_LIMIT
	flash_finish();

	SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);
	SETREG(BM_STACK_RESET, 0x01);
}
*/
static void print_info(void)
{
	uart_printf("TrustedSSD FTL (compiled at %s %s)\r\n", __TIME__, __DATE__);

	uart_print("=== Memory Configuration ===");
	PRINT_SIZE("DRAM", 		DRAM_SIZE);
	uart_printf("# of cache buffers == %d\r\n", 	NUM_BC_BUFFERS);	
	PRINT_SIZE("page cache", 	PC_BYTES);
	PRINT_SIZE("bad block bitmap",	BAD_BLK_BMP_BYTES); 
	PRINT_SIZE("non SATA buffer size",		NON_SATA_BUF_BYTES);
	uart_printf("# of SATA read buffers == %d\r\n", NUM_SATA_RD_BUFFERS);
	PRINT_SIZE("SATA read buffers", 		SATA_RD_BUF_BYTES);
	uart_printf("# of SATA write buffers == %d\r\n",NUM_SATA_WR_BUFFERS);
	PRINT_SIZE("SATA write buffers",		SATA_WR_BUF_BYTES);
	PRINT_SIZE("page size",		BYTES_PER_PAGE);
	PRINT_SIZE("sub-page size",	BYTES_PER_SUB_PAGE);
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
	gtd_init();

	gc_init();

	page_cache_init();
	read_buffer_init();
	write_buffer_init();

	pmt_init();
#if OPTION_ACL
	sot_init();
#endif

	g_ftl_read_buf_id = g_ftl_write_buf_id = 0;

	flash_clear_irq();
	// This example FTL can handle runtime bad block interrupts and read fail (uncorrectable bit errors) interrupts
	SETREG(INTR_MASK, FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);
	SETREG(FCONF_PAUSE, FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);

	enable_irq();

	uart_print("ftl_open done\r\n");
}

#if OPTION_ACL
void ftl_read(UINT32 const lba, UINT32 const num_sectors, UINT32 const skey)
#else
void ftl_read(UINT32 const lba, UINT32 const num_sectors)
#endif
{
	UINT32 remain_sects, num_sectors_to_read;
    	UINT32 lpn, sect_offset;
#if OPTION_ACL
	BOOL8  access_granted = acl_verify(lba, num_sectors, skey);
	// FIXME: only grant access for authorized user
	access_granted = TRUE;
#endif

    	lpn          = lba / SECTORS_PER_PAGE;
    	sect_offset  = lba % SECTORS_PER_PAGE;
    	remain_sects = num_sectors;

/*    	uart_printf("ftl read: g_ftl_read_buf_id=%d, SATA_RBUF_PTR=%d, BM_READ_LIMIT=%d\r\n", 
			g_ftl_read_buf_id, GETREG(SATA_RBUF_PTR), GETREG(BM_READ_LIMIT));*/
	while (remain_sects != 0)
	{
        	if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
            		num_sectors_to_read = remain_sects;
        	else
            		num_sectors_to_read = SECTORS_PER_PAGE - sect_offset;

#if OPTION_ACL
		if (access_granted) 
			read_page(lpn, sect_offset, num_sectors_to_read);
		else	/* fake page read */
			read_empty_page(sect_offset, num_sectors_to_read);
#else
		read_page(lpn, sect_offset, num_sectors_to_read);
#endif

		sect_offset   = 0;
		remain_sects -= num_sectors_to_read;
		lpn++;
    	}
}

#if OPTION_ACL
void ftl_write(UINT32 const lba, UINT32 const num_sectors, UINT32 const skey)
#else
void ftl_write(UINT32 const lba, UINT32 const num_sectors)
#endif
{
	UINT32 remain_sects, num_sectors_to_write;
    	UINT32 lpn, sect_offset;

/*  	uart_printf("ftl write: g_ftl_write_buf_id=%d, SATA_WBUF_PTR=%d, BM_WRITE_LIMIT=%d\r\n", 
			g_ftl_write_buf_id, GETREG(SATA_WBUF_PTR), GETREG(BM_WRITE_LIMIT));*/

#if OPTION_ACL
	acl_authorize(lba, num_sectors, skey);	
#endif

	lpn          = lba / SECTORS_PER_PAGE;
	sect_offset  = lba % SECTORS_PER_PAGE;
 	remain_sects = num_sectors;

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
