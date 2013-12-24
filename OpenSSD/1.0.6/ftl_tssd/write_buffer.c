#include "write_buffer.h"
#include "gc.h"
#include "mem_util.h"
#include "flash_util.h"

extern UINT32 g_ftl_write_buf_id;

/* ========================================================================= *
 * Macros, Data Structure and Gloal Variables 
 * ========================================================================= */

#define MAX_NUM_LPNS			(4 * NUM_WRITE_BUFFERS)
#define NULL_LPN			0xFFFFFFFF

/* For each logical page which has some part in write buffer, three fields are 
 * maintained: lpn, valid sector mask and buffer id. */
static UINT32	num_lpns;
static UINT32	lpns[MAX_NUM_LPNS];
static sectors_mask_t lp_masks[MAX_NUM_LPNS]; /* 1 - in buff; 0 - not in buff  */
static UINT8	buf_ids[MAX_NUM_LPNS];

/* For each write buffer, the  */
static UINT8	head_buf_id, tail_buf_id;
static sectors_mask_t buf_masks[NUM_WRITE_BUFFERS]; /* 1 - occupied; 0 - available */
static UINT8	buf_sizes[NUM_WRITE_BUFFERS];

#define NULL_BID			0xFF
#define next_buf_id(buf_id)		(((buf_id) + 1) % NUM_WRITE_BUFFERS)

/* ========================================================================= *
 * Private Functions 
 * ========================================================================= */

static UINT32 find_index_of_lpn(UINT32 const lpn)
{
	return mem_search_equ_sram(lpns, sizeof(UINT32), MAX_NUM_LPNS, lpn);
}

static void remove_lpn_by_index(UINT32 const lpn_idx)
{
	sectors_mask_t mask     = lp_masks[lpn_idx];
	UINT8  bid     	        = buf_ids[lpn_idx];
	BUG_ON("invalid bid", bid >= NUM_WRITE_BUFFERS);

	buf_masks[bid] 	       &= ~mask;
	buf_sizes[bid]		= count_sectors(buf_masks[bid]);

	lpns[lpn_idx]  		= NULL_LPN;
	lp_masks[lpn_idx] 	= 0;
	buf_ids[lpn_idx] 	= NULL_BID;

	num_lpns--;
}

static void try_to_remove_lpn(UINT32 const lpn)
{
	UINT32 lpn_idx = find_index_of_lpn(lpn);
	if (lpn_idx >= MAX_NUM_LPNS) return;

	remove_lpn_by_index(lpn_idx);	
}

static UINT8 find_fullest_buffer()
{
	return mem_search_min_max(buf_sizes, 
				  sizeof(UINT8), 
				  NUM_WRITE_BUFFERS,
				  MU_CMD_SEARCH_MAX_SRAM);
}

static void fill_whole_sub_page(UINT32 const lspn, UINT8 const lsp_mask, UINT32 const buff)
{
	if (lsp_mask == 0xFF) return;

	vp_t vp; 
	pmt_fetch(lspn, &vp);

	UINT8  vsp_offset = lspn % SUB_PAGES_PER_PAGE;
	UINT32 vspn = vp.vpn * SUB_PAGES_PER_PAGE + vsp_offset;
	vsp_t vsp = {.bank = vp.bank, .vspn = vspn};
	fu_read_sub_page(vsp, TEMP_BUF_ADDR);

	UINT32 sp_src_buff = TEMP_BUF_ADDR + vsp_offset * BYTES_PER_SUB_PAGE; 
	UINT8 i = 0;
	while (i < SECTORS_PER_SUB_PAGE) {
		// find the first missing sector
		while (i < SECTORS_PER_SUB_PAGE && 
		       (((lsp_mask >> i) & 1) == 1)) i++;
		if (i == SECTORS_PER_SUB_PAGE) break;
		UINT8 begin_i = i++;
		
		// find the last missing sector
		while (i < SECTORS_PER_SUB_PAGE &&
		       (((lsp_mask >> i) & 1) == 0)) i++;
		UINT8 end_i   = i++;

		mem_copy(buff + begin_i * BYTES_PER_SECTOR,
			 sp_src_buff + begin_i * BYTES_PER_SECTOR,
			 (end_i - begin_i) * BYTES_PER_SECTOR);
	}
}

static void remove_buffer(UINT8 const buf_id)
{
	buf_masks[buf_id] = 0;
	buf_sizes[buf_id] = 0;

	if (buf_id == head_buf_id) 
		head_buf_id = next_buf_id(head_buf_id);
}

static void flush_buffer() 
{
	UINT8	victim_buf_id   = find_fullest_buffer();
	BUG_ON("flush any empty buffer", buf_sizes[victim_buf_id] == 0);

	UINT8   bank    = fu_get_idle_bank();
	UINT32  vpn	= gc_allocate_new_vpn(bank);
	vp_t	vp	= {.bank = bank, .vpn = vpn};

	UINT32  lpn_i   = 0;
	// Iterate each lpn in the victim buffer
	while (lpn_i < MAX_NUM_LPNS)  {
		if (buf_ids[lpn_i] != victim_buf_id) {
			lpn_i++;
			break;
		}
		
		UINT32 		lpn 	= lpns[lpn_i];
		sectors_mask_t 	lp_mask = lp_masks[lpn_i];

		UINT8  begin_sp	   = begin_sector(lp_mask) / SECTORS_PER_SUB_PAGE,
		       end_sp	   = end_sector(lp_mask)   / SECTORS_PER_SUB_PAGE;
		UINT8  sp_offset   = begin_sp;
		UINT32 lspn	   = lpn * SUB_PAGES_PER_PAGE + begin_sp,
		       end_lspn	   = lspn + end_sp;
		while (lspn < end_lspn) {
			UINT8	lsp_mask = 0xFF && (lp_mask >> 
					   (SECTORS_PER_SUB_PAGE * sp_offset));
			fill_whole_sub_page(lspn, lsp_mask, 
					    WRITE_BUF(victim_buf_id) + 
					    	BYTES_PER_SUB_PAGE * sp_offset);
			pmt_update(lspn, vp);

			lspn++;
			sp_offset++;
		}

		// remove the lpn from buffer
		remove_lpn_by_index(lpn_i);	

		lpn_i++;
	}

	UINT64 victim_buf_mask 	= buf_masks[victim_buf_id];
	UINT32 offset 	   	= begin_sector(victim_buf_mask),
	       num_sectors 	= end_sector(victim_buf_mask) - offset;
	nand_page_ptprogram(bank, 
			    vpn / PAGES_PER_VBLK, 
			    vpn % PAGES_PER_VBLK,
			    offset, num_sectors,
			    WRITE_BUF(victim_buf_id));

	remove_buffer(victim_buf_id);
}

static UINT8 find_available_buffer_for(sectors_mask_t const mask)
{
	UINT8 new_buf_id = head_buf_id;
	do {
		if ((buf_masks[new_buf_id] & mask) == 0)
			return new_buf_id;
		new_buf_id = next_buf_id(new_buf_id);
	} while (new_buf_id != head_buf_id);
	return NULL_BID;	
}

static void copy_to_buffer(UINT32 const target_buf, UINT32 const src_buf, 
			   sectors_mask_t const mask)
{
	UINT8 sector_i = 0;
	while (sector_i < SECTORS_PER_PAGE) {
		// find the first sector to copy
		while (sector_i < SECTORS_PER_PAGE && 
		       ((mask >> sector_i) & 1) == 0) sector_i++;
		if (sector_i == SECTORS_PER_PAGE) break;
		UINT8 begin_sector = sector_i++;

		// find the last sector to copy
		while (sector_i < SECTORS_PER_PAGE && 
		       ((mask >> sector_i) & 1) == 1) sector_i++;
		UINT8 end_sector = sector_i++;

		mem_copy(target_buf + begin_sector * BYTES_PER_SECTOR,
			 src_buf    + begin_sector * BYTES_PER_SECTOR,
			 (end_sector - begin_sector) * BYTES_PER_SECTOR);
	}
}

static void insert_and_merge_lpn(UINT32 const lpn, UINT8 const sector_offset, 
				 UINT8  const num_sectors)
{
	sectors_mask_t  lp_new_mask = init_mask(sector_offset, num_sectors);
	UINT8		new_buf_id  = NULL_BID;	
	
	// Try to merge with the same lpn in the buffer 
	UINT32 lp_old_idx = find_index_of_lpn(lpn); 
	if (lp_old_idx < MAX_NUM_LPNS) {
		sectors_mask_t lp_old_mask  = lp_masks[lp_old_idx];
		UINT8	       old_buf_id   = buf_ids[lp_old_idx];
		sectors_mask_t old_buf_mask = buf_masks[old_buf_id];

		// Use old buffer if it has enough room
		if (((old_buf_mask & ~lp_old_mask) & lp_new_mask) == 0) {
			new_buf_id = old_buf_id;
			goto do_insertion;
		}

		// if old buffer doesn't have more room for new data from the
		// same lpn, then we need to find another buffer that fits both 
		// old & new data
		sectors_mask_t merged_mask = lp_old_mask | lp_new_mask;
		new_buf_id = find_available_buffer_for(merged_mask);
		BUG_ON("No available buffer", new_buf_id == NULL_BID);	

		// move useful part of old data to new buffer 
		sectors_mask_t old_useful_mask = 
			lp_old_mask & ~(lp_old_mask & lp_new_mask);
		copy_to_buffer(WRITE_BUF(new_buf_id),
			       WRITE_BUF(old_buf_id),
			       old_useful_mask);

		
		remove_lpn_by_index(lp_old_idx);
	}
do_insertion:
	if (new_buf_id == NULL_BID) {
		new_buf_id = find_available_buffer_for(lp_new_mask);
		
		// find a new LPN slot...
	}

	copy_to_buffer(WRITE_BUF(new_buf_id),
		       SATA_WR_BUF_PTR(g_ftl_write_buf_id),
		       lp_new_mask);
	buf_masks[new_buf_id] |= lp_new_mask;
	buf_sizes[new_buf_id]  = count_sectors(buf_masks[new_buf_id]);
}

/* ========================================================================= *
 * Public API 
 * ========================================================================= */


void write_buffer_init()
{
	BUG_ON("# of LPN slots must be a multiple of 4", MAX_NUM_LPNS % 4 != 0);
	BUG_ON("# of write buffers must be a multiple of 4", NUM_WRITE_BUFFERS % 4 != 0);
	BUG_ON("# of write buffers is too large", NUM_WRITE_BUFFERS >= 255);

	num_lpns      = 0;
	head_buf_id   = 0;
	tail_buf_id   = 0;

	mem_set_sram(lpns, 	  NULL_LPN, 	MAX_NUM_LPNS * sizeof(UINT32));
	mem_set_sram(lp_masks, 	  0, 		MAX_NUM_LPNS * sizeof(UINT64));
	mem_set_sram(buf_ids,  	  NULL_BID, 	MAX_NUM_LPNS * sizeof(UINT8));

	mem_set_sram(buf_masks,   0, 		NUM_WRITE_BUFFERS * sizeof(UINT64));
	mem_set_sram(buf_sizes,   0, 		NUM_WRITE_BUFFERS * sizeof(UINT8));
}

#define should_flush_buffer()	(num_lpns == MAX_NUM_LPNS || \
				 next_buf_id(tail_buf_id) == head_buf_id)

void write_buffer_get(UINT32 const lpn, UINT8 const sector_offset, 
		      UINT32 *buf)
{
	*buf = NULL;
}

void write_buffer_put(UINT32 const lpn, UINT8 const sector_offset, 
		      UINT8  const num_sectors)
{
	if (num_sectors == 0) return;

	// Write full page to flash directly
	if (num_sectors == SECTORS_PER_PAGE) {
		try_to_remove_lpn(lpn);

		UINT8 bank = fu_get_idle_bank();
		UINT32 vpn = gc_allocate_new_vpn(bank);
		nand_page_program_from_host(bank, 
				  	    vpn / PAGES_PER_VBLK, 
				  	    vpn % PAGES_PER_VBLK);
		return;
	}

	if (should_flush_buffer()) 
		flush_buffer();

	insert_and_merge_lpn(lpn, sector_offset, num_sectors);
}
