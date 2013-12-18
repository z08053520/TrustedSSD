#ifndef __SOT_H
#define __SOT_H

#include "jasmine.h"
#if OPTION_ACL

/* *
 * SOT = Sector Ownership Table 
 *
 *   Let's do some simple math to calculate the number of entries in SOT.
 * For a 64GB flash, there are 64GB / 512B = 128M entries, occupying 
 * 128M * 2B / 32KB = 8K pages. 
 * */

typedef UINT16		uid_t;

/* ===========================================================================
 * Macro Definition 
 * =========================================================================*/

#define SOT_ENTRIES		(SECTORS_PER_BANK * NUM_BANKS)
#define SOT_ENTRIES_PER_PAGE	(BYTES_PER_PAGE / sizeof(uid_t))
#define SOT_PAGES		COUNT_BUCKETS(SOT_ENTRIES, SOT_ENTRIES_PER_PAGE)

/* ===========================================================================
 * Public Interface 
 * =========================================================================*/

void sot_init();

void sot_fetch(UINT32 const lba, uid_t *uid);
void sot_update(UINT32 const lba, uid_t const uid);

#endif
#endif
