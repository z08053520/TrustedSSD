#include "write_buffer.h"
#include "dram.h"
#include "gc.h"
#include "mem_util.h"
#include "flash_util.h"

/* ========================================================================= *
 * Macros, Data Structure and Gloal Variables 
 * ========================================================================= */

/* For each write buffer, two fields are mainted: mask and size. */
typedef UINT8 		buf_id_t;
#define NULL_BID	0xFF

static buf_id_t		head_buf_id;
static UINT32		num_clean_buffers;
static sectors_mask_t 	buf_masks[NUM_WRITE_BUFFERS]; /* 1 - occupied; 0 - available */
static UINT8		buf_sizes[NUM_WRITE_BUFFERS];

#define next_buf_id(buf_id)		(((buf_id) + 1) % NUM_WRITE_BUFFERS)

/* For each logical page which has some part in write buffer, three fields are 
 * maintained: lpn, valid sector mask and buffer id. */
#define MAX_NUM_LPNS			(8 * NUM_WRITE_BUFFERS)
#define NULL_LPN			0xFFFFFFFF
static UINT32		num_lpns;
static UINT32		lpns[MAX_NUM_LPNS];
static sectors_mask_t 	lp_masks[MAX_NUM_LPNS]; /* 1 - in buff; 0 - not in buff  */
static buf_id_t		buf_ids[MAX_NUM_LPNS];


/* ========================================================================= *
 * Private Functions 
 * ========================================================================= */

static BOOL8 find_index_of_lpn(UINT32 const lpn, UINT32 *lpn_idx)
{
	UINT32 idx = mem_search_equ_sram(lpns, sizeof(UINT32), MAX_NUM_LPNS, lpn);
	if (idx >= MAX_NUM_LPNS) return FALSE;
	
	*lpn_idx = idx;
	return TRUE;
}

static void remove_lpn_by_index(UINT32 const lpn_idx)
{
	BUG_ON("out of bound", lpn_idx >= MAX_NUM_LPNS);

	sectors_mask_t mask     = lp_masks[lpn_idx];
	buf_id_t bid     	= buf_ids[lpn_idx];
	BUG_ON("invalid bid", bid >= NUM_WRITE_BUFFERS);

	buf_masks[bid] 	       &= ~mask;
	buf_sizes[bid]		= count_sectors(buf_masks[bid]);

	lpns[lpn_idx]  		= NULL_LPN;
	lp_masks[lpn_idx] 	= 0;
	buf_ids[lpn_idx] 	= NULL_BID;

	num_lpns--;
}

static buf_id_t find_fullest_buffer()
{
	buf_id_t max_buf_id   =  mem_search_min_max(buf_sizes, 
						  sizeof(buf_id_t), 
						  NUM_WRITE_BUFFERS,
						  MU_CMD_SEARCH_MAX_SRAM);
	// head buf has priority
	UINT8  max_buf_size = buf_sizes[max_buf_id];
	if (max_buf_id != head_buf_id && buf_sizes[head_buf_id] == max_buf_size)
		max_buf_id  = head_buf_id;
	
	// DEBUG
	/*  UINT8 i = 0;
	uart_printf("buf_sizes=[");
	for(i = 0; i < NUM_WRITE_BUFFERS;i++) {
		if (i) uart_printf(", ");
		uart_printf("%u", buf_sizes[i]);
	}
	uart_print("]");
	uart_printf("max_buf_id = %u\r\n", max_buf_id);
*/

	return max_buf_id;
}

#define FOR_EACH_MISSING_SEGMENTS_IN_SUB_PAGE(segment_handler)		\
	UINT8 i = 0;							\
	while (i < SECTORS_PER_SUB_PAGE) {				\
		/* find the first missing sector */			\
		while (i < SECTORS_PER_SUB_PAGE &&			\
		       (((lsp_mask >> i) & 1) == 1)) i++;		\
		if (i == SECTORS_PER_SUB_PAGE) break;			\
		UINT8 begin_i = i++;					\
		/* find the last missing sector */			\
		while (i < SECTORS_PER_SUB_PAGE &&			\
		       (((lsp_mask >> i) & 1) == 0)) i++;		\
		UINT8 end_i   = i++;					\
		/* evoke segment handler */				\
		(*segment_handler)(begin_i, end_i);			\
	}
	
static void fill_whole_sub_page(UINT32 const lspn, UINT8 const lsp_mask, UINT32 const buff)
{
	vp_t vp; 
	pmt_fetch(lspn, &vp);

	// Prepare handler for the missing segments
	void (*segment_handler) (UINT8, UINT8);
	// Read existing sub-page from flash to fill the missing sectors
	if (vp.vpn) {
		UINT8  vsp_offset = lspn % SUB_PAGES_PER_PAGE;
		UINT32 vspn 	  = vp.vpn * SUB_PAGES_PER_PAGE + vsp_offset;
		vsp_t  vsp 	  = {.bank = vp.bank, .vspn = vspn};
		fu_read_sub_page(vsp, FTL_BUF(vp.bank));
		
		UINT32 sp_src_buff = FTL_BUF(vp.bank)+ vsp_offset * BYTES_PER_SUB_PAGE; 

		segment_handler = 
			lambda (void, (UINT8 begin_i, UINT8 end_i) {
				mem_copy(buff + begin_i * BYTES_PER_SECTOR,
					 sp_src_buff + begin_i * BYTES_PER_SECTOR,
					 (end_i - begin_i) * BYTES_PER_SECTOR);
			});
		FOR_EACH_MISSING_SEGMENTS_IN_SUB_PAGE(segment_handler);
	}
	// Fill missing sectors with 0xFF...FF
	else {
		segment_handler = 
			lambda (void, (UINT8 begin_i, UINT8 end_i) {
				mem_set_dram(buff + begin_i * BYTES_PER_SECTOR,
					     0xFFFFFFFF, (end_i - begin_i) * BYTES_PER_SECTOR);
			});
		FOR_EACH_MISSING_SEGMENTS_IN_SUB_PAGE(segment_handler);
	}
}

static void flush_buffer() 
{
	buf_id_t 	victim_buf_id   = find_fullest_buffer();
	BUG_ON("flush any empty buffer", buf_sizes[victim_buf_id] == 0);
	sectors_mask_t 	victim_buf_mask = buf_masks[victim_buf_id];

	UINT8   bank    = fu_get_idle_bank();
	UINT32  vpn	= gc_allocate_new_vpn(bank);
	vp_t	vp	= {.bank = bank, .vpn = vpn};

	UINT32  lpn_i   = 0;
	// Iterate each lpn in the victim buffer
	while (lpn_i < MAX_NUM_LPNS)  {
		if (buf_ids[lpn_i] != victim_buf_id) {
			lpn_i++;
			continue;
		}
		
		UINT32 		lpn 	= lpns[lpn_i];
		sectors_mask_t 	lp_mask = lp_masks[lpn_i];

		UINT8  begin_sp	   = begin_sector(lp_mask) / SECTORS_PER_SUB_PAGE,
		       end_sp	   = end_sector(lp_mask)   / SECTORS_PER_SUB_PAGE;
		UINT8  sp_offset   = begin_sp;
		UINT32 lspn	   = lpn * SUB_PAGES_PER_PAGE + begin_sp,
		       end_lspn	   = lspn + end_sp;
		while (lspn < end_lspn) {
			UINT8	lsp_mask = (lp_mask >> (SECTORS_PER_SUB_PAGE * sp_offset));
			// skip "holes"
			if (lsp_mask != 0) {
				if (lsp_mask != 0xFF)
					fill_whole_sub_page(lspn, lsp_mask, 
						WRITE_BUF(victim_buf_id) + 
						BYTES_PER_SUB_PAGE * sp_offset);
				pmt_update(lspn, vp);
			}
			
			lspn++;
			sp_offset++;
		}

		// remove the lpn from buffer
		remove_lpn_by_index(lpn_i);	

		lpn_i++;
	}

	// TODO: Take care of the holes in the buffer to avoid leaking information
	//
	// Some holes in the buffer may be filled with **undefined** data,
	// leaking information in unexpected way. The safest way is too erased 
	// the data carefully when manipulating the buffer. For now, I just
	// contend with work correctly instead of perfectly. 
	UINT32 offset 	   	= begin_sector(victim_buf_mask),
	       num_sectors 	= end_sector(victim_buf_mask) - offset;
	mem_copy(FTL_BUF(bank) + offset * BYTES_PER_SECTOR, 
		 WRITE_BUF(victim_buf_id) + offset * BYTES_PER_SECTOR,
		 num_sectors * BYTES_PER_SECTOR);
	nand_page_ptprogram(bank, 
			    vpn / PAGES_PER_VBLK, 
			    vpn % PAGES_PER_VBLK,
			    offset, num_sectors,
			    FTL_BUF(bank));

	// Remove buffer
	BUG_ON("buf mask is not cleared", buf_masks[victim_buf_id] != 0ULL);
	BUG_ON("buf size is not zero", buf_sizes[victim_buf_id] != 0);
	num_clean_buffers++;
	if (victim_buf_id == head_buf_id) 
		head_buf_id = next_buf_id(head_buf_id);
}

static UINT8 allocate_buffer_for(sectors_mask_t const mask)
{
	UINT8 new_buf_id = head_buf_id;
	do {
		if ((buf_masks[new_buf_id] & mask) == 0) {
			if (buf_masks[new_buf_id] == 0) 
				num_clean_buffers--;
			return new_buf_id;
		}
		new_buf_id = next_buf_id(new_buf_id);
	} while (new_buf_id != head_buf_id);
	BUG_ON("no free buffer to allocate", 1);
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

static UINT32 get_free_lpn_index()
{
	UINT32 free_lpn_idx;
	BOOL8  res = find_index_of_lpn(NULL_LPN, &free_lpn_idx);
	BUG_ON("no available slot for new lpn", res == FALSE);
	return free_lpn_idx;
}

static void insert_and_merge_lpn(UINT32 const lpn, UINT8 const sector_offset, 
				 UINT8  const num_sectors,
				 UINT32 const buf)
{
	sectors_mask_t  lp_new_mask = init_mask(sector_offset, num_sectors);
	buf_id_t	new_buf_id  = NULL_BID;	

	// DEBUG
	/*  uart_printf("offset = %u, num_sectors = %u\r\n", sector_offset, num_sectors);
	uart_printf("lp_new_mask = ");uart_print_hex_64(lp_new_mask);
	uart_printf("max 64 uint = ");uart_print_hex_64(0xFFFFFFFFFFFFFFFFULL);*/
	// Try to merge with the same lpn in the buffer 
	UINT32 lp_idx;
	if (find_index_of_lpn(lpn, &lp_idx)) {
		sectors_mask_t	lp_old_mask  = lp_masks[lp_idx];
		buf_id_t	old_buf_id   = buf_ids[lp_idx];
		sectors_mask_t 	old_buf_mask = buf_masks[old_buf_id];

		// Use old buffer if it has enough room
		if (((old_buf_mask & ~lp_old_mask) & lp_new_mask) == 0) {
			new_buf_id = old_buf_id;
		}
		// Otherwise
		else {
			// we need to find another buffer that fits both 
			// old & new data
			sectors_mask_t merged_mask = lp_old_mask | lp_new_mask;
			new_buf_id = allocate_buffer_for(merged_mask);

			// move useful part of old data to new buffer 
			sectors_mask_t old_useful_mask = 
				lp_old_mask & ~(lp_old_mask & lp_new_mask);
			copy_to_buffer(WRITE_BUF(new_buf_id),
				       WRITE_BUF(old_buf_id),
				       old_useful_mask);

			// update old buffer
			buf_masks[old_buf_id] &= ~lp_old_mask;
			buf_sizes[old_buf_id]  = count_sectors(
							buf_masks[old_buf_id]);
			if (buf_sizes[old_buf_id] == 0) 
				num_clean_buffers++;
		}
	
		lp_masks[lp_idx] |= lp_new_mask;
		buf_ids[lp_idx]   = new_buf_id;
	}
	// New lpn
	else {
		new_buf_id 	   = allocate_buffer_for(lp_new_mask);
		
		lp_idx	   	   = get_free_lpn_index();
		lpns[lp_idx]	   = lpn;
		lp_masks[lp_idx]   = lp_new_mask;
		buf_ids[lp_idx]    = new_buf_id;

		num_lpns++;
	}
		
	// Do insertion 
	copy_to_buffer(WRITE_BUF(new_buf_id),
		       buf,
		       lp_new_mask);
	buf_masks[new_buf_id] |= lp_new_mask;
	buf_sizes[new_buf_id]  = count_sectors(buf_masks[new_buf_id]);
	//DEBUG("write buffer", "buf address = %u", WRITE_BUF(new_buf_id));
	/*  uart_printf("lp_new_mask = ");uart_print_hex_64(lp_new_mask);
	uart_printf("buf_masks[new_buf_id] = ");uart_print_hex_64(buf_masks[new_buf_id]);
	uart_printf("buf_sizes[new_buf_id] = %u\r\n", buf_sizes[new_buf_id]);*/
}

/* ========================================================================= *
 * Public API 
 * ========================================================================= */

void write_buffer_init()
{
	BUG_ON("# of LPN slots must be a multiple of 4", MAX_NUM_LPNS % 4 != 0);
//	BUG_ON("# of write buffers must be a multiple of 4", NUM_WRITE_BUFFERS % 4 != 0);
	BUG_ON("# of write buffers is too large", NUM_WRITE_BUFFERS > 255);

	num_lpns      = 0;
	num_clean_buffers   = NUM_WRITE_BUFFERS;
	head_buf_id   = 0;

	mem_set_sram(lpns, 	  NULL_LPN, 	MAX_NUM_LPNS * sizeof(UINT32));
	mem_set_sram(lp_masks, 	  0, 		MAX_NUM_LPNS * sizeof(sectors_mask_t));
//	mem_set_sram(buf_ids,  	  0xFFFFFFFF, 	MAX_NUM_LPNS * sizeof(buf_id_t));

	UINT8 i = 0;
	for (i = 0; i < MAX_NUM_LPNS; i++) {
		buf_ids[i] = 0xFF;
	}

	mem_set_sram(buf_masks,   0, 		NUM_WRITE_BUFFERS * sizeof(sectors_mask_t));
//	mem_set_sram(buf_sizes,   0, 		NUM_WRITE_BUFFERS * sizeof(UINT8));
	for (i = 0; i < NUM_WRITE_BUFFERS; i++) {
		buf_sizes[i] = 0;
	}
}

void write_buffer_get(UINT32 const lspn, 
		      UINT8  const sector_offset_in_sp, 
		      UINT8  const num_sectors_in_sp, 
		      UINT32 *buf)
{
	BUG_ON("out of bound", sector_offset_in_sp + num_sectors_in_sp > 
			       SECTORS_PER_SUB_PAGE);

	*buf = NULL;

	UINT32	lpn		= lspn / SUB_PAGES_PER_PAGE;
	UINT32 	lpn_idx;
	// if the logical page is not in buffer, just quit 
	if(!find_index_of_lpn(lpn, &lpn_idx)) return;
	
	UINT8	base_offset 	= (lspn % SUB_PAGES_PER_PAGE) * SECTORS_PER_SUB_PAGE; 
	sectors_mask_t required_sectors_mask 
				= init_mask(base_offset + sector_offset_in_sp,
					    num_sectors_in_sp);
	sectors_mask_t lp_mask = lp_masks[lpn_idx];
	// if write buffer doesn't include any sectors wanted, just quit
	if ((required_sectors_mask & lp_mask) == 0) return;

	*buf = WRITE_BUF(buf_ids[lpn_idx]) + base_offset * BYTES_PER_SECTOR;
	// if some request sectors are not in buffer, we have to load them
	if ((required_sectors_mask & lp_mask) != required_sectors_mask) {
		fill_whole_sub_page(lspn, 
				    (UINT8)(lp_mask >> base_offset),
				    *buf);
		lp_masks[lpn_idx] |= (0xFF << base_offset);
	}
	//DEBUG("write buffer", "buf address = %u", *buf);
}

void write_buffer_put(UINT32 const lpn, 
		      UINT8  const sector_offset, 
		      UINT8  const num_sectors,
		      UINT32 const sata_wr_buf)
{
	if (num_sectors == 0) return;

	insert_and_merge_lpn(lpn, sector_offset, num_sectors, sata_wr_buf);

	// Flush buffer when needed
	if (num_lpns == MAX_NUM_LPNS || num_clean_buffers == 0)
		flush_buffer();
}


void write_buffer_drop(UINT32 const lpn)
{
	UINT32 lpn_idx;
	if(!find_index_of_lpn(lpn, &lpn_idx)) return;

	buf_id_t buf_id = buf_ids[lpn_idx];
	remove_lpn_by_index(lpn_idx);
	if (buf_sizes[buf_id] == 0) num_clean_buffers++;
}

// Debug
UINT8 begin_sector(sectors_mask_t const mask) {
	ASSERT(mask!=0);
	return _begin_sector(mask);
}

UINT8 end_sector(sectors_mask_t const mask) {
	ASSERT(mask!=0);
	return _end_sector(mask);
}
