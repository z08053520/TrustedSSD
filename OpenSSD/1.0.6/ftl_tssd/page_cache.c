#include "page_cache.h"
#include "gc.h"
#include "gtd.h"
#include "dram.h"
#include "flash_util.h"
#include "page_cache_load_task.h"
#include "page_cache_flush_task.h"

/* ========================================================================= *
 * Macros, Data Structure and Gloal Variables 
 * ========================================================================= */

/* * 
 * Bit arragement for key of different page types 
 *	Type	    31  30  29 ... 0	(29 bits for index)
 *	-----------------------------
 * 	PMT	--  1   0    [any]
 * 	SOT	--  1   1    [any]
 * */
/* #define UINT32_BITS		(sizeof(UINT32) * 8) */
/* #define KEY_PMT_BASE		(1 << (UINT32_BITS - 1)) */
/* #define idx2key(idx, type)	(idx | KEY_PMT_BASE) */
/* #define key2idx(key) 		(key - KEY_PMT_BASE) */
/* #define key2type(key)		(key >= KEY_PMT_BASE ? PC_BUF_TYPE_PMT : PC_BUF_TYPE_NUL) */

/* #if OPTION_ACL */
/* #define KEY_SOT_BASE		(KEY_PMT_BASE | (1 << (UINT32_BITS - 2))) */
/* #define idx2key(key, type)	(type == PC_BUF_TYPE_PMT ? key | KEY_PMT_BASE :\ */
/* 							   key | KEY_SOT_BASE) */
/* #define key2idx(key) 		(key >= KEY_SOT_BASE ? key - KEY_SOT_BASE : \ */
/* 						       key - KEY_PMT_BASE) */
/* #define key2type(key)		(key >= KEY_SOT_BASE ? PC_BUF_TYPE_SOT : \ */
/* 					(key >= KEY_PMT_BASE ? 		 \ */
/* 					 	PC_BUF_TYPE_PMT : 	 \ */
/* 					 	PC_BUF_TYPE_NUL)) */
/* #endif */

static const page_key_t	NULL_KEY = {.as_uint = 0xFFFFFFFF};
#define NULL_TIMESTAMP		0xFFFFFFFF 

/* For each cached sub page, we record **key**, **timestamp** and **flag** */
static page_key_t cached_keys[NUM_PC_SUB_PAGES];
static UINT32 	timestamps[NUM_PC_SUB_PAGES];
static UINT8	flags[NUM_PC_SUB_PAGES];	

static UINT32 	num_free_sub_pages = NUM_PC_SUB_PAGES;
static UINT32 	current_timestamp  = 0;

#if	OPTION_PERF_TUNING
	UINT32 g_pmt_cache_miss_count = 0;
#if	OPTION_ACL
	UINT32 g_sot_cache_miss_count = 0;
#endif
#endif

/* merge buffer */	
static UINT8  	to_be_merged_pages = 0;
static UINT32 	to_be_merged_page_indexes[SUB_PAGES_PER_PAGE];

/* Optimize for visiting one PMT/SOT page repeatedly*/
static page_key_t	last_key;
static UINT32 		last_page_idx;

/* ========================================================================= *
 * Private Functions 
 * ========================================================================= */

static UINT32	find_page(page_key_t const key)
{
	// try to find cached page given key
	UINT32 page_idx; 
	if (page_key_equal(last_key, key))
		page_idx = last_page_idx;
	else {
		page_idx = mem_search_equ_sram(cached_keys, 
					       sizeof(UINT32), 
					       NUM_PC_SUB_PAGES, key.as_uint);
		if (page_idx >= NUM_PC_SUB_PAGES) return NUM_PC_SUB_PAGES;
		last_key = key;
		last_page_idx = page_idx;
	}
	return page_idx;
}
/* static void flush_merge_buffer() */
/* { */
/* 	BUG_ON("merge buffer is not full", to_be_merged_pages != */ 
/* 					   SUB_PAGES_PER_PAGE); */

/* 	UINT8  bank = fu_get_idle_bank(); */
/* 	UINT32 vpn  = gc_allocate_new_vpn(bank); */
/* 	vp_t   vp   = {.bank = bank, .vpn  = vpn}; */
/* 	UINT32 vspn = vpn * SUB_PAGES_PER_PAGE; */
/* 	vsp_t  vsp  = {.bank = bank, .vspn = vspn}; */

/* 	// Iterate merged pages */
/* 	UINT8  i; */
/* 	for (i = 0; i < SUB_PAGES_PER_PAGE; i++, vsp.vspn++) { */
/* 		UINT32 page_idx = to_be_merged_page_indexes[i]; */
/* 		UINT32 key 	= cached_keys[page_idx]; */
/* 		UINT32 idx	= key2idx(key); */
/* 		pc_buf_type_t type = key2type(key); */

/* 		// Update GTD */
/* 		switch(type) { */
/* 		case PC_BUF_TYPE_PMT: */
/* 			gtd_set_vsp(idx, vsp, GTD_ZONE_TYPE_PMT); */
/* 			break; */
/* #if OPTION_ACL */
/* 		case PC_BUF_TYPE_SOT: */
/* 			gtd_set_vsp(idx, vsp, GTD_ZONE_TYPE_SOT); */
/* 			break; */
/* #endif */
/* 		case PC_BUF_TYPE_NUL: */
/* 			BUG_ON("impossible type", 1); */
/* 		} */

/* 		// Copy to one buffer before writing back to flash */
/* 		mem_copy(FTL_WR_BUF(bank) + i * BYTES_PER_SUB_PAGE, */
/* 			 PC_SUB_PAGE(page_idx), */
/* 			 BYTES_PER_SUB_PAGE); */

/* 		// Now this page buffer can be reused */
/* 		cached_keys[page_idx] = NULL_KEY; */	
/* 	} */

/* 	// DEBUG */
/* 	#warning permanently fix this issue */
/* 	BUG_ON("conflicting out of order flash writes", */ 
/* 		is_there_any_earlier_writing(vp)); */

/* 	// Write to flash */
/* 	fu_write_page(vp, FTL_WR_BUF(bank)); */

/* 	to_be_merged_pages = 0; */
/* 	num_free_sub_pages += SUB_PAGES_PER_PAGE; */
/* } */

/* static UINT32 evict_page() */
/* { */
/* 	while (to_be_merged_pages < SUB_PAGES_PER_PAGE) { */
/* 		// The LRU page is victim */
/* 		UINT32 lru_page_idx = mem_search_min_max( */
/* 						 timestamps, */ 
/* 						 sizeof(UINT32), */ 
/* 						 NUM_PC_SUB_PAGES, */
/* 						 MU_CMD_SEARCH_MIN_SRAM); */
/* 		BUG_ON("imposibble timestamp", timestamps[lru_page_idx] == NULL_TIMESTAMP); */
/* 		// Prevent this page from evicted again */	
/* 		timestamps[lru_page_idx]  = NULL_TIMESTAMP; */
/* 		// Evict a clean page and we are done */
/* 		if (!page_is_dirty(lru_page_idx)) { */
/* 			cached_keys[lru_page_idx] = NULL_KEY; */
/* 			num_free_sub_pages++; */

/* 			return lru_page_idx; */
/* 		} */
/* 		// Add this page to merge buffer */
/* 		to_be_merged_page_indexes[to_be_merged_pages] = lru_page_idx; */
/* 		to_be_merged_pages++; */
/* 	} */
	
/* 	UINT32 a_free_page_idx = to_be_merged_page_indexes[0]; */	
/* 	flush_merge_buffer(); */
/* 	return a_free_page_idx; */
/* } */

/* static UINT32 load_page(UINT32 const idx, pc_buf_type_t const type) */
/* { */
/* 	// find a free page */
/* 	UINT32 free_page_idx = num_free_sub_pages == 0 ? */
/* 					evict_page() : */
/* 					mem_search_equ_sram(cached_keys, */ 
/* 							    sizeof(UINT32), */
/* 							    NUM_PC_SUB_PAGES, */
/* 							    NULL_KEY); */
/* 	BUG_ON("free page not found after evicting", free_page_idx >= NUM_PC_SUB_PAGES); */

/* 	cached_keys[free_page_idx] = idx2key(idx, type); */
/* 	timestamps[free_page_idx]  = 0; */
/* 	page_reset_dirty(free_page_idx); */
/* 	num_free_sub_pages--; */

/* 	vsp_t vsp = get_vsp(idx, type); */
/* 	if (vsp.vspn) { */
/* 		fu_read_sub_page(vsp, PC_TEMP_BUF, FU_SYNC); */

/* 		UINT8 sect_offset = vsp.vspn % SUB_PAGES_PER_PAGE * SECTORS_PER_SUB_PAGE; */ 
/* 		mem_copy(PC_SUB_PAGE(free_page_idx), */
/* 			 PC_TEMP_BUF + sect_offset * BYTES_PER_SECTOR, */
/* 			 BYTES_PER_SUB_PAGE); */
/* 	} */
/* 	else { */
/* 		mem_set_dram(PC_SUB_PAGE(free_page_idx), 0, BYTES_PER_SUB_PAGE); */
/* 	} */
/* 	return free_page_idx; */
/* } */
static void handle_timestamp_overflow()
{
	mem_set_sram(timestamps, 0, sizeof(UINT32) * NUM_PC_SUB_PAGES);

	/* Restore the timestamps for to-be-merged pages */
	UINT8 sp_i;
	for (sp_i = 0; sp_i < to_be_merged_pages; sp_i++) {
		UINT8 merge_page_idx = to_be_merged_page_indexes[sp_i];
		timestamps[merge_page_idx] = NULL_TIMESTAMP;
	}
}

/* ========================================================================= *
 * Public Functions 
 * ========================================================================= */

void page_cache_init(void)
{
	BUG_ON("# of sub pages is not a multiple of 8", 
			NUM_PC_SUB_PAGES % SUB_PAGES_PER_PAGE != 0);
	///BUG_ON("Capacity too large", NUM_PC_SUB_PAGES > 255);

	UINT32 num_bytes = sizeof(UINT32) * NUM_PC_SUB_PAGES;
	mem_set_sram(cached_keys, NULL_KEY.as_uint, 	num_bytes);
	mem_set_sram(timestamps,  NULL_TIMESTAMP, 	num_bytes);

	UINT8 page_i = 0;
	for (page_i = 0; page_i < NUM_PC_SUB_PAGES; page_i++)
		flags[page_i] = 0;

	last_key = NULL_KEY;
	last_page_idx = SUB_PAGES_PER_PAGE;
}

BOOL8	page_cache_has (page_key_t const key)
{
	UINT32	page_idx = find_page(key);
	return page_idx >= NUM_PC_SUB_PAGES;
}

void 	page_cache_get (page_key_t const key,
			UINT32 *addr, BOOL8 const will_modify)
{
	UINT32	page_idx = find_page(key);
	BUG_ON("page not found in cache", page_idx >= NUM_PC_SUB_PAGES);

	*addr = PC_SUB_PAGE(page_idx);

	/* this page is evicted and in merge buffer */
	if (timestamps[page_idx] == NULL_TIMESTAMP) return;

	// set dirty if to be modified
	if (will_modify) set_dirty(flags[page_idx]);
	// update timestamp for LRU cache policy
	timestamps[page_idx] = current_timestamp++;
	if (unlikely(current_timestamp == NULL_TIMESTAMP)) 
		handle_timestamp_overflow();
}

void	page_cache_put (page_key_t const key,
			UINT32 *buf, UINT8 const flag)
{
	BUG_ON("page cache is full", page_cache_is_full());

	UINT32	page_idx = find_page(key);
	BUG_ON("page is already in cache", page_idx < NUM_PC_SUB_PAGES);

	UINT32	free_page_idx = mem_search_equ_sram(cached_keys, 
						   sizeof(UINT32),
						   NUM_PC_SUB_PAGES,
						   NULL_KEY.as_uint);
	BUG_ON("no free page is available even when not full", free_page_idx >= NUM_PC_SUB_PAGES);

	cached_keys[free_page_idx] = key;
	timestamps[free_page_idx]  = current_timestamp++;
	flags[free_page_idx]	   = flag;
	if (unlikely(current_timestamp == NULL_TIMESTAMP)) 
		handle_timestamp_overflow();	
	
	*buf = PC_SUB_PAGE(page_idx);

	num_free_sub_pages--;
}

BOOL8	page_cache_get_flag(page_key_t const key, UINT8 *flag)
{
	UINT32	page_idx = find_page(key);
	if (page_idx >= NUM_PC_SUB_PAGES) {
		*flag = 0;
		return 1;
	}
	*flag = flags[page_idx];	
	return 0;
}

BOOL8	page_cache_set_flag(page_key_t const key, UINT8 const flag)
{
	UINT32	page_idx = find_page(key);
	if (page_idx >= NUM_PC_SUB_PAGES) return 1;
	flags[page_idx] = flag;
	return 0;
}

BOOL8	page_cache_is_full(void)
{
	return num_free_sub_pages == 0;
}

BOOL8	page_cache_evict()
{
	BUG_ON("no pages to evict", num_free_sub_pages == NUM_PC_SUB_PAGES);

	while (to_be_merged_pages < SUB_PAGES_PER_PAGE) {
		// The LRU (Least Recently Used) page is victim
		UINT32 lru_page_idx = mem_search_min_max(
						 timestamps, 
						 sizeof(UINT32), 
						 NUM_PC_SUB_PAGES,
						 MU_CMD_SEARCH_MIN_SRAM);
		BUG_ON("imposibble timestamp", timestamps[lru_page_idx] == NULL_TIMESTAMP);

		/* TODO: handle this situation */
		BUG_ON("evicting reserved page", is_reserved(flags[lru_page_idx]));

		timestamps[lru_page_idx]  = NULL_TIMESTAMP;
		if (!is_dirty(flags[lru_page_idx])) {
			cached_keys[lru_page_idx] = NULL_KEY;
			flags[lru_page_idx] = 0;
			num_free_sub_pages++;
			return FALSE;
		}
		
		to_be_merged_page_indexes[to_be_merged_pages++] = lru_page_idx;
	}
	return TRUE; /* need to flush merge buffer */
}

void	page_cache_flush(UINT32 const merge_buf, 
			 page_key_t merged_keys[SUB_PAGES_PER_PAGE])
{
	BUG_ON("merge buffer is not full", 
		to_be_merged_pages != SUB_PAGES_PER_PAGE);

	UINT8	sp_i;
	for (sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++) {
		UINT32	page_idx  =  to_be_merged_page_indexes[sp_i];	
		merged_keys[sp_i] = cached_keys[page_idx];
		mem_copy(merge_buf + sp_i * BYTES_PER_SUB_PAGE,
			 PC_SUB_PAGE(page_idx),
			 BYTES_PER_SUB_PAGE);

		cached_keys[page_idx] = NULL_KEY;
		flags[page_idx]   = 0;
	}

	num_free_sub_pages += SUB_PAGES_PER_PAGE;
	to_be_merged_pages = 0;
}

task_res_t	page_cache_load(page_key_t const key)
{
	if (page_cache_has(key)) {
		UINT8 flag;
		page_cache_get_flag(key, &flag);
		return is_reserved(flag) ? 
				/* loading */
				TASK_PAUSED : 
				/* in cache */
				TASK_CONTINUED;	
	}

	if (!task_can_allocate(1)) return TASK_BLOCKED;

	/* Flush page cache */ 
	if (page_cache_is_full()) {
		BOOL8 need_flush = page_cache_evict();
		if (need_flush) {
			/* One load task plus one flush task */
			if (!task_can_allocate(2)) return TASK_BLOCKED;

			task_t	*pc_flush_task = task_allocate();
			page_cache_flush_task_init(pc_flush_task);
			task_res_t res = task_engine_insert_and_process(
						pc_flush_task);
			if (res == TASK_BLOCKED) return TASK_BLOCKED;
		}
	}

	/* Load missing page */
	task_t	*pc_load_task = task_allocate();
	page_cache_load_task_init(pc_load_task, key);
	task_res_t res = task_engine_insert_and_process(pc_load_task);
	return res == TASK_FINISHED ? TASK_CONTINUED : res;
}

/* void page_cache_load(UINT32 const idx, UINT32 *addr, */ 
/* 		     pc_buf_type_t const type, BOOL8 const will_modify) */
/* { */
/* 	BUG_ON("invalid type", type == PC_BUF_TYPE_NUL); */
	
/* 	// try to find cached page given key */
/* 	UINT32 key = idx2key(idx, type); */
/* 	UINT32 page_idx; */ 
/* 	if (last_key == key) */
/* 		page_idx = last_page_idx; */
/* 	else { */
/* 		page_idx = mem_search_equ_sram(cached_keys, */ 
/* 					       sizeof(UINT32), */ 
/* 					       NUM_PC_SUB_PAGES, key); */
/* 		// if not found, load the page into cache */
/* 		if (page_idx >= NUM_PC_SUB_PAGES) { */
/* #if	OPTION_PERF_TUNING */
/* 			if (type == PC_BUF_TYPE_PMT) g_pmt_cache_miss_count++; */
/* #if	OPTION_ACL */
/* 			else if (type == PC_BUF_TYPE_SOT) g_sot_cache_miss_count++; */
/* #endif */
/* #endif */
/* 			page_idx = load_page(idx, type); */
/* 		} */
/* 		last_key = key; */
/* 		last_page_idx = page_idx; */
/* 	} */
/* 	*addr = PC_SUB_PAGE(page_idx); */

/* 	// if this page is already in merge buffer, don't modify its state */ 
/* 	if (timestamps[page_idx] == NULL_TIMESTAMP) */ 
/* 		return; */	
	
/* 	// update timestamp for LRU cache policy */
/* 	timestamps[page_idx] = current_timestamp++; */
/* 	// handle timestamp overflow */
/* 	if (unlikely(current_timestamp == NULL_TIMESTAMP)) { */
/* 		mem_set_sram(timestamps, 0, sizeof(UINT32) * NUM_PC_SUB_PAGES); */
	
/* 		// we must keep the timestamp of pages to be merged as */
/* 		// NULL_TIMESTAMP to prevent them from merged again */
/* 		UINT8 merge_page_i = 0; */
/* 		while (merge_page_i < to_be_merged_pages) { */
/* 			UINT32 merge_page_idx = to_be_merged_page_indexes[merge_page_i]; */
/* 			timestamps[merge_page_idx] = NULL_TIMESTAMP; */

/* 			merge_page_i++; */
/* 		} */
/* 	} */

/* 	// set dirty if to be modified */
/* 	if (will_modify) page_set_dirty(page_idx); */
/* } */
