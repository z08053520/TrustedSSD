#ifndef __PAGE_RW_LOCK_H
#define __PAGE_RW_LOCK_H

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

#define PAGE_LOCK_GRANTED	0
#define PAGE_LOCK_DENIED	1

BOOL8 page_read_lock(UINT32 const lpn);
BOOL8 page_write_lock(UINT32 const lpn);
void page_read_unlock(UINT32 const lpn);
void page_write_unlock(UINT32 const lpn);

#endif
