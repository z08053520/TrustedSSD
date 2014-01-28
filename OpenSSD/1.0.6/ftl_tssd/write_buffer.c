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
static buf_id_t		lp_buf_ids[MAX_NUM_LPNS];


/* ========================================================================= *
 * Private Functions 
 * ========================================================================= */

static void buf_mask_align_to_sp(sectors_mask_t *mask)
{
	/* There are 8 sectors in a sub page */
	sectors_mask_t sp_mask = 0xFF;
	while (sp_mask) {
		if (*mask & sp_mask) *mask |= sp_mask;

		sp_mask = sp_mask << SECTORS_PER_SUB_PAGE;
	}
}

static void buf_mask_add(UINT32 const buf_id, 
			 sectors_mask_t const lp_mask)
{
	sectors_mask_t lp_mask_aligned = lp_mask;
	buf_mask_align_to_sp(&lp_mask_aligned);	

	buf_masks[buf_id] |= lp_mask_aligned;
	buf_sizes[buf_id] = count_sectors(buf_masks[buf_id]);
}

static void buf_mask_remove(UINT32 const buf_id, 
			    sectors_mask_t const lp_mask)
{
	sectors_mask_t lp_mask_aligned = lp_mask;
	buf_mask_align_to_sp(&lp_mask_aligned);	

	buf_masks[buf_id] &= ~lp_mask_aligned;
	buf_sizes[buf_id] = count_sectors(buf_masks[buf_id]);
}

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
	buf_id_t bid     	= lp_buf_ids[lpn_idx];
	BUG_ON("invalid bid", bid >= NUM_WRITE_BUFFERS);

	buf_mask_remove(bid, mask);

	lpns[lpn_idx]  		= NULL_LPN;
	lp_masks[lpn_idx] 	= 0;
	lp_buf_ids[lpn_idx] 	= NULL_BID;

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
	
	return max_buf_id;
}
 

static void dump_state()
{
	UINT8 i = 0;

	uart_print("");
	DEBUG("write buffer", "=========== buffer state ========");
	
	uart_printf("head_buf_id = %u, # clean buffers = %u\r\n", 
		    head_buf_id, num_clean_buffers);

	uart_print("buf_masks=[");
	for(i = 0; i < NUM_WRITE_BUFFERS;i++) {
		uart_print_hex_64(buf_masks[i]);
		if (i != NUM_WRITE_BUFFERS-1) uart_printf(", ");
	}
	uart_print("]");

	uart_printf("buf_sizes=[");
	for(i = 0; i < NUM_WRITE_BUFFERS;i++) {
		if (i) uart_printf(", ");
		uart_printf("%u", buf_sizes[i]);
	}
	uart_print("]");

	uart_printf("lpns (%u in total):\r\n", num_lpns);
	for(i = 0; i < MAX_NUM_LPNS; i++) {
		if (lpns[i] == NULL_LPN) continue;

		UINT8 lp_buf_id = lp_buf_ids[i];
		uart_printf("[%u] lpn = %u, buf_id = %u, mask = ", 
			    i, lpns[i], lp_buf_id);
		uart_print_hex_64(lp_masks[i]);

		UINT8 first_sector = begin_sector(lp_masks[i]),
		      last_sector  = end_sector(lp_masks[i]) - 1;
		uart_printf("\tsector %u (%u) to sector %u (%u)\r\n",
				first_sector,
				read_dram_32(WRITE_BUF(lp_buf_id) + first_sector * BYTES_PER_SECTOR),
				last_sector,
				read_dram_32(WRITE_BUF(lp_buf_id) + last_sector * BYTES_PER_SECTOR));
	}
	uart_print("");
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

static UINT32 get_free_lpn_index()
{
	UINT32 free_lpn_idx;
	BOOL8  res = find_index_of_lpn(NULL_LPN, &free_lpn_idx);
	BUG_ON("no available slot for new lpn", res == FALSE);
	return free_lpn_idx;
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
//	mem_set_sram(lp_buf_ids,  	  0xFFFFFFFF, 	MAX_NUM_LPNS * sizeof(buf_id_t));

	UINT8 i = 0;
	for (i = 0; i < MAX_NUM_LPNS; i++) {
		lp_buf_ids[i] = 0xFF;
	}

	mem_set_sram(buf_masks,   0, 		NUM_WRITE_BUFFERS * sizeof(sectors_mask_t));
//	mem_set_sram(buf_sizes,   0, 		NUM_WRITE_BUFFERS * sizeof(UINT8));
	for (i = 0; i < NUM_WRITE_BUFFERS; i++) {
		buf_sizes[i] = 0;
	}
}

void write_buffer_get(UINT32 const lpn, 
		      UINT32 *buf, 
		      sectors_mask_t *valid_sectors)
{
	UINT32 	lpn_idx;
	if(!find_index_of_lpn(lpn, &lpn_idx)) {
		*buf = NULL;
		return;
	}

	*buf = WRITE_BUF(lp_buf_ids[lpn_idx]);
	*valid_sectors = lp_masks[lpn_idx];
}

void write_buffer_put(UINT32 const lpn, 
		      UINT8  const sector_offset, 
		      UINT8  const num_sectors,
		      UINT32 const buf)
{
	if (num_sectors == 0) return;
	BUG_ON("buffer is full!", write_buffer_is_full());

	sectors_mask_t  lp_new_mask = init_mask(sector_offset, num_sectors);
	buf_id_t	new_buf_id  = NULL_BID;	

	// DEBUG
	/* uart_printf("lpn = %u, offset = %u, num_sectors = %u\r\n", lpn, sector_offset, num_sectors); */
	/* uart_printf("lp_new_mask = ");uart_print_hex_64(lp_new_mask); */

	// Try to merge with the same lpn in the buffer 
	UINT32 lp_idx;
	if (find_index_of_lpn(lpn, &lp_idx)) {
		sectors_mask_t	lp_old_mask  = lp_masks[lp_idx];
		buf_id_t	old_buf_id   = lp_buf_ids[lp_idx];
		sectors_mask_t 	old_buf_mask = buf_masks[old_buf_id];

		sectors_mask_t  rvs_common_mask = ~(lp_old_mask & lp_new_mask);
		sectors_mask_t  new_useful_mask = lp_new_mask & rvs_common_mask;

		// Use old buffer if it has enough room
		if ((old_buf_mask & new_useful_mask) == 0) {
			new_buf_id = old_buf_id;
		}
		// Otherwise
		else {
			// we need to find another buffer that fits both 
			// old & new data
			sectors_mask_t merged_mask = lp_old_mask | lp_new_mask;
			new_buf_id = allocate_buffer_for(merged_mask);

			// move useful part of old data to new buffer 
			sectors_mask_t old_useful_mask = lp_old_mask & rvs_common_mask;
			fu_copy_buffer(WRITE_BUF(new_buf_id),
				       WRITE_BUF(old_buf_id),
				       old_useful_mask);

			// update old buffer
			buf_mask_remove(old_buf_id, lp_old_mask);
			if (buf_sizes[old_buf_id] == 0) 
				num_clean_buffers++;

			// Update new buffer
			buf_mask_add(new_buf_id, old_useful_mask);
		}
	
		lp_masks[lp_idx]    |= lp_new_mask;
		lp_buf_ids[lp_idx]   = new_buf_id;
	}
	// New lpn
	else {
		new_buf_id 	   = allocate_buffer_for(lp_new_mask);
		
		lp_idx	   	   = get_free_lpn_index();
		lpns[lp_idx]	   = lpn;
		lp_masks[lp_idx]   = lp_new_mask;
		lp_buf_ids[lp_idx] = new_buf_id;

		num_lpns++;
	}
		
	// Do insertion 
	fu_copy_buffer(WRITE_BUF(new_buf_id),
		       buf,
		       lp_new_mask);
	buf_mask_add(new_buf_id, lp_new_mask);
	/* buf_masks[new_buf_id] |= lp_new_mask; */
	/* buf_sizes[new_buf_id]  = count_sectors(buf_masks[new_buf_id]); */

	/* DEBUG("write buffer", "buf address = %u", WRITE_BUF(new_buf_id)); */
	/* uart_printf("lp_new_mask = ");uart_print_hex_64(lp_new_mask); */
	/* uart_printf("buf_masks[new_buf_id] = ");uart_print_hex_64(buf_masks[new_buf_id]); */
	/* uart_printf("buf_sizes[new_buf_id] = %u\r\n", buf_sizes[new_buf_id]); */

	/* uart_print("after put"); */
	/* dump_state(); */
}


#if OPTION_FTL_TEST
static BOOL8	single_buffer_mode = FALSE;
void write_buffer_set_mode(BOOL8 const use_single_buffer)
{
	single_buffer_mode = use_single_buffer;
}
#endif

BOOL8 write_buffer_is_full()
{
#if OPTION_FTL_TEST
	return 	(num_lpns == MAX_NUM_LPNS) || 
		(single_buffer_mode  && (num_clean_buffers < NUM_WRITE_BUFFERS)) || 
		(!single_buffer_mode && (num_clean_buffers == 0));
#else
	return num_lpns == MAX_NUM_LPNS || num_clean_buffers == 0;
#endif
}

void write_buffer_drop(UINT32 const lpn)
{
	UINT32 lpn_idx;
	if(!find_index_of_lpn(lpn, &lpn_idx)) return;

	buf_id_t buf_id = lp_buf_ids[lpn_idx];
	remove_lpn_by_index(lpn_idx);
	
	if (buf_sizes[buf_id] == 0) num_clean_buffers++;
}

#define begin_subpage(mask)	(begin_sector(mask) / SECTORS_PER_SUB_PAGE)
#define end_subpage(mask)	COUNT_BUCKETS(end_sector(mask), SECTORS_PER_SUB_PAGE)

void write_buffer_flush(UINT32 const buf, UINT32 *lspns, 
			sectors_mask_t *valid_sectors)
{
	/* find a vicitim buffer */
	buf_id_t buf_id = find_fullest_buffer();
	BUG_ON("flush any empty/invliad  buffer", buf_sizes[buf_id] == 0 ||
						  buf_id >= NUM_WRITE_BUFFERS);
	*valid_sectors 	= 0;

	mem_set_sram(lspns, NULL_LSPN, sizeof(UINT32) * SUB_PAGES_PER_PAGE);

	UINT32  lpn_i   = 0;
	// Iterate each lpn in the victim buffer
	while (lpn_i < MAX_NUM_LPNS)  {
		if (lp_buf_ids[lpn_i] != buf_id) {
			lpn_i++;
			continue;
		}

		UINT32 		lpn 	= lpns[lpn_i];
		sectors_mask_t 	lp_mask = lp_masks[lpn_i];

		*valid_sectors |= lp_mask;

		UINT8  begin_sp	   = begin_subpage(lp_mask),
		       end_sp	   = end_subpage(lp_mask);
		BUG_ON("empty sub page", begin_sp == end_sp);
		UINT8  sp_offset   = begin_sp;
		UINT32 lspn_base   = lpn * SUB_PAGES_PER_PAGE; 
		UINT32 lspn	   = lspn_base + begin_sp,
		       end_lspn	   = lspn_base + end_sp;
		while (lspn < end_lspn) {
			UINT8	lsp_mask = (lp_mask >> (SECTORS_PER_SUB_PAGE * sp_offset));
		
			if (lsp_mask) lspns[sp_offset] = lspn;	
			
			lspn++;
			sp_offset++;
		}
		
		// remove the lpn from buffer
		remove_lpn_by_index(lpn_i);	

		lpn_i++;
	}

	// Copy buffer
	UINT32 offset 	   	= begin_sector(*valid_sectors),
	       num_sectors 	= end_sector(*valid_sectors) - offset;
	mem_copy(buf + offset * BYTES_PER_SECTOR, 
		 WRITE_BUF(buf_id) + offset * BYTES_PER_SECTOR,
		 num_sectors * BYTES_PER_SECTOR);

	// Remove buffer
	BUG_ON("buf mask is not cleared", buf_masks[buf_id] != 0ULL);
	BUG_ON("buf size is not zero", buf_sizes[buf_id] != 0);
	num_clean_buffers++;
	if (buf_id == head_buf_id) 
		head_buf_id = next_buf_id(head_buf_id);
	
	/* uart_print("after flush"); */
	/* dump_state(); */
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
