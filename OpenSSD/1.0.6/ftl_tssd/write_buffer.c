#include "write_buffer.h"
#include "gc.h"
#include "mem_util.h"

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
static UINT8	head_buf_id, tail_bud_id;
static sectors_mask_t buf_masks[NUM_WRITE_BUFFERS]; /* 1 - occupied; 0 - available */
static UINT8	buf_sizes[NUM_WRITE_BUFFERS];

#define NULL_BID			0xFF
#define next_buf_id(buf_id)		(((buf_id) + 1) % NUM_WRITE_BUFFERS)

/* ========================================================================= *
 * Private Functions 
 * ========================================================================= */

static UINT32 find_lpn_idx(UINT32 const lpn)
{
	return mem_search_equ_sram(lpns, sizeof(UINT32), MAX_NUM_LPNS, lpn);
}

static void remove_lpn(UINT32 const lpn)
{
	UINT32 lpn_idx = find_lpn_idx(lpn);
	if (lpn_idx >= MAX_NUM_LPNS) return;

	sectors_mask_t mask    = lp_masks[lpn_idx];
	UINT8  bid     = buf_ids[lpn_idx];
	BUG_ON("invalid bid", bid >= NUM_WRITE_BUFFERS);

	buf_masks[bid] 	       &= ~mask;
	buf_sizes[bid]		= count_sectors(buf_masks[bid]);

	lpns[lpn_idx]  		= LPN_NULL;
	lp_masks[lpn_idx] 	= 0;
	buf_ids[lpn_idx] 	= BID_NULL;

	num_lpns--;
}

static UINT8 find_fullest_buffer()
{
	return mem_search_min_max(buf_sizes, 
				  sizeof(UINT8), 
				  NUM_WRITE_BUFFERS,
				  MU_CMD_SEARCH_MAX_SRAM);
}

static void flush_buffer() 
{
	UINT8	vicitm_buf_id   = find_fullest_buffer();
	BUG_ON("flush any empty buffer", buf_sizes[vicitm_buf_id] == 0);

	UINT8   bank    = gc_get_idle_bank();
	UINT32  vpn     = gc_allocate_new_vpn(bank);

	UINT32  lpn_i   = 0;
	// Iterate each lpn in the vicitm buffer
	while (lpn_i < MAX_NUM_LPNS)  {
		if (buf_ids[lpn_i] != victim_buf_id) {
			lpn_i++;
			break;
		}
		
		// Make sure whole sub-pages are in buffer
		UINT32 		lpn 	= lpns[lpn_i];
		sectors_mask_t 	lp_mask = lp_masks[lpn_i];
		fu_fill_sub_pages(lpn, lp_mask, WRITE_BUF(victim_buf_id));

		// Update mapping table
		UINT8  begin_sp	   = begin_sector(lp_mask) / SECTORS_PER_SUB_PAGE,
		       end_sp	   = end_sector(lp_mask)   / SECTORS_PER_SUB_PAGE;
		UINT32 lspn	   = lpn * SUB_PAGES_PER_PAGE + begin_sp,
		       end_lspn	   = lspn + end_sp;
		while (lspn < end_lspn) {
			pmt_update(lspn, vpn);
			lspn++;
		}

		lpn_i++;
	}

	UINT64 vicitm_buf_mask 	= buf_masks[victim_buf_id];
	UINT32 offset 	   	= begin_sector(victim_buf_mask),
	       num_sectors 	= end_sector(victim_buf_mask) - offset;
	nand_page_ptprogram(bank, 
			    vpn / PAGES_PER_VBLK, 
			    vpn % PAGES_PER_VBLK,
			    offset, num_sectors,
			    WRITE_BUF(victim_buf_id));

	remove_buffer(victim_buf_id);
	if (victim_buf_id == head_buf_id) 
		head_buf_id = next_buf_id(head_buf_id);
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
		remove_lpn(lpn);

		UINT8 bank = gc_get_idle_bank();
		UINT32 vpn = gc_allocate_new_vpn(bank);
		nand_page_program_from_host(bank, 
				  	    vpn / PAGES_PER_VBLK, 
				  	    vpn % PAGES_PER_VBLK);
		return;
	}

	if (should_flush_buffer()) flush_buffer();

	// Try to merge with existing data from the same lpn in the buffer 
	UINT32 lpn_idx = find_lpn_idx(lpn);
	if (lpn_idx < MAX_NUM_LPNS) {
						
	}
}
