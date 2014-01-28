#ifndef __GTD_H
#define __GTD_H
#include "jasmine.h"
#include "pmt.h"

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
	#define PAGE_TYPE_LIST			\
			ENTRY(PMT)		\
			ENTRY(SOT)
#else
	#define PAGE_TYPE_LIST			\
			ENTRY(PMT)
#endif

typedef enum {
#define ENTRY(type_name)	PAGE_TYPE_##type_name,
	PAGE_TYPE_LIST
#undef ENTRY
	NUM_PAGE_TYPES
} page_type_t;

/* may require gcc to use -fms-extensions */
typedef union {
	struct {
		UINT8	type:1;
		UINT32	idx:31;
	};
	UINT32	as_uint;
} page_key_t;

#define page_key_equal(key0, key1)	((key0).as_uint == (key1).as_uint)

/* A zone is a consecutive area of memory storing info for one type of page */
typedef struct {
#define ENTRY(zone_name)	UINT32 zone_name[zone_name##_SUB_PAGES];
	PAGE_TYPE_LIST
#undef ENTRY
} gtd_zones_t; 

/* ===========================================================================
 * Public Interface 
 * =========================================================================*/

void gtd_init(void);
void gtd_flush(void);

vsp_t  gtd_get_vsp(page_key_t const key);
void   gtd_set_vsp(page_key_t const key, vsp_t const vsp); 

#endif /* __GTD_H */
