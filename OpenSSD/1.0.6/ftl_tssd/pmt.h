#ifndef __PMT_H
#define __PMT_H
#include "jasmine.h"

/* *
 * PMT = page-level mapping table
 *
 * 	logical sub-page number (LSPN) --> virtual page number (VPN)	
 *	
 *   PMT store in which virtual page (32KB) a user sub-page (4KB) is stored. 
 *
 *   Let's do some simple math to calculate the number of entries in PMT.
 *   For a 64GB flash with 32KB page and 4KB sub-page, there are 64GB / 4KB = 
 *   16M PMT entries, in 16M / (32KB / sizeof(UINT32)) = 2K pages.
 * */
#define PMT_ENTRIES			(SUB_PAGES_PER_PAGE * PAGES_PER_BANK * NUM_BANKS)

#define PMT_ENTRIES_PER_SUB_PAGE	(BYTES_PER_SUB_PAGE / sizeof(UINT32))
#define PMT_SUB_PAGES			COUNT_BUCKETS(PMT_ENTRIES, PMT_ENTRIES_PER_SUB_PAGE)

// TODO: remove?
#define PMT_ENTRIES_PER_PAGE		(BYTES_PER_PAGE / sizeof(UINT32))
#define PMT_PAGES			COUNT_BUCKETS(PMT_ENTRIES, PMT_ENTRIES_PER_PAGE)

void pmt_init(void);

void pmt_update(UINT32 const lspn, UINT32 const vpn);
void pmt_fetch(UINT32 const lspn, UINT32 *vpn);

#endif /* __PMT_H */
