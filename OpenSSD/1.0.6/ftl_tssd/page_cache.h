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

BOOL8	page_cache_has (page_key_t const key);
BOOL8 	page_cache_get (page_key_t const key,
			UINT32 *buf, BOOL8 const will_modify);
void	page_cache_put (page_key_t const key,
			UINT32 *buf, UINT8 const flag);

BOOL8	page_cache_get_flag(page_key_t const key, UINT8 *flag);
BOOL8	page_cache_set_flag(page_key_t const key, UINT8 const flag);

BOOL8	page_cache_is_full(void);
BOOL8	page_cache_evict();
void	page_cache_flush(UINT32 const merge_buf, 
			 page_key_t merged_keys[SUB_PAGES_PER_PAGE]);

task_res_t	page_cache_load(page_key_t const key);
#endif
