#include "page_lock.h"
#include "dram.h"

#define MAX_NUM_LOCKS_PER_OWNER		SUB_PAGES_PER_PAGE
#define MAX_NUM_LOCKS			(MAX_NUM_LOCKS_PER_OWNER * \
					MAX_NUM_PAGE_LOCK_OWNERS)
static UINT32 num_locks = 0;

#define PL_LPNS_ADDR			PL_ADDR
#define PL_LPN(i)			(PL_LPNS_ADDR + sizeof(UINT32) * (i))
#define PL_OWNERS_INFO_ADDR		(PL_LPNS_ADDR + sizeof(UINT32) * MAX_NUM_LOCKS)
#define PL_OWNERS_INFO(i)		(PL_OWNERS_INFO_ADDR + sizeof(UINT32) * (i))

#define find_lpn(lpn)							\
		mem_search_equ_dram(PL_LPNS_ADDR,			\
			sizeof(UINT32), MAX_NUM_LOCKS, (lpn))

/* owners info of a locked page
 *
 *	i-th bit:	31 30 | 29 28 | 27 26 | ... | 3 2 | 1 0
 *	----------------------|-------|-------|-----|-----|----
 *	(i/2)-th owner:	15-th | 14-th | 13-th | ... |1-th |0-th
 *			owner | owner | owner | ... |owner|owner
 *			lock  | lock  | lock  | ... |lock |lock
 *
 * There are at most 16 owners (threads).
 * */
typedef UINT32 owners_info_t;

#define get_owners_info(lock_idx)			\
		read_dram_32(PL_OWNERS_INFO(lock_idx))
#define set_owners_info(lock_idx, owners_info)		\
		write_dram_32(PL_OWNERS_INFO(lock_idx), (owners_info))
#define get_owner_lock_type(owners_info, owner_id)	\
		(((owners_info) >> ((owner_id)* 2)) & 0x03)
#define set_owner_lock_type(owners_info, owner_id, lock_type)	do {	\
		(owners_info) &= ~(0x03 << ((owner_id) * 2));		\
		(owners_info) |= ((0x03 & (lock_type)) << ((owner_id) * 2));\
	} while (0)

#define release_lock_at(lock_idx)	do {				\
		write_dram_32(PL_LPN(lock_idx), NULL_LPN);	\
		num_locks--;						\
	} while(0)

static UINT8 assign_lock_for(UINT32 const lpn) {
	ASSERT(num_locks < MAX_NUM_LOCKS);
	UINT8 free_lock_idx = mem_search_equ_dram(PL_LPNS_ADDR, sizeof(UINT32),
						MAX_NUM_LOCKS, NULL_LPN);
	write_dram_32(PL_LPN(free_lock_idx), lpn);
	num_locks++;
	return free_lock_idx;
}

void page_lock_init() {
	/* one page can be locked by at most 16 different lock owners */
	ASSERT(MAX_NUM_PAGE_LOCK_OWNERS <= 16);

	mem_set_dram(PL_LPNS_ADDR, NULL_LPN, sizeof(UINT32) * MAX_NUM_LOCKS);
	mem_set_dram(PL_OWNERS_INFO_ADDR, 0, sizeof(UINT32) * MAX_NUM_LOCKS);
}

static page_lock_type_t _highest_compatible_lock[NUM_PAGE_LOCK_TYPES] = {
	PAGE_LOCK_WRITE,
	PAGE_LOCK_IN,
	PAGE_LOCK_NULL,
	PAGE_LOCK_NULL
};
#define get_highest_compatible_lock(lock_type)	\
		(_highest_compatible_lock[lock_type])

/* write lock is the highest, while null lock is the lowest */
static inline page_lock_type_t get_highest_lock_except_owner(
					owners_info_t const owners_info,
					page_lock_owner_id_t const except_owner_id)
{
	if (owners_info == 0) return PAGE_LOCK_NULL;

	page_lock_type_t highest_lock = PAGE_LOCK_NULL;
	UINT8 owner_id;
	for (owner_id = 0; owner_id < MAX_NUM_PAGE_LOCK_OWNERS; owner_id++) {
		if (owner_id == except_owner_id) continue;
		page_lock_type_t lock = get_owner_lock_type(
						owners_info, owner_id);
		if (lock > highest_lock) highest_lock = lock;
	}
	return highest_lock;
}

page_lock_type_t page_lock(page_lock_owner_id_t const owner_id,
				UINT32 const lpn,
				page_lock_type_t const new_lock)
{
	ASSERT(owner_id < MAX_NUM_PAGE_LOCK_OWNERS);
	UINT8 lock_idx = find_lpn(lpn);
	/* if the page has never been locked */
	if (lock_idx >= MAX_NUM_LOCKS) lock_idx = assign_lock_for(lpn);

	/* determine appropriate lock */
	owners_info_t owners_info = get_owners_info(lock_idx);
	page_lock_type_t old_lock = get_owner_lock_type(owners_info, owner_id);
	page_lock_type_t highest_lock_except_owner =
			get_highest_lock_except_owner(owners_info, owner_id);
	page_lock_type_t highest_compatible_lock =
			get_highest_compatible_lock(highest_lock_except_owner);
	page_lock_type_t final_lock = MIN(highest_compatible_lock,
					MAX(new_lock, old_lock));

	/* lock granted and need to update DRAM */
	if (final_lock != PAGE_LOCK_NULL && final_lock != old_lock) {
		set_owner_lock_type(owners_info, owner_id, final_lock);
		set_owners_info(lock_idx, owners_info);
	}
	return final_lock;
}

void page_unlock(page_lock_owner_id_t const owner_id, UINT32 const lpn)
{
	ASSERT(owner_id < MAX_NUM_PAGE_LOCK_OWNERS);
	UINT8 lock_idx = find_lpn(lpn);
	if (lock_idx >= MAX_NUM_LOCKS) return;

	owners_info_t owners_info = get_owners_info(lock_idx);
	page_lock_type_t old_lock = get_owner_lock_type(owners_info, owner_id);
	if (old_lock == PAGE_LOCK_NULL) return;

	set_owner_lock_type(owners_info, owner_id, PAGE_LOCK_NULL);
	set_owners_info(lock_idx, owners_info);
	/* if this page is not locked by any owner */
	if (owners_info == 0) release_lock_at(lock_idx);
}
