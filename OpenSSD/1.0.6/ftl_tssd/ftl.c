#include "ftl.h"
#include "cmt.h"
#include "gtd.h"
#include "pmt.h"
#include "gc.h"
#include "cache.h"

/* ========================================================================= *
 * Macros, Data Structure and Gloal Variables 
 * ========================================================================= */

UINT32 g_ftl_read_buf_id;
UINT32 g_ftl_write_buf_id;

typedef struct {

} ftl_metadata;

/* ========================================================================= *
 * Private Functions 
 * ========================================================================= */

static void sanity_check(void)
{
	UINT32 dram_requirement = RD_BUF_BYTES + WR_BUF_BYTES + COPY_BUF_BYTES
		+ FTL_BUF_BYTES + HIL_BUF_BYTES + TEMP_BUF_BYTES 
		+ BAD_BLK_BMP_BYTES + CACHE_BYTES;

	BUG_ON("DRAM is under/over-utilized", dram_requirement != DRAM_SIZE);
	BUG_ON("ftl_metadata is too larget", sizeof(ftl_metadata) > BYTES_PER_PAGE);
}

static UINT32 get_vpn(UINT32 const lpn)
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

	// try to read from cache
	cache_get(lpn, &page_buff, CACHE_BUF_TYPE_USR);
	if (page_buff) {
		cache_fill(lpn, sect_offset, num_sectors_to_read, CACHE_BUF_TYPE_USR);
		mem_copy(RD_BUF_PTR(g_ftl_read_buf_id) + BYTES_PER_SECTOR * sect_offset,
			 page_buff + BYTES_PER_SECTOR * sect_offset,
			 BYTES_PER_SECTOR * num_sectors_to_read);
		goto mem_xfer_done;
	}
	
	vpn  = get_vpn(lpn);

	// wait for the next buffer to get SATA transfer done
	while (g_ftl_read_buf_id == GETREG(SATA_RBUF_PTR));

	// read from flash
	if (vpn != NULL)
	{
		bank = lpn2bank(lpn);
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
            	// Send 0xFF...FF to host when the host request to read the 
		// sector that has never been written.
		mem_set_dram(RD_BUF_PTR(g_ftl_read_buf_id) 
				+ sect_offset * BYTES_PER_SECTOR,
				0xFFFFFFFF, 
				num_sectors_to_read * BYTES_PER_SECTOR);
	}

mem_xfer_done:
	g_ftl_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_RD_BUFFERS;
	
	flash_finish();

	// read buffer is ready for SATA transfer
	SETREG(BM_STACK_RDSET, g_ftl_read_buf_id);
	SETREG(BM_STACK_RESET, 0x02);
}

static void write_page (UINT32 const lpn, 
			UINT32 const sect_offset, 
			UINT32 const num_sectors_to_write)
{
	UINT32 buff_addr;
	UINT32 target_addr, src_addr, num_bytes; 

	cache_get(lpn, &buff_addr, CACHE_BUF_TYPE_USR);
	if (buff_addr == NULL) {
		cache_put(lpn, &buff_addr, CACHE_BUF_TYPE_USR);

		/* init buffer */ 

		target_addr = buff_addr;
		num_bytes   = sect_offset * BYTES_PER_SECTOR;
		mem_set_dram(target_addr, 0xFFFFFFFF, num_bytes);

		target_addr = buff_addr + (sect_offset + 
				num_sectors_to_write) * BYTES_PER_SECTOR;
		num_bytes   = (SECTORS_PER_PAGE - sect_offset - 
				num_sectors_to_write) * BYTES_PER_SECTOR;
		mem_set_dram(target_addr, 0xFFFFFFFF, num_bytes);
	}

	// wait for SATA transfer completion
	while (g_ftl_write_buf_id == GETREG(SATA_WBUF_PTR));

	// copy from SATA circular buffer to buffer cache 
	target_addr = buff_addr + sect_offset * BYTES_PER_PAGE;
	src_addr    = WR_BUF_PTR(g_ftl_write_buf_id) + sect_offset * BYTES_PER_PAGE;
	num_bytes   = num_sectors_to_write * BYTES_PER_PAGE;
	mem_copy(target_addr, src_addr, num_bytes);

	cache_set_valid_sectors(lpn, sect_offset, num_sectors_to_write, CACHE_BUF_TYPE_USR);
	cache_set_dirty(lpn, CACHE_BUF_TYPE_USR);

	g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;	

	SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);
	SETREG(BM_STACK_RESET, 0x01);
}

static void print_info(void)
{

}

/* ========================================================================= *
 * Public API 
 * ========================================================================= */

void ftl_open(void) {
	led(0);
    	sanity_check();

	print_info();

	/* the initialization order indicates the dependencies between modules */
	bb_init();	
	cmt_init();
	gtd_init();

	gc_init();

	cache_init();

	pmt_init();

	g_ftl_read_buf_id = g_ftl_write_buf_id = 0;
}

void ftl_read(UINT32 const lba, UINT32 const num_sectors) 
{
	UINT32 remain_sects, num_sectors_to_read;
    	UINT32 lpn, sect_offset;

    	lpn          = lba / SECTORS_PER_PAGE;
    	sect_offset  = lba % SECTORS_PER_PAGE;
    	remain_sects = num_sectors;

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

}

void ftl_isr(void) {
	/* TODO: add BSP interrupt handler */
}
