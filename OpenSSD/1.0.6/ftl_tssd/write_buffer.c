#include "write_buffer.h"
#include "dram.h"
#include "gc.h"
#include "mem_util.h"
#include "fla.h"
#include "buffer.h"
#include "fla.h"

/* ========================================================================= *
 * Macros, Data Structure and Gloal Variables
 * ========================================================================= */

/* #define DEBUG_WRITE_BUFFER */
#ifdef DEBUG_WRITE_BUFFER
	#define debug(format, ...)	uart_print(format, ##__VA_ARGS__)
#else
	#define debug(format, ...)
#endif

/* For each write buffer, two fields are mainted: mask and size. */
typedef UINT8 		buf_id_t;
#define NULL_BID	0xFF

static buf_id_t		head_buf_id;
static UINT32		num_clean_buffers;
static UINT8		buf_managed_ids[NUM_WRITE_BUFFERS]; /* buf id -->
							       managed buf id */
static sectors_mask_t 	buf_masks[NUM_WRITE_BUFFERS]; /* 1 - occupied; 0 - available */
static UINT8		buf_sizes[NUM_WRITE_BUFFERS];
#if OPTION_ACL
static user_id_t	buf_uids[NUM_WRITE_BUFFERS];
#endif

#define next_buf_id(buf_id)		(((buf_id) + 1) % NUM_WRITE_BUFFERS)

#define WRITE_BUF(buf_id)		MANAGED_BUF(buf_managed_ids[buf_id])

void allocate_buf(buf_id_t const buf_id)
{
	ASSERT(buf_managed_ids[buf_id] == NULL_BUF_ID);
	buf_managed_ids[buf_id] = buffer_allocate();
	num_clean_buffers--;
}

void free_buf(buf_id_t const buf_id)
{
	UINT8 buf_managed_id = buf_managed_ids[buf_id];
	buffer_free(buf_managed_id);
	buf_managed_ids[buf_id] = NULL_BUF_ID;
#if OPTOIN_ACL
	buf_uids[buf_id] = NULL_USER_ID;
#endif
	num_clean_buffers++;
}

/* For each logical page which has some part in write buffer, three fields are
 * maintained: lpn, valid sector mask and buffer id. */
#define MAX_NUM_LPNS			(SUB_PAGES_PER_PAGE * NUM_WRITE_BUFFERS)
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

	if (buf_sizes[buf_id] == 0) free_buf(buf_id);
}

#if OPTION_ACL
static BOOL8 next_index_of_lpn(UINT32 const lpn, UINT32 *lp_idx)
{
	static UINT32 next_lp_idx = 0;
	while (next_lp_idx < MAX_NUM_LPNS &&
		lpns[next_lp_idx] != lpn) next_lp_idx++;

	if (next_lp_idx == MAX_NUM_LPNS) {
		next_lp_idx = 0;
		return FALSE;
	}

	*lp_idx = next_lp_idx++;
	return TRUE;
}

static BOOL8 find_index_of_lpn_with_uid( UINT32 const lpn,
			user_id_t const uid, UINT32 *lp_idx)
{
	while (next_index_of_lpn(lpn, lp_idx)) {
		buf_id_t buf_id = lp_buf_ids[*lp_idx];
		if (buf_uids[buf_id] == uid) return TRUE;
	}
	return FALSE;
}
#endif

static BOOL8 find_index_of_lpn(UINT32 const lpn, UINT32 *lp_idx)
{
	UINT32 idx = mem_search_equ_sram(lpns, sizeof(UINT32),
					MAX_NUM_LPNS, lpn);
	if (idx >= MAX_NUM_LPNS) return FALSE;

	*lp_idx = idx;
	return TRUE;
}

static void remove_lp_by_index(UINT32 const lp_idx)
{
	BUG_ON("out of bound", lp_idx >= MAX_NUM_LPNS);

	sectors_mask_t mask     = lp_masks[lp_idx];
	buf_id_t bid     	= lp_buf_ids[lp_idx];
	BUG_ON("invalid bid", bid >= NUM_WRITE_BUFFERS);

	buf_mask_remove(bid, mask);

	lpns[lp_idx]  		= NULL_LPN;
	lp_masks[lp_idx] 	= 0;
	lp_buf_ids[lp_idx] 	= NULL_BID;

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


static void dump_state() __attribute__ ((unused));
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

static UINT8 allocate_buffer_for(sectors_mask_t const mask
#if OPTION_ACL
				,user_id_t const uid
#endif
				)
{
	buf_id_t new_buf_id = head_buf_id;
	do {
#if OPTION_ACL
		if (buf_masks[new_buf_id] == 0) {
			ASSERT(buf_uids[new_buf_id] == NULL_USER_ID);
			allocate_buf(new_buf_id);
			buf_uids[new_buf_id] = uid;
			return new_buf_id;
		}
		else if (buf_uids[new_buf_id] == uid &&
				(buf_masks[new_buf_id] & mask) == 0)
			return new_buf_id;
#else
		if ((buf_masks[new_buf_id] & mask) == 0) {
			if (buf_masks[new_buf_id] == 0)
				allocate_buf(new_buf_id);
			return new_buf_id;
		}
#endif
		new_buf_id = next_buf_id(new_buf_id);
	} while (new_buf_id != head_buf_id);
	ASSERT(0);
	return NULL_BID;
}

static UINT32 get_free_lp_index()
{
	UINT32 free_lp_idx;
	BOOL8  res = find_index_of_lpn(NULL_LPN, &free_lp_idx);
	ASSERT(res == TRUE);
	return free_lp_idx;
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
		buf_managed_ids[i] = NULL_BUF_ID;
#if OPTION_ACL
		buf_uids[i] = NULL_USER_ID;
#endif
	}
}

sectors_mask_t write_buffer_pull(UINT32 const lpn,
				sectors_mask_t const target_sectors,
#if OPTION_ACL
				user_id_t const uid,
#endif
				UINT32 const to_buf)
{
#if OPTION_ACL
	sectors_mask_t valid_sectors = 0;
	UINT32 lp_idx;
	while (next_index_of_lpn(lpn, &lp_idx)) {
		buf_id_t lp_buf_id	= lp_buf_ids[lp_idx];
		user_id_t buf_uid	= buf_uids[lp_buf_id];
		sectors_mask_t lp_valid_sectors = lp_masks[lp_idx];

		sectors_mask_t copied_sectors;
		UINT32 from_buf;
		if (buf_uid == uid) {
			copied_sectors = lp_valid_sectors & target_sectors;
			from_buf = WRITE_BUF(lp_buf_id);
		}
		else {
			/* the granularity of access control is sub-page */
			buf_mask_align_to_sp(&lp_valid_sectors);
			ASSERT((valid_sectors & lp_valid_sectors) == 0);
			copied_sectors = lp_valid_sectors & target_sectors;
			/* fill the sectors that cannot be authorized with 0s */
			from_buf = ALL_ZERO_BUF;
		}
		fla_copy_buffer(to_buf, from_buf, copied_sectors);
		valid_sectors |= copied_sectors;
	}
	return valid_sectors;
#else
	UINT32 lp_idx;
	if (!find_index_of_lpn(lpn, &lp_idx)) return 0;

	UINT32 from_buf = WRITE_BUF(lp_buf_ids[lp_idx]);
	sectors_mask_t valid_sectors = lp_masks[lp_idx];
	fla_copy_buffer(to_buf, from_buf, valid_sectors);
	return valid_sectors;
#endif
}

void write_buffer_push(UINT32 const lpn,
		      UINT8  const sector_offset,
		      UINT8  const num_sectors,
#if OPTION_ACL
		      user_id_t const uid,
#endif
		      UINT32 const from_buf)
{
	if (num_sectors == 0) return;
	ASSERT(!write_buffer_is_full());

	UINT32 lp_idx;
	sectors_mask_t  lp_new_mask = init_mask(sector_offset, num_sectors);
#if OPTION_ACL
	/* access control works on sub-page granualarity */
	sectors_mask_t	lp_new_mask_align_to_sp = lp_new_mask;
	buf_mask_align_to_sp(&lp_new_mask_align_to_sp);

	/* remove common part of this page from other users' buffers */
	while (next_index_of_lpn(lpn, &lp_idx)) {
		buf_id_t lp_buf_id = lp_buf_ids[lp_idx];
		user_id_t buf_uid = buf_uids[lp_buf_id];
		if (buf_uid == uid) continue;

		sectors_mask_t	lp_old_mask_other_usr = lp_masks[lp_idx];
		sectors_mask_t	removal_common_mask =
					lp_new_mask_align_to_sp &
					lp_old_mask_other_usr;
		sectors_mask_t	lp_new_mask_other_usr =
					lp_old_mask_other_usr &
					~(removal_common_mask);
		if (lp_new_mask_other_usr) {
			lp_masks[lp_idx] = lp_new_mask_other_usr;
			buf_mask_remove(lp_buf_id, removal_common_mask);
		}
		else {
			remove_lp_by_index(lp_idx);
		}
	}
#endif
	// Try to merge with the same lpn in the buffer
	buf_id_t	new_buf_id  = NULL_BID;
#if OPTION_ACL
	if (find_index_of_lpn_with_uid(lpn, uid, &lp_idx)) {
#else
	if (find_index_of_lpn(lpn, &lp_idx)) {
#endif
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
#if OPTION_ACL
			new_buf_id = allocate_buffer_for(merged_mask, uid);
#else
			new_buf_id = allocate_buffer_for(merged_mask);
#endif

			// move useful part of old data to new buffer
			sectors_mask_t old_useful_mask = lp_old_mask & rvs_common_mask;
			fla_copy_buffer(WRITE_BUF(new_buf_id),
				       WRITE_BUF(old_buf_id),
				       old_useful_mask);

			// update old buffer
			buf_mask_remove(old_buf_id, lp_old_mask);

			// Update new buffer
			buf_mask_add(new_buf_id, old_useful_mask);
		}

		lp_masks[lp_idx]    |= lp_new_mask;
		lp_buf_ids[lp_idx]   = new_buf_id;
	}
	// New lpn
	else {
#if OPTION_ACL
		new_buf_id 	   = allocate_buffer_for(lp_new_mask, uid);
#else
		new_buf_id 	   = allocate_buffer_for(lp_new_mask);
#endif

		lp_idx	   	   = get_free_lp_index();
		lpns[lp_idx]	   = lpn;
		lp_masks[lp_idx]   = lp_new_mask;
		lp_buf_ids[lp_idx] = new_buf_id;

		num_lpns++;
	}
	// Do insertion
	fla_copy_buffer(WRITE_BUF(new_buf_id), from_buf, lp_new_mask);
	buf_mask_add(new_buf_id, lp_new_mask);
}

BOOL8 write_buffer_is_full()
{
	return num_lpns == MAX_NUM_LPNS || num_clean_buffers == 0;
}

void write_buffer_drop(UINT32 const lpn)
{
	UINT32 lp_idx;
#if OPTION_ACL
	while (next_index_of_lpn(lpn, &lp_idx))
		remove_lp_by_index(lp_idx);
#else
	if(!find_index_of_lpn(lpn, &lp_idx)) return;
	remove_lp_by_index(lp_idx);
#endif
}

void write_buffer_flush(UINT8 *flushed_buf_id,
			sectors_mask_t *valid_sectors,
#if OPTION_ACL
			user_id_t *uid,
#endif
			UINT32 sp_lpn[SUB_PAGES_PER_PAGE])
{
	/* find a vicitim buffer */
	buf_id_t buf_id = find_fullest_buffer();
	ASSERT(buf_sizes[buf_id] > 0);
	ASSERT(buf_id < NUM_WRITE_BUFFERS);

	ASSERT(buf_managed_ids[buf_id] != NULL_BUF_ID);
	*flushed_buf_id = buf_managed_ids[buf_id];
#if OPION_ACL
	*uid = buf_uids[buf_id];
#endif

	*valid_sectors 	= 0;
	mem_set_sram(sp_lpn, NULL_LPN, sizeof(UINT32) * SUB_PAGES_PER_PAGE);

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
		for (UINT8 sp_i = begin_sp; sp_i < end_sp; sp_i++) {
			UINT8	lsp_mask = (lp_mask >>
						(SECTORS_PER_SUB_PAGE * sp_i));
			if (lsp_mask) sp_lpn[sp_i] = lpn;
		}

		// remove the lpn from buffer
		remove_lp_by_index(lpn_i);

		lpn_i++;
	}
	ASSERT(buf_masks[buf_id] == 0ULL);
	ASSERT(buf_sizes[buf_id] == 0);
	ASSERT(buf_managed_ids[buf_id] == NULL_BUF_ID);
#if OPTION_ACL
	ASSERT(buf_uids[buf_id] = NULL_USER_ID);
#endif
	ASSERT(num_clean_buffers > 0);

	if (buf_id == head_buf_id)
		head_buf_id = next_buf_id(head_buf_id);
}

// Debug
UINT8 begin_sector(sectors_mask_t const mask) {
	BUG_ON("mask should not be empty", mask==0);
	return _begin_sector(mask);
}

UINT8 end_sector(sectors_mask_t const mask) {
	BUG_ON("mask should not be empty", mask==0);
	return _end_sector(mask);
}
