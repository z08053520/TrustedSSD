#ifndef __PMT_CACHE_H
#define __PMT_CACHE_H

#include "jasmine.h"

/*
 * Cache for PMT
 *
 * The entire PMT data can not be fit into DRAM. Thus we use a LRU cache to
 * store `hot` PMT data in order to accelerate PMT access.
 *
 * */

#define NULL_PMT_IDX	0xFFFFFFFF

/* Get the buffer allocated for a PMT page
 *	return NULL if not cached */
UINT32 	pmt_cache_get(UINT32 const pmt_idx);
/* Put a PMT page into cache and allocate buffer for it
 *	A PMT page is reserved by default after put into cache.
 * */
BOOL8	pmt_cache_put(UINT32 const pmt_idx);

/* A reserved PMT page won't be evicted */
BOOL8	pmt_cache_set_reserved(UINT32 const pmt_idx, BOOL8 const is_reserved);
BOOL8	pmt_cache_is_reserved(UINT32 const pmt_idx, BOOL8 *is_reserved);

/* A dirty PMT page should be written back to flash */
BOOL8	pmt_cache_set_dirty(UINT32 const pmt_idx, BOOL8 const is_dirty);
BOOL8	pmt_cache_is_dirty(UINT32 const pmt_idx, BOOL8 *is_dirty);


/* Return whether the cache is full
 *	If cache is full, no more page can be put into cache. */
BOOL8	pmt_cache_is_full(void);
/* Evict a PMT page in cache
 *	The eviction policy is LRU(Least Recently Used). If the LRU page is
 *	clean, then we are done; if it is dirty, we put the page into a merge
 *	buffer and then find next LRU page to evict.
 *
 *	Return 0 if at least one buffer page is free;
 *	Return 1 if merge buffer for dirty, evicted page is full.
 * */
BOOL8	pmt_cache_evict();
/* Flush the merge buffer for evicted, dirty pages
 *	Copy SUB_PAGES_PER_PAGE dirty PMT sub-pages into target_buf. The PMT
 *	indexes of these pages are stored in merge_pmt_idxes.
 *	Return 0 if succeed; 1 if fails.
 * */
BOOL8	pmt_flush(UINT32 const target_buf,
		UINT32 merged_pmt_idxes[SUB_PAGES_PER_PAGE]);

#endif
