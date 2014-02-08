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

task_res_t sot_load(UINT32 const lpn);

BOOL8	sot_authenticate(UINT32 const lpn, UINT8 const offset,
			 UINT8 const num_sectors, uid_t const expected_uid);
void	sot_authorize  (UINT32 const lspn, UINT8 const offset,
			UINT8 const num_sectors, uid_t const new_uid);

#endif
#endif
