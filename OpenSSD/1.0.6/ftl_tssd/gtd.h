#ifndef __GTD_H
#define __GTD_h
#include "pmt.h"

/* *
 * GTD = global translation table
 *
 *   GTD serves the logical-to-physical address translation for pages of PMT 
 *   (Page Mapping Table) and SOT (Secotr Ownership Table). While the address
 *   of user pages are translated to by looking up PMT.
 *
 *   Let's do some simple math to calculate the number of entries in GTD.
 * For a 64GB flash with 32KB page, there are 256 PMT pages and 8K SOT pages; 
 * In other word, a total 8448 GTD entries, in 8448 / (32KB / sizeof(UINT32)) = 
 * 1 page ( a little bit more than one page). 
 * */

/* ===========================================================================
 * Macro Definitions 
 * =========================================================================*/

#if OPTION_ACL
#include "sot.h"
#define ZONE_LIST			\
		ENTRY(PMT),		\
		ENTRY(SOT)
#else
#define ZONE_LIST			\
		ENTRY(PMT)
#endif

typedef enum {
#define ENTRY(zone_name)	GTD_ZONE_TYPE_##zone_name,
	ZONE_LIST,
#undef ENTRY
	NUM_GTD_ZONE_TYPES
} gtd_zone_type_t;

typedef struct {
#define ENTRY(zone_name)	UINT32 zone_name[zone_name##_PAGES],
	ZONE_LIST
#undef ENTRY
} gtd_zone_t; 

#define GTD_SIZE		sizeof(gtd_zone_t)
/* GTD must occupy whole pages */
#define GTD_BYTES		(COUNT_BUCKETS(GTD_SIZE, BYTES_PER_PAGE) * BYTES_PER_PAGE)
#define GTD_ADDR		(BAD_BLK_BMP_ADDR + BAD_BLK_BMP_BYTES)

/* ===========================================================================
 * Public Interface 
 * =========================================================================*/

void gtd_init(void);
void gtd_flush(void);

UINT32 gtd_get_vpn(UINT32 const index, gtd_zone_type_t const zone_type);
void   gtd_set_vpn(UINT32 const index, UINT32 const vpn, gtd_zone_type_t const zone_type); 

#endif /* __GTD_H */
