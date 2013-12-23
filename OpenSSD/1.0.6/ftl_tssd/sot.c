#include "sot.h"
#if OPTION_ACL
#include "mem_util.h"

/* ===========================================================================
 * Private Functions 
 * =========================================================================*/

#define sot_get_index(lba)	(lba / SOT_ENTRIES_PER_SUB_PAGE)
#define sot_get_offset(lba)	(lba % SOT_ENTRIES_PER_SUB_PAGE)

static uid_t read_uid(UINT32 const buff, UINT32 const offset)
{
	if (sizeof(UINT32) == sizeof(uid_t))
		return read_dram_32(buff + sizeof(uid_t) * offset);
	else
		return read_dram_16(buff + sizeof(uid_t) * offset);
}

static void write_uid(UINT32 const buff, UINT32 const offset, uid_t const uid)
{
	if (sizeof(UINT32) == sizeof(uid_t))
		return write_dram_32(buff + sizeof(uid_t) * offset, uid);
	else
		return write_dram_16(buff + sizeof(uid_t) * offset, uid);
}

/* ===========================================================================
 * Public Functions 
 * =========================================================================*/

void sot_init()
{
	INFO("sot>init", "# of SOT entries = %d, # of SOT pages = %d",
			SOT_ENTRIES, SOT_PAGES);
	BUG_ON("size of uid is neither 32-bit nor 16-bit", 
			sizeof(uid_t) != sizeof(UINT32) && 
			sizeof(uid_t) != sizeof(UINT16));
}

void sot_fetch(UINT32 const lba, uid_t *uid)
{
	UINT32 index 	= sot_get_index(lba);
	UINT32 offset 	= sot_get_offset(lba);
	UINT32 buff;
	page_cache_load(index, &buff, PC_BUF_TYPE_SOT, FALSE);

	*uid = read_uid(buff, offset);
}

void sot_update(UINT32 const lba, uid_t const uid)
{
	UINT32 index 	= sot_get_index(lba);
	UINT32 offset 	= sot_get_offset(lba);
	UINT32 buff;
	page_cache_load(index, &buff, PC_BUF_TYPE_SOT, TRUE);
	
	write_uid(buff, offset, uid);
}

BOOL8 sot_check(UINT32 const lba_begin, UINT32 const num_sectors, 
		uid_t const expected_uid)
{
	UINT32 lba = lba_begin, lba_end = lba_begin + num_sectors;
	uid_t actual_uid;

	while (lba < lba_end) {
		sot_fetch(lba, &actual_uid);
		if (actual_uid != 0 && actual_uid != expected_uid) 
			return FALSE;

		lba ++;
	}
	return TRUE; 
}

void sot_set(UINT32 const lba_begin, UINT32 const num_sectors, 
	     uid_t const new_uid)
{
	UINT32 lba = lba_begin, lba_end = lba_begin + num_sectors;

	while (lba < lba_end) {
		sot_update(lba, new_uid);

		lba++;
	}
}

#endif
