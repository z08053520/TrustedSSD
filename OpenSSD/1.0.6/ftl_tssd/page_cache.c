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

/* #define DEBUG_PAGE_CACHE */
#ifdef DEBUG_PAGE_CACHE
	/* #define debug(format, ...)	uart_print(format, ##__VA_ARGS__) */
	#define debug(format, ...)	\
		do {			\
			if (show_debug_msg) uart_print(format, ##__VA_ARGS__);\
		} while(0)
#else
	#define debug(format, ...)
#endif

static const page_key_t	NULL_KEY = {.as_uint = 0xFFFFFFFF};
#define NULL_TIMESTAMP		0xFFFFFFFF
#define NULL_PAGE_IDX		NUM_PC_SUB_PAGES

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

static void handle_timestamp_overflow()
{
	/* find minimum timestamp */
	UINT32	min_timestamp_idx = mem_search_min_max(timestamps,
					sizeof(UINT32),
					NUM_PC_SUB_PAGES,
					MU_CMD_SEARCH_MIN_SRAM),
		min_timestamp = timestamps[min_timestamp_idx];
	/* update timestamps */
	UINT8 sp_i;
	for (sp_i = 0; sp_i < NUM_PC_SUB_PAGES; sp_i++) {
		if (timestamps[sp_i] == NULL_TIMESTAMP) continue;
		timestamps[sp_i] -= min_timestamp;
	}
}

static void update_timestamp(UINT32 const page_idx)
{
	/* update timestamp for LRU cache policy */
	timestamps[page_idx] = current_timestamp++;
	if (unlikely(current_timestamp == NULL_TIMESTAMP))
		handle_timestamp_overflow();
}

static UINT32 get_page(page_key_t const key)
{
	/* shortcuts for consecutive access of the same key */
	if (page_key_equal(last_key, key))
		return last_page_idx;

	UINT32 page_idx = mem_search_equ_sram(cached_keys,
					       sizeof(UINT32),
					       NUM_PC_SUB_PAGES, key.as_uint);
	if (page_idx >= NUM_PC_SUB_PAGES) return NULL_PAGE_IDX;

	last_key = key;
	last_page_idx = page_idx;

	/* update timestamp of page that is not evicted into merge buffer */
	if (timestamps[page_idx] != NULL_TIMESTAMP)
		update_timestamp(page_idx);

	return page_idx;
}

static void free_page(UINT32 const page_idx)  {
	BUG_ON("out of bounds", page_idx >= NUM_PC_SUB_PAGES);
	cached_keys[page_idx] = NULL_KEY;
	flags[page_idx] = 0;
	num_free_sub_pages++;

	if (last_page_idx == page_idx) {
		last_page_idx = NULL_PAGE_IDX;
		last_key = NULL_KEY;
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
	last_page_idx = NULL_PAGE_IDX;
}

BOOL8	page_cache_has (page_key_t const key)
{
	UINT32	page_idx = get_page(key);
	return page_idx != NULL_PAGE_IDX;
}

BOOL8 	page_cache_get (page_key_t const key,
			UINT32 *addr, BOOL8 const will_modify)
{
	debug("\t> page_cache_get(type = %s, idx = %u)",
		key.type == PAGE_TYPE_PMT ? "pmt" : "sot",
		key.idx);

	UINT32	page_idx = get_page(key);
	if (page_idx == NULL_PAGE_IDX) {
		debug("\t\t NOT found");
		*addr = NULL;
		return TRUE;
	}
	debug("\t\t found");

	*addr = PC_SUB_PAGE(page_idx);

	/* only update flag of page that is not evicted into merge buffer */
	if (timestamps[page_idx] != NULL_TIMESTAMP && will_modify)
		set_dirty(flags[page_idx]);
	return FALSE;
}

void	page_cache_put (page_key_t const key,
			UINT32 *buf, UINT8 const flag)
{
	BUG_ON("page cache is full", page_cache_is_full());

	debug("\t> page_cache_put(type = %s, idx = %u)",
		key.type == PAGE_TYPE_PMT ? "pmt" : "sot",
		key.idx);

	UINT32	old_page_idx = get_page(key);
	BUG_ON("page is already in cache", old_page_idx != NULL_PAGE_IDX);

	UINT32	free_page_idx = mem_search_equ_sram(cached_keys,
						   sizeof(UINT32),
						   NUM_PC_SUB_PAGES,
						   NULL_KEY.as_uint);
	BUG_ON("no free page is available even when not full",
		free_page_idx >= NUM_PC_SUB_PAGES);

	cached_keys[free_page_idx] = key;
	flags[free_page_idx]	   = flag;
	update_timestamp(free_page_idx);

	*buf = PC_SUB_PAGE(free_page_idx);

	last_key = key;
	last_page_idx = free_page_idx;

	num_free_sub_pages--;
}

BOOL8	page_cache_get_flag(page_key_t const key, UINT8 *flag)
{
	UINT32	page_idx = get_page(key);
	if (page_idx == NULL_PAGE_IDX) return TRUE;
	*flag = flags[page_idx];
	return FALSE;
}

BOOL8	page_cache_set_flag(page_key_t const key, UINT8 const flag)
{
	UINT32	page_idx = get_page(key);
	if (page_idx == NULL_PAGE_IDX) return TRUE;
	flags[page_idx] = flag;
	return FALSE;
}

BOOL8	page_cache_is_full(void)
{
	return num_free_sub_pages == 0;
}

static void dump_state()
{
	if (show_debug_msg) {
		uart_print("free sub pages == %u, current timestamp == %u",
				num_free_sub_pages, current_timestamp);

		UINT8 i, size = NUM_PC_SUB_PAGES;

		uart_printf("cached_keys = [\r\n\t");
		for (i = 0; i < size; i++) {
			if (i) uart_printf(", ");
			uart_printf("<%s:%u>",
				cached_keys[i].type == PAGE_TYPE_PMT ?
					"PMT" : "SOT",
				cached_keys[i].idx);
		}
		uart_print("]");

		uart_printf("timestamps = [\r\n\t");
		for (i = 0; i < size; i++) {
			if (i) uart_printf(", ");
			uart_printf("%u", timestamps[i]);
		}
		uart_print("]");

		uart_printf("flags = [\r\n\t");
		for (i = 0; i < size; i++) {
			if (i) uart_printf(", ");
			uart_printf("%u", flags[i]);
		}
		uart_print("]");
	}
}

BOOL8	page_cache_evict()
{
	BUG_ON("no pages to evict", num_free_sub_pages == NUM_PC_SUB_PAGES);

	debug("\t> page_cache_evict");

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
		if (is_reserved(flags[lru_page_idx])) {
			dump_state();
			return NEED_BLOCK;
		}

		debug("\t\t evicted! now there are %u pages to be merged",
			to_be_merged_pages+1);

		timestamps[lru_page_idx]  = NULL_TIMESTAMP;
		if (!is_dirty(flags[lru_page_idx])) {
			free_page(lru_page_idx);
			debug(" not dirty\r\n");
			return FALSE;
		}
		debug(" not dirty\r\n");

		to_be_merged_page_indexes[to_be_merged_pages++] = lru_page_idx;
		/* uart_printf(" moved to merge buffer (%u/%u)\r\n", */
		/* 	    to_be_merged_pages, SUB_PAGES_PER_PAGE); */
	}
	debug("\t\t need flush!");
	return TRUE; /* need to flush merge buffer */
}

void	page_cache_flush(UINT32 const merge_buf,
			 page_key_t merged_keys[SUB_PAGES_PER_PAGE])
{
	BUG_ON("merge buffer is not full",
		to_be_merged_pages != SUB_PAGES_PER_PAGE);
	debug("\t> page_cache_flush");

	UINT8	sp_i;
	for (sp_i = 0; sp_i < SUB_PAGES_PER_PAGE; sp_i++) {
		UINT32	page_idx  = to_be_merged_page_indexes[sp_i];
		merged_keys[sp_i] = cached_keys[page_idx];
		mem_copy(merge_buf + sp_i * BYTES_PER_SUB_PAGE,
			 PC_SUB_PAGE(page_idx),
			 BYTES_PER_SUB_PAGE);
		free_page(page_idx);
	}
	to_be_merged_pages = 0;
}

task_res_t	page_cache_load(page_key_t const key)
{
	debug("\t> page_cache_load(type = %s, idx = %u)",
		key.type == PAGE_TYPE_PMT ? "pmt" : "sot",
		key.idx);
	if (page_cache_has(key)) {
		debug("\t\tthe page is in cache");
		UINT8 flag;
		page_cache_get_flag(key, &flag);
		return is_reserved(flag) ?
				/* loading */
				TASK_PAUSED :
				/* in cache */
				TASK_CONTINUED;
	}

	debug("\t\tthe page is NOT in page");
	/* Flush page cache */
	if (page_cache_is_full()) {
		debug("\t\tpage cache is full, need to evict");
		BOOL8 need_flush = page_cache_evict();
		if (need_flush == TRUE) {
			debug("\t\tneed to flush");
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
			debug("\t\tinsert pc flush task: res = %u", res);
			if (res == TASK_BLOCKED) return TASK_BLOCKED;
		}
		else if (need_flush == NEED_BLOCK) {
			debug("\t\tneed to block");
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
	debug("\t\tinsert pc load task: res = %u", res);
	return res == TASK_FINISHED ? TASK_CONTINUED : res;
}
