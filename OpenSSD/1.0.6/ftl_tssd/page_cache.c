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

static const page_key_t	NULL_KEY = {.as_uint = 0xFFFFFFFF};
#define NULL_TIMESTAMP		0xFFFFFFFF 

/* For each cached sub page, we record **key**, **timestamp** and **flag** */
static page_key_t cached_keys[NUM_PC_SUB_PAGES];
static UINT32 	timestamps[NUM_PC_SUB_PAGES];
static UINT8	flags[NUM_PC_SUB_PAGES];	

static UINT32 	num_free_sub_pages;
static UINT32 	current_timestamp;

#if	OPTION_PERF_TUNING
	UINT32 g_page_cache_flush_count = 0;
	UINT32 g_pmt_cache_miss_count = 0;
#if	OPTION_ACL
	UINT32 g_sot_cache_miss_count = 0;
#endif
#endif

/* merge buffer */	
static UINT8  	to_be_merged_pages;
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

	UINT32 page_i = 0;
	for (page_i = 0; page_i < NUM_PC_SUB_PAGES; page_i++)
		flags[page_i] = 0;

	num_free_sub_pages = NUM_PC_SUB_PAGES;
	current_timestamp  = 0;
	
	to_be_merged_pages = 0;

	last_key = NULL_KEY;
	last_page_idx = SUB_PAGES_PER_PAGE;
}

BOOL8	page_cache_has (page_key_t const key)
{
	UINT32	page_idx = find_page(key);
	return page_idx < NUM_PC_SUB_PAGES;
}

BOOL8 	page_cache_get (page_key_t const key,
			UINT32 *addr, BOOL8 const will_modify)
{
	UINT32	page_idx = find_page(key);
	if (page_idx >= NUM_PC_SUB_PAGES) {
		*addr = NULL;
		return TRUE;
	}

	*addr = PC_SUB_PAGE(page_idx);

	/* this page is evicted and in merge buffer */
	if (timestamps[page_idx] == NULL_TIMESTAMP) return FALSE;

	// set dirty if to be modified
	if (will_modify) set_dirty(flags[page_idx]);
	// update timestamp for LRU cache policy
	timestamps[page_idx] = current_timestamp++;
	if (unlikely(current_timestamp == NULL_TIMESTAMP)) 
		handle_timestamp_overflow();
	return FALSE;
}

void	page_cache_put (page_key_t const key,
			UINT32 *buf, UINT8 const flag)
{
	BUG_ON("page cache is full", page_cache_is_full());

	UINT32	old_page_idx = find_page(key);
	BUG_ON("page is already in cache", old_page_idx < NUM_PC_SUB_PAGES);

	UINT32	free_page_idx = mem_search_equ_sram(cached_keys, 
						   sizeof(UINT32),
						   NUM_PC_SUB_PAGES,
						   NULL_KEY.as_uint);
	BUG_ON("no free page is available even when not full", free_page_idx >= NUM_PC_SUB_PAGES);

	cached_keys[free_page_idx] = key;
	timestamps[free_page_idx]  = current_timestamp++;
	if (unlikely(current_timestamp == NULL_TIMESTAMP)) 
		handle_timestamp_overflow();	
	flags[free_page_idx]	   = flag;
	
	*buf = PC_SUB_PAGE(free_page_idx);

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
		/* uart_printf("evict page %u with tiemstamp %u and flag %u...", */
		/* 	    lru_page_idx, timestamps[lru_page_idx], */ 
		/* 	    flags[lru_page_idx]); */

		/* TODO: handle this situation more gracefully*/
		/* BUG_ON("evicting reserved page", is_reserved(flags[lru_page_idx])); */
		#define NEED_BLOCK	2
		if (is_reserved(flags[lru_page_idx])) return NEED_BLOCK;

		timestamps[lru_page_idx]  = NULL_TIMESTAMP;
		if (!is_dirty(flags[lru_page_idx])) {
			cached_keys[lru_page_idx] = NULL_KEY;
			flags[lru_page_idx] = 0;
			num_free_sub_pages++;
			/* uart_printf(" not dirty\r\n"); */
			return FALSE;
		}
		
		to_be_merged_page_indexes[to_be_merged_pages++] = lru_page_idx;
		/* uart_printf(" moved to merge buffer (%u/%u)\r\n", */ 
		/* 	    to_be_merged_pages, SUB_PAGES_PER_PAGE); */
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
	/* uart_printf("page_cache_load(%u)\r\n", key); */
	if (page_cache_has(key)) {
	/* uart_print("here2"); */
		UINT8 flag;
		page_cache_get_flag(key, &flag);
		return is_reserved(flag) ? 
				/* loading */
				TASK_PAUSED : 
				/* in cache */
				TASK_CONTINUED;	
	}

	/* uart_print("here3"); */
	/* Flush page cache */ 
	if (page_cache_is_full()) {
	/* uart_print("here4"); */
		BOOL8 need_flush = page_cache_evict();
		if (need_flush == TRUE) {
			/* uart_print("try flush task"); */
			/* One load task plus one flush task */
			if (!task_can_allocate(1)) return TASK_BLOCKED;

			/* uart_print("flush task"); */
#if	OPTION_PERF_TUNING
			g_page_cache_flush_count += 1;
#endif

			task_t	*pc_flush_task = task_allocate();
			page_cache_flush_task_init(pc_flush_task);
			task_res_t res = task_engine_insert_and_process(
						pc_flush_task);
			if (res == TASK_BLOCKED) return TASK_BLOCKED;
		}
		else if (need_flush == NEED_BLOCK) {
			return TASK_BLOCKED;
		}
	}
	
	/* uart_print("here5"); */
	if (!task_can_allocate(1)) return TASK_BLOCKED;

	/* uart_print("here6"); */
#if	OPTION_PERF_TUNING
	if (key.type == PAGE_TYPE_PMT) g_pmt_cache_miss_count++;
#if	OPTION_ACL
	else if (key.type == PAGE_TYPE_SOT) g_sot_cache_miss_count++;
#endif
#endif
	/* Load missing page */
	task_t	*pc_load_task = task_allocate();
	page_cache_load_task_init(pc_load_task, key);
	task_res_t res = task_engine_insert_and_process(pc_load_task);
	return res == TASK_FINISHED ? TASK_CONTINUED : res;
}
