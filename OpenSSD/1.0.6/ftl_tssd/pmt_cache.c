#include "pmt_cache.h"
#include "mem_util.h"
#include "dram.h"
#include "bit_array.h"

#define EVICTED_TIMESTAMP	0xFFFFFFFF
#define RESERVED_TIMESTAMP	0xFFFFFFFE
#define NULL_TIMESTAMP		0xFFFFFFFD
#define NULL_PAGE_IDX		NUM_PC_SUB_PAGES

/* For each cached sub page, we record **pmt_idx**, and **timestamp**.
 *
 * There are three different entries in cache:
 *
 *	Free entries (timestamp == NULL_TIMESTAMP, index = NULL_PMT_IDX),
 *	Normal entries (timestamp < NULL_TIMESTAMP, index < NULL_PMT_IDX),
 *	Reserved entries (timestamp == RESERVED_TIMESTAMP, index < NULL_PMT_IDX).
 *	Evicted entries (timestamp == EVICTED_TIMESTAMP, index < NULL_PMT_IDX).
 *
 * */
static UINT32	cached_pmt_idxes[NUM_PC_SUB_PAGES] = {
	[0 ... (NUM_PC_SUB_PAGES-1)] = NULL_PMT_IDX
};
static UINT32	cached_pmt_timestamps[NUM_PC_SUB_PAGES] = {
	[0 ... (NUM_PC_SUB_PAGES-1)] = NULL_TIMESTAMP
};
static DECLARE_BIT_ARRAY(cached_pmt_is_dirty, NUM_PC_SUB_PAGES);

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
	/* find minimum timestamp */
	UINT32	min_timestamp_idx = mem_search_min_max(
					cached_pmt_timestamps,
					sizeof(UINT32),
					NUM_PC_SUB_PAGES,
					MU_CMD_SEARCH_MIN_SRAM),
		min_timestamp = cached_pmt_timestamps[min_timestamp_idx];
	/* update timestamps */
	UINT8 sp_i;
	for (sp_i = 0; sp_i < NUM_PC_SUB_PAGES; sp_i++) {
		if (cached_pmt_timestamps[sp_i] >= NULL_TIMESTAMP) continue;
		cached_pmt_timestamps[sp_i] -= min_timestamp;
	}
}

static inline void update_timestamp(UINT32 const page_idx)
{
	/* update timestamp for LRU cache policy */
	cached_pmt_timestamps[page_idx] = current_timestamp++;
	if (unlikely(current_timestamp >= NULL_TIMESTAMP))
		handle_timestamp_overflow();
}

static UINT32 get_page(UINT32 const pmt_idx)
{
	/* shortcuts for consecutive access of the same pmt_idx */
	if (last_pmt_idx == pmt_idx) return last_page_idx;

	UINT32 page_idx = mem_search_equ_sram(cached_pmt_idxes,
					       sizeof(UINT32),
					       NUM_PC_SUB_PAGES, pmt_idx);
	if (page_idx >= NUM_PC_SUB_PAGES) return NULL_PAGE_IDX;

	last_pmt_idx = pmt_idx;
	last_page_idx = page_idx;

	if (cached_pmt_timestamps[page_idx] < NULL_TIMESTAMP)
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
	UINT32	old_page_idx = get_page(pmt_idx);
	/* if the entry exists, then don't need to put again */
	if (old_page_idx != NULL_PAGE_IDX) return 0;

	/* if no more free entries, put fails */
	if (num_free_sub_pages == 0) return 1;

	UINT32 free_page_idx = find_free_buf();
	cached_pmt_idxes[free_page_idx] = pmt_idx;
	cached_pmt_timestamps[free_page_idx] = RESERVED_TIMESTAMP;

	last_pmt_idx = pmt_idx;
	last_page_idx = free_page_idx;

	num_free_sub_pages--;
	return 0;
}

BOOL8	pmt_cache_set_reserved(UINT32 const pmt_idx, BOOL8 const is_reserved)
{
	UINT32	page_idx = get_page(pmt_idx);
	/* if the entry doesn't exist, then fails */
	if (page_idx == NULL_PAGE_IDX) return 1;

	if (is_reserved)
		cached_pmt_timestamps[page_idx] = RESERVED_TIMESTAMP;
	else
		update_timestamp(page_idx);
	return 0;
}

BOOL8	pmt_cache_is_reserved(UINT32 const pmt_idx, BOOL8 *is_reserved)
{
	UINT32	page_idx = get_page(pmt_idx);
	/* if the entry exists, put fails */
	if (page_idx == NULL_PAGE_IDX) return 1;

	*is_reserved = (cached_pmt_timestamps[page_idx] == RESERVED_TIMESTAMP);
	return 0;
}

BOOL8	pmt_cache_set_dirty(UINT32 const pmt_idx, BOOL8 const is_dirty)
{
	UINT32	page_idx = get_page(pmt_idx);
	/* if the entry exists, then fails */
	if (page_idx == NULL_PAGE_IDX) return 1;

	if (is_dirty)
		bit_array_set(cached_pmt_is_dirty, page_idx);
	else
		bit_array_clear(cached_pmt_is_dirty, page_idx);
	return 0;
}

BOOL8	pmt_cache_is_dirty(UINT32 const pmt_idx, BOOL8 *is_dirty)
{
	UINT32	page_idx = get_page(pmt_idx);
	/* if the entry exists, then fails */
	if (page_idx == NULL_PAGE_IDX) return 1;

	*is_dirty = bit_array_test(cached_pmt_is_dirty, page_idx);
	return 0;
}

BOOL8	pmt_cache_is_full(void)
{
	return num_free_sub_pages == 0;
}

static UINT8 merge_buf_size = 0;
static UINT32 merged_page_idxes[SUB_PAGES_PER_PAGE];

BOOL8	pmt_cache_evict_and_flush(
		UINT32 const flush_buf,
		UINT32 merged_pmt_idxes[SUB_PAGES_PER_PAGE])
{
	if (!pmt_cache_is_full()) return 0;

	while (merge_buf_size < SUB_PAGES_PER_PAGE) {
		UINT32 lru_page_idx = mem_search_min_max(
						 cached_pmt_timestamps,
						 sizeof(UINT32),
						 NUM_PC_SUB_PAGES,
						 MU_CMD_SEARCH_MIN_SRAM);
		UINT32 lru_page_timestamp = cached_pmt_timestamps[lru_page_idx];
		ASSERT(lru_page_timestamp < NULL_TIMESTAMP);

		BOOL8 is_dirty = bit_array_test(cached_pmt_is_dirty, lru_page_idx);
		if (!is_dirty) {
			free_page(lru_page_idx);
			return 0;
		}

		cached_pmt_timestamps[lru_page_idx] = RESERVED_TIMESTAMP;
		merged_page_idxes[merge_buf_size] = lru_page_idx;
		merge_buf_size++;
	}

	/* need to flush */
	UINT32 flush_buf_offset = 0;
	for_each_subpage(sp_i) {
		UINT8 merge_page_idx = merged_page_idxes[sp_i];

		UINT32 merge_pmt_idx = cached_pmt_idxes[merge_page_idx];
		merged_pmt_idxes[sp_i] = merge_pmt_idx;

		mem_copy(flush_buf + flush_buf_offset,
			PC_SUB_PAGE(merge_page_idx),
			BYTES_PER_SUB_PAGE);

		free_page(merge_page_idx);
		flush_buf_offset += BYTES_PER_SUB_PAGE;
	}

	merge_buf_size = 0;
	return 1;
}
