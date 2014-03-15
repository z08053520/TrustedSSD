#ifndef __PAGE_LOCK_H
#define __PAGE_LOCK_H

/*
 * Page read/write lock
 *
 * The intent of page r/w lock is to provide FTL code an mechanism to guarantee
 * the semantic correctness while concurrently executing.
 *
 * Use lock with caution:
 *	- One thread should never try to acquire lock that it has owned
 *	- One thread should never release a lock that it doesn't own
 *
 * */

#include "jasmine.h"

/*
 * Page lock schema: read-write-intent
 *
 * There are four types of lock for pages:
 *	- null lock (NU)
 *	- read lock (RD)
 *	- intent lock (IN)
 *	- write lock (WR)
 * Acquire read lock for read operation, write lock for write operation,
 * and intent lock if you want to write but cannot acquire the write lock right
 * now.
 *
 * Lock compatibility matrix:
 *
 *	NU	RD	IN	WR
 * NU	OK	OK	OK	OK
 * RD	OK	OK	OK	X
 * IN	OK	X	X	X
 * WR	OK	X	X	X
 *
 * */
typedef enum {
	PAGE_LOCK_NULL,
	PAGE_LOCK_READ,
	PAGE_LOCK_INTENT,
	PAGE_LOCK_WRITE,
	NUM_PAGE_LOCK_TYPES
} page_lock_type_t;

#define MAX_NUM_PAGE_LOCK_OWNERS	MAX_NUM_THREADS
typedef UINT8 page_lock_owner_id_t;

void page_lock_init();
/*
 * Acquire lock
 *
 * LPN is logial page number
 *
 * Possible return lock types (in order of priority):
 *	1) read lock or null lock, if you want to acquire read lock;
 *	2) write lock or intent lock or null lock, if you want to acquire write lock;
 *
 * Acquire null lock if you want to check the current lock acquired by owner.
 *
 * One owner can only acquire one lock for a page. Hige priority lock replaces
 * low priority (WR > IN > RD > NU).
 *
 * It is OK to lock a page again when you have already acquired the lock.
 * */
page_lock_type_t page_lock(page_lock_owner_id_t const owner_id,
				UINT32 const lpn,
				page_lock_type_t const lock_type);
/*
 * Release the lock of a page acquired by a owner
 *
 * It is OK to unlock a page that is not locked by a owner or any owner.
 * */
void page_unlock(page_lock_owner_id_t const owner_id, UINT32 const lpn);

#endif
