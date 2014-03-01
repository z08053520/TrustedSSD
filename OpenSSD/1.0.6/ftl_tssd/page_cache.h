#ifndef __PAGE_CACHE_H
#define __PAGE_CACHE_H

#include "jasmine.h"
#include "gtd.h"
#include "task_engine.h"

/* ========================================================================= *
 *  Macros and Types
 * ========================================================================= */

typedef enum {
	PC_FLAG_RESERVED = 1,
	PC_FLAG_DIRTY = 2
} pc_flag_t;

#define is_dirty(flag)		(((flag) & PC_FLAG_DIRTY) != 0)
#define set_dirty(flag)		((flag) |= PC_FLAG_DIRTY)
#define reset_dirty(flag)	((flag) &= ~PC_FLAG_DIRTY)

#define is_reserved(flag)	(((flag) & PC_FLAG_RESERVED) != 0)
#define set_reserved(flag)	((flag) |= PC_FLAG_RESERVED)
#define reset_reserved(flag)	((flag) &= ~PC_FLAG_RESERVED)

/* ========================================================================= *
 *  Public Interface
 * ========================================================================= */

void	page_cache_init(void);

BOOL8	page_cache_has (UINT32 const pmt_idx);
BOOL8 	page_cache_get (UINT32 const pmt_idx,
			UINT32 *buf, BOOL8 const will_modify);
void	page_cache_put (UINT32 const pmt_idx,
			UINT32 *buf, UINT8 const flag);

BOOL8	page_cache_get_flag(UINT32 const pmt_idx, UINT8 *flag);
BOOL8	page_cache_set_flag(UINT32 const pmt_idx, UINT8 const flag);

BOOL8	page_cache_is_full(void);
BOOL8	page_cache_evict();
void	page_cache_flush(UINT32 const merge_buf,
			 UINT32 merged_pmt_idxes[SUB_PAGES_PER_PAGE]);

task_res_t	page_cache_load(UINT32 const pmt_idx);
#endif
