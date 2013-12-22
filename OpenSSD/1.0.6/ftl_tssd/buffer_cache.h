#ifndef __BUFFER_CACHE_H
#define __BUFFER_CACHE_H

#include "jasmine.h"
#include "dram.h"

typedef enum _bc_buf_type {
	/* Cache for page mapping table (PMT). 
	 * For this type of entries, the key is pmt_index. */
	BC_BUF_TYPE_PMT	
	/* Cache for user pages.
	 * For this type of entries, the key is lpn. */
	,BC_BUF_TYPE_USR
#if OPTION_ACL
	,BC_BUF_TYPE_SOT
#endif
} bc_buf_type;

void bc_init(void);

/* get the DRAM buffer address for a page */
void bc_get(UINT32 key, UINT32 *addr, bc_buf_type const type);
/* put a page into cache, then allocate and return the buffer */
void bc_put(UINT32 key, UINT32 *addr, bc_buf_type const type);

/* fill the page */
void bc_fill(UINT32 const key, UINT32 const offset, 
		UINT32 const num_sectors, bc_buf_type const type);
void bc_fill_full_page(UINT32 key, bc_buf_type const type);

void bc_set_valid_sectors(UINT32 key, UINT8 offset, UINT8 const num_sectors, 
			     bc_buf_type const type);
void bc_set_dirty(UINT32 key, bc_buf_type const type);

#endif /*  __BUFFER_CACHE_H */
