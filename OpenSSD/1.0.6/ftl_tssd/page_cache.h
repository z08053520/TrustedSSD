#ifndef __PAGE_CACHE_H
#define __PAGE_CACHE_H

#include "jasmine.h"

/* ========================================================================= *
 *  Macros and Types
 * ========================================================================= */

/* may require gcc to use -fms-extensions */
typedef union {
	struct {
		UINT8	type:1;
		UINT32	idx:31;
	};
	UINT32	as_uint;
} pc_key_t;

#define PC_TYPE_PMT	0
#if OPTION_ACL
#define PC_TYPE_SOT	1
#endif 

typedef enum {
	PC_FLAG_RESERVED = 1,
	PC_FLAG_DIRTY = 2
} pc_flag_t;
#define is_dirty(flag)		(((flag) & PC_FLAG_DIRTY) != 0)
#define is_reserved(flag)	(((flag) & PC_FLAG_RESERVED) != 0)
#define set_dirty(flag)		((flag) |= PC_FLAG_DIRTY)
#define set_reserved(flag)	((flag) |= PC_FLAG_RESERVED)
#define reset_dirty(flag)	((flag) &= ~PC_FLAG_DIRTY)
#define reset_reserved(flag)	((flag) &= ~PC_FLAG_RESERVED)

/* ========================================================================= *
 *  Public Interface
 * ========================================================================= */

void	page_cache_init(void);

BOOL8	page_cache_has (pc_key_t const key);
void 	page_cache_get (pc_key_t const key,
			UINT32 *buf, BOOL8 const will_modify);
void	page_cache_put (pc_key_t const key,
			UINT32 *buf, UINT8 const flag);

BOOL8	page_cache_get_flag(pc_key_t const key, UINT8 *flag);
BOOL8	page_cache_set_flag(pc_key_t const key, UINT8 const flag);

BOOL8	page_cache_is_full(void);
BOOL8	page_cache_evict();
void	page_cache_flush(UINT32 const merge_buf, 
			 pc_key_t merged_keys[SUB_PAGES_PER_PAGE]);
#endif
