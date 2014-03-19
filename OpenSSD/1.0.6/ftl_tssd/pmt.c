#include "pmt.h"
#include "pmt_cache.h"
#include "pmt_thread.h"

/* ========================================================================= *
 * Public API
 * ========================================================================= */

BOOL8	pmt_is_loaded(UINT32 const lpn)
{
	UINT32 pmt_idx  = pmt_get_index(lpn);

	UINT32 buf = pmt_cache_get(pmt_idx);
	if (buf == NULL) return FALSE;

	BOOL8 is_loading = pmt_cache_is_loading(pmt_idx);
	return !is_loading;
}

void	pmt_load(UINT32 const lpn)
{
	UINT32 pmt_idx  = pmt_get_index(lpn);

	UINT32 buf = pmt_cache_get(pmt_idx);
	/* the PMT page is loaded or being loaded */
	if (buf != NULL) return;

	pmt_thread_request_enqueue(pmt_idx);
}

void pmt_get_vp(UINT32 const lpn, UINT8 const sp_offset, vp_t *vp)
{
	UINT32	pmt_idx  = pmt_get_index(lpn);
	UINT32	pmt_buf = pmt_cache_get(pmt_idx);
	ASSERT(pmt_buf != NULL);

	UINT32	pmt_offset = pmt_get_offset(lpn) * sizeof(pmt_entry_t)
				+ (UINT32)(&((pmt_entry_t*)0)->vps[sp_offset]);
	vp->as_uint = read_dram_32(pmt_buf + pmt_offset);
}

void pmt_update_vp(UINT32 const lpn, UINT8 const sp_offset, vp_t const vp)
{
	UINT32	pmt_idx  = pmt_get_index(lpn);
	UINT32	pmt_buf = pmt_cache_get(pmt_idx);
	ASSERT(pmt_buf != NULL);
	pmt_cache_set_dirty(pmt_idx, TRUE);

	UINT32	pmt_offset = pmt_get_offset(lpn) * sizeof(pmt_entry_t)
				+ (UINT32)(&((pmt_entry_t*)0)->vps[sp_offset]);
	write_dram_32(pmt_buf + pmt_offset, vp.as_uint);
}

void	pmt_fix(UINT32 const lpn)
{
	UINT32	pmt_idx  = pmt_get_index(lpn);
	pmt_cache_fix(pmt_idx);
}

void	pmt_unfix(UINT32 const lpn)
{
	UINT32	pmt_idx  = pmt_get_index(lpn);
	pmt_cache_unfix(pmt_idx);
}

#if 0
#if OPTION_ACL

#define align_even(num)		(num / 2 * 2)

BOOL8	pmt_authenticate(UINT32 const lpn, UINT8 const sect_offset,
			 UINT8 const num_sectors, user_id_t const expected_uid)
{
	UINT32	pmt_buf = NULL;
	UINT32	pmt_idx = pmt_get_index(lpn);
	page_cache_get(pmt_idx, &pmt_buf, FALSE);
	BUG_ON("buffer is empty", pmt_buf == NULL);

	UINT32	uids_addr = pmt_buf +
				pmt_get_offset(lpn) * sizeof(pmt_entry_t) +
				(UINT32)(((pmt_entry_t*)0)->uids);
	if (num_sectors >= SECTORS_PER_PAGE / 2) {
		user_id_t actual_uids[SECTORS_PER_PAGE] = {0};
		/* assume user_id_t is 16-bits */
		/* address must be 4B align */
		UINT32	target_buf = (UINT32) & actual_uids[align_even(sect_offset)],
			src_buf = uids_addr + align_even(sect_offset) * sizeof(user_id_t);
		/* size must be 4B align*/
		UINT32	size = (num_sectors + (num_sectors % 2 == 1 ? 1 :
				(sect_offset % 2 == 1) * 2)) * sizeof(user_id_t);
		mem_copy(target_buf, src_buf, size);

		for (UINT8 sect_i = sect_offset, sect_end = sect_offset + num_sectors;
			sect_i < sect_end; sect_i++)
			if (actual_uids[sect_i] != expected_uid) return FALSE;
	}
	else {
		UINT32 uid_addr;
		UINT8 sect_i, sect_end;
		for (sect_i = sect_offset,
			sect_end = sect_offset + num_sectors,
			uid_addr = uids_addr + sect_offset * sizeof(user_id_t);
			sect_i < sect_end;
			sect_i++, uid_addr += sizeof(user_id_t))
			if (read_dram_16(uid_addr) != expected_uid)
				return FALSE;
	}
	return TRUE;
}

void	pmt_authorize  (UINT32 const lpn, UINT8 const sect_offset,
			UINT8 const num_sectors, user_id_t const new_uid)
{
	UINT32	pmt_buf = NULL;
	UINT32	pmt_idx	= pmt_get_index(lpn);
	page_cache_get(pmt_idx, &pmt_buf, TRUE);
	BUG_ON("buffer is empty", pmt_buf == NULL);

	UINT32	uid_addr_begin = pmt_buf +
				pmt_get_offset(lpn) * sizeof(pmt_entry_t) +
				(UINT32)(&(((pmt_entry_t*)0)->uids[sect_offset]));
	user_id_t uids[SECTORS_PER_PAGE] =
		{[0 ... (SECTORS_PER_PAGE-1)] = new_uid};
	if (sect_offset % 2 == 1)
		uids[sect_offset - 1] = read_dram_16(uid_addr_begin -
						sizeof(user_id_t));
	if ((sect_offset + num_sectors) % 2 == 1)
		uids[sect_offset + num_sectors] =
			read_dram_16(uid_addr_begin +
					num_sectors * sizeof(user_id_t));

	/* assume user_id_t is 16-bits */
	/* address must be 4B align */
	UINT32	target_buf = uid_addr_begin -
				(sect_offset % 2 == 1) * sizeof(user_id_t),
		src_buf = (UINT32) & uids[align_even(sect_offset)];
	/* size must be 4B align*/
	UINT32	size = (num_sectors + (num_sectors % 2 == 1 ? 1 :
			(sect_offset % 2 == 1) * 2)) * sizeof(user_id_t);
	mem_copy(target_buf, src_buf, size);
}
#endif
#endif
