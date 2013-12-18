#include "sot.h"
#if OPTION_ACL
#include "mem_util.h"
#include "buffer_cache.h"

/* ===========================================================================
 * Private Functions 
 * =========================================================================*/

#define sot_get_index(lba)	(lba / SOT_ENTRIES_PER_PAGE)
#define sot_get_offset(lba)	(lba % SOT_ENTRIES_PER_PAGE)

static UINT32 load_sot_buffer(UINT32 const sot_index)
{
	UINT32 sot_buff;

	bc_get(sot_index, &sot_buff, BC_BUF_TYPE_SOT);
	if (sot_buff == NULL) {
		bc_put(sot_index, &sot_buff, BC_BUF_TYPE_SOT);
		bc_fill_full_page(sot_index, BC_BUF_TYPE_SOT);
	}
	return sot_buff;
}

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
	UINT32 buff	= load_sot_buffer(index);
	
	*uid = read_uid(buff, offset);
}

void sot_update(UINT32 const lba, uid_t const uid)
{
	UINT32 index 	= sot_get_index(lba);
	UINT32 offset 	= sot_get_offset(lba);
	UINT32 buff	= load_sot_buffer(index);
	
	write_uid(buff, offset, uid);
	
	bc_set_dirty(index, BC_BUF_TYPE_SOT);
}

#endif
