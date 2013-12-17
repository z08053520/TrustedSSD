#ifndef __CMT_H
#define __CMT_H
#include "jasmine.h"
#include "buffer_cache.h"

/* the number of fixed entries must be less than half of capacity */
#define CMT_MAX_FIX_ENTRIES		NUM_BC_BUFFERS
#define CMT_MIN_EVICTABLE_ENTRIES	64	
//#define CMT_MIN_EVICTABLE_ENTRIES	1
#define CMT_ENTRIES			(CMT_MAX_FIX_ENTRIES + CMT_MIN_EVICTABLE_ENTRIES)	

/* ========================================================================== 
 * CMT(Cached Mapping Table) Public API 
 * ========================================================================*/
void   cmt_init(void);

BOOL32 cmt_get(UINT32 const lpn, UINT32* vpn);
BOOL32 cmt_add(UINT32 const lpn, UINT32 const vpn);
BOOL32 cmt_update(UINT32 const lpn, UINT32 const new_vpn);
BOOL32 cmt_evict(UINT32 *lpn, UINT32 *vpn, BOOL32 *is_dirty);

BOOL32 cmt_fix(UINT32 const lpn);
BOOL32 cmt_unfix(UINT32 const lpn);

BOOL32 cmt_is_full();
#endif
