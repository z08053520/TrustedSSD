#include "ftl.h"
#include "cmt.h"
#include "pmt.h"
#include "cache.h"

/* ========================================================================= *
 * Macros, Data Structure and Gloal Variables 
 * ========================================================================= */

UINT32 g_ftl_read_buf_id;
UINT32 g_ftl_write_buf_id;

typedef struct {

} ftl_metadata;

/* ========================================================================= *
 * Private Function Declarations 
 * ========================================================================= */

static void sanity_check(void);
static void build_bad_blk_list(void);
static void format(void);
static BOOL32 check_format_mark(void);
static void load_metadata();

/* ========================================================================= *
 * Private Functions 
 * ========================================================================= */

static void sanity_check(void)
{
	UINT32 dram_requirement = RD_BUF_BYTES + WR_BUF_BYTES + COPY_BUF_BYTES
		+ FTL_BUF_BYTES + HIL_BUF_BYTES + TEMP_BUF_BYTES 
		+ BAD_BLK_BMP_BYTES + VCOUNT_BYTES;

	BUG_ON("out of DRAM", dram_requirement > DRAM_SIZE ||
			      sizeof(ftl_metadata) > BYTES_PER_PAGE);
}

static void build_bad_blk_list(void)
{
/*  
	UINT32 bank, num_entries, result, vblk_offset;
	scan_list_t* scan_list = (scan_list_t*) TEMP_BUF_ADDR;
  
	mem_set_dram(BAD_BLK_BMP_ADDR, NULL, BAD_BLK_BMP_BYTES);

	disable_irq();

	flash_clear_irq();

	for (bank = 0; bank < NUM_BANKS; bank++)
	{
		SETREG(FCP_CMD, FC_COL_ROW_READ_OUT);
		SETREG(FCP_BANK, REAL_BANK(bank));
		SETREG(FCP_OPTION, FO_E);
		SETREG(FCP_DMA_ADDR, (UINT32) scan_list);
		SETREG(FCP_DMA_CNT, SCAN_LIST_SIZE);
		SETREG(FCP_COL, 0);
		SETREG(FCP_ROW_L(bank), SCAN_LIST_PAGE_OFFSET);
		SETREG(FCP_ROW_H(bank), SCAN_LIST_PAGE_OFFSET);

		SETREG(FCP_ISSUE, NULL);
		while ((GETREG(WR_STAT) & 0x00000001) != 0);
		while (BSP_FSM(bank) != BANK_IDLE);

		num_entries = NULL;
		result = OK;

		if (BSP_INTR(bank) & FIRQ_DATA_CORRUPT)
		{
			result = FAIL;
		}
		else
		{
			UINT32 i;

			num_entries = read_dram_16(&(scan_list->num_entries));

			if (num_entries > SCAN_LIST_ITEMS)
			{
				result = FAIL;
			}
			else
			{
				for (i = 0; i < num_entries; i++)
				{
					UINT16 entry = read_dram_16(scan_list->list + i);
					UINT16 pblk_offset = entry & 0x7FFF;

					if (pblk_offset == 0 || pblk_offset >= PBLKS_PER_BANK)
					{
						#if OPTION_REDUCED_CAPACITY == FALSE
						result = FAIL;
						#endif
					}
					else
					{
						write_dram_16(scan_list->list + i, pblk_offset);
					}
				}
			}
		}

		if (result == FAIL)
		{
			num_entries = 0;
		}
		else
		{
			write_dram_16(&(scan_list->num_entries), 0);
		}

		g_bad_blk_count[bank] = 0;

		for (vblk_offset = 1; vblk_offset < VBLKS_PER_BANK; vblk_offset++)
		{
			BOOL32 bad = FALSE;

			#if OPTION_2_PLANE
			{
				UINT32 pblk_offset;

				pblk_offset = vblk_offset * NUM_PLANES;

				if (mem_search_equ_dram(scan_list, sizeof(UINT16), num_entries, pblk_offset) < num_entries)
				{
					bad = TRUE;
				}

				pblk_offset = vblk_offset * NUM_PLANES + 1;

				if (mem_search_equ_dram(scan_list, sizeof(UINT16), num_entries, pblk_offset) < num_entries)
				{
					bad = TRUE;
				}
			}
			#else
			{
				if (mem_search_equ_dram(scan_list, sizeof(UINT16), num_entries, vblk_offset) < num_entries)
				{
					bad = TRUE;
				}
			}
			#endif

			if (bad)
			{
				g_bad_blk_count[bank]++;
				set_bit_dram(BAD_BLK_BMP_ADDR + bank*(VBLKS_PER_BANK/8 + 1), vblk_offset);
			}
		}
	}
*/
}

static void format(void)
{

}

static BOOL32 check_format_mark(void)
{
	return 1;
}

static void load_metadata() {

}

/* ========================================================================= *
 * Public API 
 * ========================================================================= */

void ftl_open(void) {
	led(0);
    	sanity_check();
	build_bad_blk_list();
	
	/* if this is the first time after reloading firmware */
	if (check_format_mark() == FALSE)
		format();

        load_metadata();
	cmt_init();
	cache_init();

	g_ftl_read_buf_id = g_ftl_write_buf_id = 0;
}

UINT32 ftl_get_vpn(UINT32 const lpn)
{
	UINT32 vpn;
	UINT32 victim_lpn, victim_vpn; BOOL32 victim_dirty;
	UINT32 res;
  
	// if lpn is not cached, the new entry has to be loaded
	if (cmt_get(lpn, &vpn)) {	
		// and if cache is full, a entry has to be evicted
		if (cmt_is_full()) {	
			res =  cmt_evict(&victim_lpn, &victim_vpn, &victim_dirty);
			BUG_ON("failed to evit; never should happen", res);	

			// need to write back
			if (victim_dirty)
				pmt_update(victim_lpn, victim_vpn);
		}
		pmt_fetch(lpn, &vpn);
		cmt_add(lpn, vpn);
	}
	return vpn;
}

UINT32 ftl_get_new_vpn(UINT32 const lpn)
{
	return 0;
}

void ftl_read(UINT32 const lba, UINT32 const num_sectors) 
{
	UINT32 remain_sects, num_sectors_to_read;
    	UINT32 lpn, sect_offset;
    	UINT32 bank, vpn;
	UINT32 page_buff;

    	lpn          = lba / SECTORS_PER_PAGE;
    	sect_offset  = lba % SECTORS_PER_PAGE;
    	remain_sects = num_sectors;

	while (remain_sects != 0)
	{
        	if ((sect_offset + remain_sects) < SECTORS_PER_PAGE)
            		num_sectors_to_read = remain_sects;
        	else
            		num_sectors_to_read = SECTORS_PER_PAGE - sect_offset;

		// try to read from cache
		cache_get(lpn, &page_buff, CACHE_BUF_TYPE_USR);
		if (page_buff) {
			cache_fill(lpn, sect_offset, num_sectors_to_read, CACHE_BUF_TYPE_USR);
			mem_copy(RD_BUF_PTR(g_ftl_read_buf_id),
				 page_buff,
				 BYTES_PER_PAGE);
			goto mem_xfer_done;
		}
		
		vpn  = ftl_get_vpn(lpn);
		// read from flash
        	if (vpn != NULL)
        	{
			bank = lpn2bank(lpn);
			nand_page_ptread_to_host(bank,
						 vpn / PAGES_PER_BLK,
						 vpn % PAGES_PER_BLK,
						 sect_offset,
						 num_sectors_to_read);
			goto next_page;
        	}
        	// the logical page has never been written to 
        	else
        	{
			mem_set_dram(RD_BUF_PTR(g_ftl_read_buf_id) 
					+ sect_offset * BYTES_PER_SECTOR,
                         		0xFFFFFFFF, 
					num_sectors_to_read*BYTES_PER_SECTOR);
		}
mem_xfer_done:
		flash_finish();
		g_ftl_read_buf_id++;

		// read buffer is ready for SATA transfer
		SETREG(BM_STACK_RDSET, g_ftl_read_buf_id);
		SETREG(BM_STACK_RESET, 0x02);
next_page:
		sect_offset   = 0;
		remain_sects -= num_sectors_to_read;
		lpn++;
    	}
}

void ftl_write(UINT32 const lba, UINT32 const num_sectors) {

}

void ftl_test_write(UINT32 const lba, UINT32 const num_sectors) {

}

void ftl_flush(void) {

}

void ftl_isr(void) {

}
