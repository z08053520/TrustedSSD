#ifndef __BUFFER_CACHE_H
#define __BUFFER_CACHE_H

#include "jasmine.h"

/* cache size is 16MB by default */
#define BC_ADDR			DRAM_BASE 
#define NUM_BC_BUFFERS_PER_BANK 16 
//#define NUM_BC_BUFFERS_PER_BANK 1	
#define NUM_BC_BUFFERS		(NUM_BC_BUFFERS_PER_BANK * NUM_BANKS)
#define BC_BYTES		(NUM_BC_BUFFERS * BYTES_PER_PAGE)
#define BC_BUF(i)		(BC_ADDR + BYTES_PER_PAGE * i)
#define BC_BUF_IDX(addr)	((addr - BC_ADDR) / BYTES_PER_PAGE)

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
void bc_fill(UINT32 key, UINT32 const offset, 
		UINT32 const num_sectors, bc_buf_type const type);
void bc_fill_full_page(UINT32 key, bc_buf_type const type);

void bc_set_valid_sectors(UINT32 key, UINT8 offset, UINT8 const num_sectors, 
			     bc_buf_type const type);
void bc_set_dirty(UINT32 key, bc_buf_type const type);

#endif /*  __BUFFER_CACHE_H */
