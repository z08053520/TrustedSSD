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

static inline void write_uid(UINT32 const buff, UINT32 const offset, user_id_t const uid)
{
	if (sizeof(UINT32) == sizeof(user_id_t))
		return write_dram_32(buff + sizeof(user_id_t) * offset, uid);
	else
		return write_dram_16(buff + sizeof(user_id_t) * offset, uid);
}

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
	UINT32	lba	   = lpn * SECTORS_PER_PAGE;
	UINT32	sot_index  = sot_get_index(lba);
	page_key_t key	   = {.type = PAGE_TYPE_SOT, .idx = sot_index};
	return page_cache_load(key) == TASK_CONTINUED ? TASK_CONTINUED
						      : TASK_PAUSED;
}

BOOL8	sot_authenticate(UINT32 const lpn, UINT8 const sect_offset,
			 UINT8 const num_sectors, user_id_t const expected_uid)
{
	UINT32	lba	= lpn * SECTORS_PER_PAGE + sect_offset;
	UINT32	index	= sot_get_index(lba);
	UINT32	offset	= sot_get_offset(lba);

	UINT32	sot_buf;
	page_key_t sot_key = {.type = PAGE_TYPE_SOT, .idx = index};
	page_cache_get(sot_key, &sot_buf, FALSE);

	UINT8	i;
	for (i = 0; i < num_sectors; i++) {
		user_id_t	actual_uid = read_uid(sot_buf, offset);
		if (actual_uid != expected_uid) return FALSE;
		offset++;
	}
	return TRUE;
}

void	sot_authorize  (UINT32 const lpn, UINT8 const sect_offset,
			UINT8 const num_sectors, user_id_t const new_uid)
{
	UINT32	lba	= lpn * SECTORS_PER_PAGE + sect_offset;
	UINT32	index	= sot_get_index(lba);
	UINT32	offset	= sot_get_offset(lba);

	UINT32	sot_buf;
	page_key_t sot_key = {.type = PAGE_TYPE_SOT, .idx = index};
	page_cache_get(sot_key, &sot_buf, FALSE);

	UINT8	i;
	for (i = 0; i < num_sectors; i++) {
		write_uid(sot_buf, offset, new_uid);
		offset++;
	}
}

#endif
