#include "page_rw_lock.h"

#define MAX_NUM_LOCKS	64
static UINT32 locked_lpns[MAX_NUM_LOCKS] = {0};
static UINT8 rd_lock_counts[MAX_NUM_LOCKS] = {0};
static UINT32 num_locks = 0;

#define find_lpn(lpn)							\
		mem_search_equ_sram(locked_lpns,			\
			sizeof(UINT32), MAX_NUM_LOCKS, (lpn) + 1)

#define is_read_lock(lock_idx)						\
		(rd_lock_counts[(lock_idx)] > 0)
#define is_write_lock(lock_idx)						\
		(rd_lock_counts[(lock_idx)] == 0)

#define increase_read_lock_count(lock_idx)				\
		(++(rd_lock_counts[(lock_idx)]))
#define decrease_read_lock_count(lock_idx)				\
		(--(rd_lock_counts[(lock_idx)]))

#define release_lock_at(lock_idx)	do {				\
		locked_lpns[(lock_idx)] = 0;				\
		num_locks--;						\
	} while(0)

static UINT8 assign_lock_for(UINT32 const lpn) {
	UINT8 free_lock_idx = mem_search_equ_sram(locked_lpns,
					sizeof(UINT32), MAX_NUM_LOCKS, 0);
	locked_lpns[free_lock_idx] = lpn + 1;
	num_locks++;
	return free_lock_idx;
}


BOOL8 page_read_lock(UINT32 const lpn)
{
	UINT8 lock_idx = find_lpn(lpn);
	if (lock_idx >= MAX_NUM_LOCKS) {
		/* no more locks available */
		if (num_locks >= MAX_NUM_LOCKS) return PAGE_LOCK_DENIED;

		lock_idx = assign_lock_for(lpn);
		increase_read_lock(lock_idx);
		return PAGE_LOCK_GRANTED;
	}
	/* write lock is not compatible with read lock, so deny */
	if (is_write_lock(lock_idx)) return PAGE_LOCK_DENIED;
	/* increase read lock counter */
	increase_read_lock_count(lock_idx);
	return PAGE_LOCK_GRANTED;
}

BOOL8 page_write_lock(UINT32 const lpn)
{
	UINT8 lock_idx = find_lpn(lpn);
	/* write lock conflicts with any lock */
	if (lock_idx < MAX_NUM_LOCKS) return PAGE_LOCK_DENIED;

	/* no more locks available */
	if (num_locks >= MAX_NUM_LOCKS) return PAGE_LOCK_DENIED;

	assign_lock_for(lpn);
	return PAGE_LOCK_GRANTED;
}

void page_read_unlock(UINT32 const lpn)
{
	UINT8 lock_idx = find_lpn(lpn);
	ASSERT(lock_idx < MAX_NUM_LOCKS);
	ASSERT(is_read_lock(lock_idx));
	UINT8 rd_lock_count = decrease_read_lock_count(lock_idx);
	if (rd_lock_count == 0) release_lock_at(lock_idx);
}

void page_write_unlock(UINT32 const lpn)
{
	UINT8 lock_idx = find_lpn(lpn);
	ASSERT(lock_idx < MAX_NUM_LOCKS);
	ASSERT(is_write_lock(lock_idx));
	release_lock_at(lock_idx);
}
