#ifndef __SOT_H
#define __SOT_H
#include "jasmine.h"
#if OPTION_ACL

/* *
 * SOT = Sector Ownership Table 
 *	
 *	LBA (logical block/sector number) --> UID (user id)		
 *	
 *   Let's do some simple math to calculate the number of entries in SOT.
 *   For a 64GB flash, there are 64GB / 512B = 128M entries, occupying 
 *   128M * 2B / 32KB = 8K pages. 
 * */


/* ===========================================================================
 * Type and Macro Definitions
 * =========================================================================*/

typedef UINT16		uid_t;

#define SOT_ENTRIES			(SECTORS_PER_BANK * NUM_BANKS)

#define SOT_ENTRIES_PER_SUB_PAGE	(BYTES_PER_SUB_PAGE / sizeof(uid_t))
#define SOT_SUB_PAGES			COUNT_BUCKETS(SOT_ENTRIES, SOT_ENTRIES_PER_SUB_PAGE)
// TODO: remove?
#define SOT_ENTRIES_PER_PAGE		(BYTES_PER_PAGE / sizeof(uid_t))
#define SOT_PAGES			COUNT_BUCKETS(SOT_ENTRIES, SOT_ENTRIES_PER_PAGE)

/* ===========================================================================
 * Public Interface 
 * =========================================================================*/

void sot_init();

/* for individual sector  */
void sot_fetch(UINT32 const lba, uid_t *uid);
void sot_update(UINT32 const lba, uid_t const uid);

/* for consecutive sectors */
BOOL8 sot_check(UINT32 const lba_begin, UINT32 const num_sectors, uid_t const uid);
void sot_set(UINT32 const lba_begin, UINT32 const num_sectors, uid_t const uid);

#endif
#endif
