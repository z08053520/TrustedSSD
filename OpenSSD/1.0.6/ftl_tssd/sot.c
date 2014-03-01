#include "sot.h"
#if OPTION_ACL
#include "page_cache.h"
#include "mem_util.h"

/* ===========================================================================
 * Private Functions
 * =========================================================================*/

#define sot_get_index(lba)	(lba / SOT_ENTRIES_PER_SUB_PAGE)
#define sot_get_offset(lba)	(lba % SOT_ENTRIES_PER_SUB_PAGE)

static inline user_id_t read_uid(UINT32 const buff, UINT32 const offset)
{
	if (sizeof(UINT32) == sizeof(user_id_t))
		return read_dram_32(buff + sizeof(user_id_t) * offset);
	else
		return read_dram_16(buff + sizeof(user_id_t) * offset);
}

/* static inline void write_uid(UINT32 const buff, UINT32 const offset, user_id_t const uid) */
/* { */
/* 	if (sizeof(UINT32) == sizeof(user_id_t)) */
/* 		return write_dram_32(buff + sizeof(user_id_t) * offset, uid); */
/* 	else */
/* 		return write_dram_16(buff + sizeof(user_id_t) * offset, uid); */
/* } */

/* ===========================================================================
 * Public Functions
 * =========================================================================*/

void sot_init()
{
	INFO("sot>init", "# of SOT entries = %d, # of SOT pages = %d",
			SOT_ENTRIES, SOT_PAGES);
	BUG_ON("size of uid is neither 32-bit nor 16-bit",
			sizeof(user_id_t) != sizeof(UINT32) &&
			sizeof(user_id_t) != sizeof(UINT16));
}

task_res_t sot_load(UINT32 const lpn)
{
	// DEBUG
	/* uart_print("\t> sot_load: lpn = %u", lpn); */

	UINT32	lba	   = lpn * SECTORS_PER_PAGE;
	UINT32	sot_index  = sot_get_index(lba);
	page_key_t key	   = {.type = PAGE_TYPE_SOT, .idx = sot_index};
	return page_cache_load(key);
}

#define align_even(num)		(num / 2 * 2)

BOOL8	sot_authenticate(UINT32 const lpn, UINT8 const sect_offset,
			 UINT8 const num_sectors, user_id_t const expected_uid)
{
	// DEBUG
	/* uart_print("\t> sot_authenticate: lpn = %u", lpn); */

	UINT32	lba	= lpn * SECTORS_PER_PAGE + sect_offset;
	UINT32	index	= sot_get_index(lba);
	UINT32	offset	= sot_get_offset(lba);

	UINT32	sot_buf;
	page_key_t sot_key = {.type = PAGE_TYPE_SOT, .idx = index};
	page_cache_get(sot_key, &sot_buf, FALSE);

	if (num_sectors >= SECTORS_PER_PAGE / 2) {
		user_id_t actual_uids[SECTORS_PER_PAGE] = {0};
		/* assume user_id_t is 16-bits */
		/* address must be 4B align */
		UINT32	target_buf = (UINT32) & actual_uids[align_even(sect_offset)],
			src_buf = sot_buf + align_even(offset) * sizeof(user_id_t);
		/* size must be 4B align*/
		UINT32	size = (num_sectors + (num_sectors % 2 == 1) +
				(offset % 2 == 1 && num_sectors % 2 == 0))
				* sizeof(user_id_t);
		mem_copy(target_buf, src_buf, size);

		for (UINT8 sect_i = sect_offset, sect_end = sect_offset + num_sectors;
			sect_i < sect_end; sect_i++)
			if (actual_uids[sect_i] != expected_uid) return FALSE;
	}
	else {
		for (UINT8 sect_i = sect_offset, sect_end = sect_offset + num_sectors;
			sect_i < sect_end; sect_i++, offset++)
			if (read_uid(sot_buf, offset) != expected_uid) return FALSE;
	}
	return TRUE;
}

void	sot_authorize  (UINT32 const lpn, UINT8 const sect_offset,
			UINT8 const num_sectors, user_id_t const new_uid)
{
	// DEBUG
	/* uart_print("\t> sot_authorize: lpn = %u", lpn); */

	UINT32	lba	= lpn * SECTORS_PER_PAGE + sect_offset;
	UINT32	index	= sot_get_index(lba);
	UINT32	offset	= sot_get_offset(lba);

	UINT32	sot_buf;
	page_key_t sot_key = {.type = PAGE_TYPE_SOT, .idx = index};
	page_cache_get(sot_key, &sot_buf, TRUE);

	user_id_t uids[SECTORS_PER_PAGE] =
		{[0 ... (SECTORS_PER_PAGE-1)] = new_uid};
	if (sect_offset % 2 == 1)
		uids[sect_offset-1] = read_uid(sot_buf, offset-1);
	if ((sect_offset + num_sectors) % 2 == 1)
		uids[sect_offset + num_sectors] =
			read_uid(sot_buf, offset + num_sectors);

	/* assume user_id_t is 16-bits */
	/* address must be 4B align */
	UINT32	target_buf = sot_buf + align_even(offset) * sizeof(user_id_t),
		src_buf = (UINT32) & uids[align_even(sect_offset)];
	/* size must be 4B align*/
	UINT32	size = (num_sectors + (num_sectors % 2 == 1) +
			(offset % 2 == 1 && num_sectors % 2 == 0))
			* sizeof(user_id_t);
	mem_copy(target_buf, src_buf, size);
}

#endif
