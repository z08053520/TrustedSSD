#ifndef __PAGE_CACHE_H
#define __PAGE_CACHE_H

#include "jasmine.h"

typedef enum _pc_buf_type {
	/* Cache a page of PMT (Page Mapping Table) 
	 * For this type of entries, the key is pmt_index. */
	PC_BUF_TYPE_PMT
#if OPTION_ACL
	/* Cache a page of SOT (Sector Ownership Table) 
	 * For this type of entries, the key is sot_index. */
	,PC_BUF_TYPE_SOT
#endif
	,PC_BUF_TYPE_NUL
} pc_buf_type_t;

void	page_cache_init(void);
BOOL8	page_cache_has (UINT32 const idx, pc_buf_type_t const type);
void 	page_cache_get (UINT32 const idx, UINT32 *buf, 
			pc_buf_type_t const type, BOOL8 const will_modify);
void	page_cache_put (UINT32 const idx, UINT32 *buf, 
			pc_buf_type_t const type);

BOOL8	page_cache_is_full(void);
void	page_cache_evict(UINT32 *idx, pc_buf_type_t *type, 
			 BOOL8 *is_dirty, UINT32 *buf);

#endif
