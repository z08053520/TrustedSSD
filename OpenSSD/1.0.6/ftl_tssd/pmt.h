#ifndef __PMT_H
#define __PMT_H
#include "jasmine.h"

/* *
 * PMT = page metadata table
 *
 * PMT maintains metadata for every logical page. The metadata includes:
 *	1) logical sub-page (LSP) --> virtual sub-page (VSP);
 *	2) ownership information for each sectors in the page when
 *	ACL (Access Control Layer) is enabled.
 * */

/* ===========================================================================
 * Type and Macro Definitions
 * =========================================================================*/

typedef struct {
	vp_t		vps[SUB_PAGES_PER_PAGE];
#if OPTION_ACL
	user_id_t	uids[SECTORS_PER_PAGE];
#endif
} pmt_entry_t;

#define PMT_BYTES_PER_ENTRY		sizeof(pmt_entry_t)
#define PMT_ENTRIES			(PAGES_PER_BANK * NUM_BANKS)
/* Make ensure that all sub-pages of a page is in one PMT page */
#define PMT_ENTRIES_PER_SUB_PAGE	(BYTES_PER_SUB_PAGE / \
						PMT_BYTES_PER_ENTRY)
#define PMT_SUB_PAGES			COUNT_BUCKETS(PMT_ENTRIES, \
						PMT_ENTRIES_PER_SUB_PAGE)

#define pmt_get_index(lpn)		((lpn) / PMT_ENTRIES_PER_SUB_PAGE)
#define pmt_get_offset(lpn)		((lpn) % PMT_ENTRIES_PER_SUB_PAGE)

/* ===========================================================================
 * Public Interface
 * =========================================================================*/

BOOL8	pmt_is_loaded(UINT32 const lpn);
BOOL8	pmt_load(UINT32 const lpn);

void 	pmt_update_vp(UINT32 const lpn, UINT8 const sp_offset, vp_t const vp);
void 	pmt_get_vp(UINT32 const lpn,  UINT8 const sp_offset, vp_t* vp);

#if OPTION_ACL
BOOL8	pmt_authenticate(UINT32 const lpn, UINT8 const sect_offset,
			 UINT8 const num_sectors, user_id_t const expected_uid);
void	pmt_authorize  (UINT32 const lpn, UINT8 const sect_offset,
			UINT8 const num_sectors, user_id_t const new_uid);
#endif
#endif /* __PMT_H */
