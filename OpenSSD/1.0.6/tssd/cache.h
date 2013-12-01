#ifndef __CACHE_H
#define __CACHE_H

#include "jasmine.h"

/* cache size is 16MB by default */
#define CACHE_ADDR		DRAM_BASE 
#define CACHE_SCALE		16	
#define NUM_CACHE_BUFFERS	(2 * NUM_BANKS * CACHE_SCALE)
#define CACHE_BYTES		(NUM_CACHE_BUFFERS * BYTES_PER_PAGE)
#define CACHE_BUF(i)		(CACHE_ADDR + BYTES_PER_PAGE * i)

typedef enum _cache_buf_type {
	/* Cache for page mapping table (PMT). 
	 * For this type of entries, the key is pmt_index. */
	CACHE_BUF_TYPE_PMT,	
	/* Cache for user pages.
	 * For this type of entries, the key is lpn. */
	CACHE_BUF_TYPE_USR	
} cache_buf_type;

void cache_init(void);

/* get the DRAM buffer address for a page */
void cache_get(UINT32 key, UINT32 *addr, cache_buf_type const type);
/* put a page into cache, then allocate and return the buffer */
void cache_put(UINT32 key, UINT32 *addr, cache_buf_type const type);
/* fill the page */
void cache_fill(UINT32 key, UINT32 const offset, 
		UINT32 const num_sectors, cache_buf_type const type);
void cache_fill_full_page(UINT32 key, cache_buf_type const type);

/* inform the cache that some sectors of the page have been loaded from flash */
void cache_load_sectors(UINT32 const lpn, UINT8 offset, UINT8 const num_sectors);
/* inform the cache that some sectors of the page have been overwritten in
 * DRAM so that cache can write back to flash when evicting the page*/
void cache_overwrite_sectors(UINT32 const lpn, UINT8 offset, UINT8 const num_sectors);

#endif /*  __CACHE_H */
