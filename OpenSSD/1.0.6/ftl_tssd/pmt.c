#include "pmt.h"
#include "page_cache.h"

/* ========================================================================= *
 * Macros, Data Structure and Gloal Variables 
 * ========================================================================= */

#define pmt_get_index(lspn)	(lspn / PMT_ENTRIES_PER_SUB_PAGE)
#define pmt_get_offset(lspn)	(lspn % PMT_ENTRIES_PER_SUB_PAGE)

/* ========================================================================= *
 * Public API 
 * ========================================================================= */

void pmt_init(void)
{
	INFO("pmt>init", "# of PMT entries = %d, # of PMT pages = %d",
			PMT_ENTRIES, PMT_PAGES);
}

void pmt_fetch(UINT32 const lspn, vp_t *vp)
{
	UINT32 pmt_index  = pmt_get_index(lspn);
	UINT32 pmt_offset = pmt_get_offset(lspn);
	UINT32 pmt_buff;
	page_cache_load(pmt_index, &pmt_buff, PC_BUF_TYPE_PMT, FALSE);
	BUG_ON("buffer is empty", pmt_buff == NULL);

	vp_or_int res = {
		.as_int = read_dram_32(pmt_buff + sizeof(UINT32) * pmt_offset)
	};
	*vp = res.as_vp;
}

void pmt_update(UINT32 const lspn, vp_t const vp)
{
	UINT32 pmt_index  = pmt_get_index(lspn);
	UINT32 pmt_offset = pmt_get_offset(lspn);
	UINT32 pmt_buff;
	page_cache_load(pmt_index, &pmt_buff, PC_BUF_TYPE_PMT, TRUE);
	BUG_ON("buffer is empty", pmt_buff == NULL);

	vp_or_int val = {.as_vp = vp};
	write_dram_32(pmt_buff + sizeof(UINT32) * pmt_offset, val.as_int);
}
