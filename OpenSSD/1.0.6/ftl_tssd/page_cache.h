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

void page_cache_init(void);
void page_cache_load(UINT32 const idx, UINT32 *addr, 
		     pc_buf_type_t const type, BOOL8 const will_modify);

#endif
