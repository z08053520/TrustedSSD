#ifndef __CACHE_H
#define __CACHE_H

#include "jasmine.h"

/* cache size is 16MB by default */
#define CACHE_ADDR		DRAM_BASE 
#define CACHE_SCALE		16	
#define NUM_CACHE_BUFFERS	(2 * NUM_BANKS * CACHE_SCALE)
#define CACHE_BYTES		(NUM_CACHE_BUFFERS * BYTES_PER_PAGE)
#define CACHE_BUF(i)		(CACHE_ADDR + BYTES_PER_PAGE * i)

void cache_init(void);

/* get the DRAM buffer address for a page */
void cache_get(UINT32 const lpn, UINT32 *addr);
/* put a page into cache, then allocate and return the buffer */
void cache_put(UINT32 const lpn, UINT32 const vpn, UINT32 *addr);

/* inform the cache that some sectors of the page have been loaded from flash */
void cache_load_sectors(UINT32 const lpn, UINT8 offset, UINT8 const num_sectors);
/* inform the cache that some sectors of the page have been overwritten in
 * DRAM so that cache can write back to flash when evicting the page*/
void cache_overwrite_sectors(UINT32 const lpn, UINT8 offset, UINT8 const num_sectors);

#endif /*  __CACHE_H */
