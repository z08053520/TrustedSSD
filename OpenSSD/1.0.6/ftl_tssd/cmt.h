#ifndef __CMT_H
#define __CMT_H
#include "jasmine.h"
#include "cache.h"

/* the number of fixed entries must be less than half of capacity */
#define CMT_MAX_FIX_ENTRIES	NUM_CACHE_BUFFERS
#define CMT_ENTRIES		(2 * CMT_MAX_FIX_ENTRIES + 2)

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