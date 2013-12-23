#ifndef __GTD_H
#define __GTD_H
#include "jasmine.h"

/* *
 * GTD = global translation table
 * 	
 * 		PMT/SOT subpage index --> virtual sub page
 *
 *   GTD serves the logical-to-physical address translation for pages of PMT 
 *   (Page Mapping Table) and SOT (Secotr Ownership Table). While the address
 *   of user pages are translated to by looking up PMT.
 *
 *   In contrast to user pages, PMT and SOT pages (which are 4KB) are mapped to 
 *   virtual sub-pages in a fully assoiciative way.
 *
 *   Let's do some simple math to calculate the number of entries in GTD.
 *   For a 64GB flash with 32KB page and 4KB sub-page, there are 2K PMT pages 
 *   and 8K SOT pages; In other word, a total 10K GTD entries, in 
 *   10K / (32KB / sizeof(UINT32)) = 1.25 page < 2 page.
 * */

/* ===========================================================================
 * Macro Definitions 
 * =========================================================================*/

#if OPTION_ACL
	#include "sot.h"
	#define ZONE_LIST			\
			ENTRY(PMT)		\
			ENTRY(SOT)
#else
	#define ZONE_LIST			\
			ENTRY(PMT)
#endif

typedef enum {
#define ENTRY(zone_name)	GTD_ZONE_TYPE_##zone_name,
	ZONE_LIST
#undef ENTRY
	NUM_GTD_ZONE_TYPES
} gtd_zone_type_t;

typedef struct {
#define ENTRY(zone_name)	UINT32 zone_name[zone_name##_SUB_PAGES];
	ZONE_LIST
#undef ENTRY
} gtd_zone_t; 

/* ===========================================================================
 * Public Interface 
 * =========================================================================*/

void gtd_init(void);
void gtd_flush(void);

UINT32 gtd_get_vspn(UINT32 const index, gtd_zone_type_t const zone_type);
void   gtd_set_vspn(UINT32 const index, UINT32 const vspn, gtd_zone_type_t const zone_type); 

#endif /* __GTD_H */
