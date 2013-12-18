#ifndef __PMT_H
#define __PMT_H
#include "jasmine.h"

/* *
 * PMT = page-level mapping table
 *
 *   Let's do some simple math to calculate the number of entries in PMT.
 * For a 64GB flash with 32KB page, there are 64GB / 32KB = 2M PMT entries, in
 * 2M / (32KB / sizeof(UINT32)) = 256 pages.
 * */
#define PMT_ENTRIES		(PAGES_PER_BANK * NUM_BANKS)
//#define PMT_ENTRIES_PER_PAGE	(BYTES_PER_PAGE / sizeof(UINT32))
#define PMT_ENTRIES_PER_PAGE	(BYTES_PER_PAGE / 4)
#define PMT_PAGES		COUNT_BUCKETS(PMT_ENTRIES, PMT_ENTRIES_PER_PAGE)

void pmt_init(void);

void pmt_update(UINT32 const lpn, UINT32 const vpn);
void pmt_fetch(UINT32 const lpn, UINT32 *vpn);

#endif /* __PMT_H */
