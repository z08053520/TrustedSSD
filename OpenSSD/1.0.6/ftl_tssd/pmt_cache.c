#include "pmt_cache.h"
#include "mem_util.h"
#include "dram.h"
#include "bit_array.h"

#define EVICTED_TIMESTAMP	0xFFFFFFFF
#define FIXED_TIMESTAMP		0xFFFFFFFE
#define LOADING_TIMESTAMP	0xFFFFFFFD
#define NULL_TIMESTAMP		0xFFFFFFFC

#define NULL_PAGE_IDX		NUM_PC_SUB_PAGES
#define NULL_PMT_IDX		0xFFFFFFFF

/* For each cached sub page, we record **pmt_idx**, and **timestamp**.
 *
 * There are four different entries in cache:
 *
 *	Free entries (timestamp == NULL_TIMESTAMP, index = NULL_PMT_IDX),
 *	Loading entries (timestamp == LOADING_TIMESTAMP, index < NULL_PMT_IDX).
 *	Normal entries (timestamp < NULL_TIMESTAMP, index < NULL_PMT_IDX),
 *	Fixed entries (timestamp == FIXED_TIMESTAMP, index < NULL_PMT_IDX).
 *	Evicted entries (timestamp == EVICTED_TIMESTAMP, index < NULL_PMT_IDX).
 *
 * Only normal entries can be evicted.
 * */
static UINT32	cached_pmt_idxes[NUM_PC_SUB_PAGES] = {
	[0 ... (NUM_PC_SUB_PAGES-1)] = NULL_PMT_IDX
};
static UINT32	cached_pmt_timestamps[NUM_PC_SUB_PAGES] = {
	[0 ... (NUM_PC_SUB_PAGES-1)] = NULL_TIMESTAMP
};
static DECLARE_BIT_ARRAY(cached_pmt_is_dirty, NUM_PC_SUB_PAGES);
static UINT8	cached_pmt_fix_count[NUM_PC_SUB_PAGES] = {0};

static UINT32	num_free_sub_pages = NUM_PC_SUB_PAGES;
static UINT32	current_timestamp = 0;

/* Optimization for visiting same PMT page repeatedly*/
static UINT32	last_pmt_idx = NULL_PMT_IDX;
static UINT32 	last_page_idx = NULL_PAGE_IDX;

/* ========================================================================= *
 *  Private Interface
 * ========================================================================= */

static void handle_timestamp_overflow()
{
	uart_print("warning: pmt cache timestamp overflowed!");

	/* reassign timestamp by order */
	UINT32 timestamp_low_bound;
	UINT32 min_timestamp;
	/* update the timestamp of potentially all page */
	for (UINT32 i = 0; i < NUM_PC_SUB_PAGES; i++) {
		timestamp_low_bound = i;
		min_timestamp = NULL_TIMESTAMP;
		/* find the i-th minimum timestamp */
		for (UINT32 j = 0; j < NUM_PC_SUB_PAGES; j++) {
			UINT32 j_timestamp = cached_pmt_timestamps[j];
			if (j_timestamp < timestamp_low_bound) continue;
			if (j_timestamp >= min_timestamp) continue;
			min_timestamp = j_timestamp;
		}
		if (min_timestamp == NULL_TIMESTAMP) break;

		if (min_timestamp == timestamp_low_bound) continue;

		/* i-th minimum timestamp is set to i */
		for (UINT32 k = 0; k < NUM_PC_SUB_PAGES; k++)
			if (cached_pmt_timestamps[k] == min_timestamp)
				cached_pmt_timestamps[k] = timestamp_low_bound;
	}
	/* count timestamp from minnimal */
	current_timestamp = ++timestamp_low_bound;
}

static inline void update_timestamp(UINT32 const page_idx)
{
	/* update timestamp for LRU cache policy */
	cached_pmt_timestamps[page_idx] = current_timestamp++;
	if (unlikely(current_timestamp >= NULL_TIMESTAMP))
		handle_timestamp_overflow();
}

static void revoke_eviction(UINT32 const page_idx);

static UINT32 get_page(UINT32 const pmt_idx)
{
	ASSERT(pmt_idx < PMT_SUB_PAGES);

	/* shortcuts for consecutive access of the same pmt_idx */
	if (last_pmt_idx == pmt_idx) return last_page_idx;

	UINT32 page_idx = mem_search_equ_sram(cached_pmt_idxes,
					       sizeof(UINT32),
					       NUM_PC_SUB_PAGES, pmt_idx);
	if (page_idx >= NUM_PC_SUB_PAGES) return NULL_PAGE_IDX;

	last_pmt_idx = pmt_idx;
	last_page_idx = page_idx;

	UINT32 timestamp = cached_pmt_timestamps[page_idx];
	if (timestamp == EVICTED_TIMESTAMP)
		revoke_eviction(page_idx);
	else if (timestamp < NULL_TIMESTAMP)
		update_timestamp(page_idx);

	return page_idx;
}

static inline void free_page(UINT32 const page_idx)
{
	ASSERT(page_idx < NUM_PC_SUB_PAGES);

	cached_pmt_idxes[page_idx] = NULL_PMT_IDX;
	cached_pmt_timestamps[page_idx] = NULL_TIMESTAMP;
	bit_array_clear(cached_pmt_is_dirty, page_idx);

	if (last_page_idx == page_idx) {
		last_page_idx = NULL_PAGE_IDX;
		last_pmt_idx = NULL_PMT_IDX;
	}

	num_free_sub_pages++;
}

static inline UINT32 find_free_buf()
{
	UINT32	free_page_idx = mem_search_equ_sram(cached_pmt_idxes,
						   sizeof(UINT32),
						   NUM_PC_SUB_PAGES,
						   NULL_PMT_IDX);
	ASSERT(free_page_idx < NUM_PC_SUB_PAGES);
	return free_page_idx;
}

/* ========================================================================= *
 *  Public Interface
 * ========================================================================= */

UINT32 	pmt_cache_get(UINT32 const pmt_idx)
{
	UINT32	page_idx = get_page(pmt_idx);
	/* if the entry exists, put fails */
	if (page_idx == NULL_PAGE_IDX) return NULL;
	/* else return buffer address */
	return PC_SUB_PAGE(page_idx);
}

BOOL8	pmt_cache_put(UINT32 const pmt_idx)
{
	ASSERT(pmt_idx < PMT_SUB_PAGES);

	UINT32	old_page_idx = get_page(pmt_idx);
	ASSERT(old_page_idx == NULL_PAGE_IDX);

	/* if no more free entries, put fails */
	if (num_free_sub_pages == 0) return 1;

	UINT32 free_page_idx = find_free_buf();
	cached_pmt_idxes[free_page_idx] = pmt_idx;
	cached_pmt_timestamps[free_page_idx] = LOADING_TIMESTAMP;

	last_pmt_idx = pmt_idx;
	last_page_idx = free_page_idx;

	num_free_sub_pages--;
	return 0;
}

BOOL8	pmt_cache_is_loading(UINT32 const pmt_idx)
{
	UINT32	page_idx = get_page(pmt_idx);
	ASSERT(page_idx != NULL_PAGE_IDX);
	return cached_pmt_timestamps[page_idx] == LOADING_TIMESTAMP;
}

void	pmt_cache_set_loaded(UINT32 const pmt_idx)
{
	UINT32	page_idx = get_page(pmt_idx);
	ASSERT(page_idx != NULL_PAGE_IDX);
	ASSERT(cached_pmt_timestamps[page_idx] == LOADING_TIMESTAMP);
	update_timestamp(page_idx);
}

void	pmt_cache_fix(UINT32 const pmt_idx)
{
	UINT32	page_idx = get_page(pmt_idx);
	ASSERT(page_idx != NULL_PAGE_IDX);

	if (cached_pmt_fix_count[page_idx] == 0) {
		ASSERT(cached_pmt_timestamps[page_idx] < NULL_TIMESTAMP);
		cached_pmt_timestamps[page_idx] = FIXED_TIMESTAMP;
		cached_pmt_fix_count[page_idx] = 1;
	}
	else {
		ASSERT(cached_pmt_timestamps[page_idx] == FIXED_TIMESTAMP);
		cached_pmt_fix_count[page_idx]++;
	}
}

void	pmt_cache_unfix(UINT32 const pmt_idx)
{
	UINT32	page_idx = get_page(pmt_idx);
	ASSERT(page_idx != NULL_PAGE_IDX);

	ASSERT(cached_pmt_timestamps[page_idx] == FIXED_TIMESTAMP);
	ASSERT(cached_pmt_fix_count[page_idx] > 0);
	cached_pmt_fix_count[page_idx]--;
	if (cached_pmt_fix_count[page_idx] == 0)
		update_timestamp(page_idx);
}

void	pmt_cache_set_dirty(UINT32 const pmt_idx, BOOL8 const is_dirty)
{
	UINT32	page_idx = get_page(pmt_idx);
	ASSERT(page_idx != NULL_PAGE_IDX);

	if (is_dirty)
		bit_array_set(cached_pmt_is_dirty, page_idx);
	else
		bit_array_clear(cached_pmt_is_dirty, page_idx);
}

BOOL8	pmt_cache_is_dirty(UINT32 const pmt_idx)
{
	UINT32	page_idx = get_page(pmt_idx);
	ASSERT(page_idx != NULL_PAGE_IDX);

	BOOL8 is_dirty = bit_array_test(cached_pmt_is_dirty, page_idx);
	return is_dirty;
}

BOOL8	pmt_cache_is_full(void)
{
	return num_free_sub_pages == 0;
}

/*
 * Eviction and merge buffer for dirty, evicted pages
 * */

static UINT8 merge_buf_size = 0;
static UINT32 merged_page_idxes[SUB_PAGES_PER_PAGE];

static void revoke_eviction(UINT32 const page_idx)
{
	ASSERT(cached_pmt_timestamps[page_idx] == EVICTED_TIMESTAMP);
	ASSERT(merge_buf_size > 0);
	// find the evicted page in merge buf
	UINT8 sp_i;
	for (sp_i = 0; sp_i < merge_buf_size; sp_i++)
		if (merged_page_idxes[sp_i] == page_idx) break;
	ASSERT(sp_i != merge_buf_size);
	// remove the evicted page from merge buf
	merge_buf_size--;
	merged_page_idxes[sp_i] = merged_page_idxes[merge_buf_size];
	// change timestamp to normal
	update_timestamp(page_idx);
}

BOOL8	pmt_cache_evict()
{
	if (!pmt_cache_is_full()) return 0;

	while (merge_buf_size < SUB_PAGES_PER_PAGE) {
		UINT32 lru_page_idx = mem_search_min_max(
						 cached_pmt_timestamps,
						 sizeof(UINT32),
						 NUM_PC_SUB_PAGES,
						 MU_CMD_SEARCH_MIN_SRAM);
		ASSERT(lru_page_idx < NUM_PC_SUB_PAGES);
		ASSERT(cached_pmt_idxes[lru_page_idx] < PMT_SUB_PAGES);
		UINT32 lru_page_timestamp = cached_pmt_timestamps[lru_page_idx];
		ASSERT(lru_page_timestamp < NULL_TIMESTAMP);

		BOOL8 is_dirty = bit_array_test(cached_pmt_is_dirty, lru_page_idx);
		if (!is_dirty) {
			free_page(lru_page_idx);
			return 0;
		}

		if (lru_page_idx == last_page_idx) {
			last_page_idx = NULL_PAGE_IDX;
			last_pmt_idx = NULL_PMT_IDX;
		}

		cached_pmt_timestamps[lru_page_idx] = EVICTED_TIMESTAMP;
		merged_page_idxes[merge_buf_size] = lru_page_idx;
		merge_buf_size++;
	}
	return 1;
}

void	pmt_cache_flush(UINT32 const flush_buf,
			UINT32 merged_pmt_idxes[SUB_PAGES_PER_PAGE])
{
	ASSERT(merge_buf_size == SUB_PAGES_PER_PAGE);
	/* need to flush */
	UINT32 flush_buf_offset = 0;
	for_each_subpage(sp_i) {
		UINT32 merge_page_idx = merged_page_idxes[sp_i];

		UINT32 merge_pmt_idx = cached_pmt_idxes[merge_page_idx];
		merged_pmt_idxes[sp_i] = merge_pmt_idx;

		mem_copy(flush_buf + flush_buf_offset,
			PC_SUB_PAGE(merge_page_idx),
			BYTES_PER_SUB_PAGE);

		free_page(merge_page_idx);
		flush_buf_offset += BYTES_PER_SUB_PAGE;
	}
	merge_buf_size = 0;
}
