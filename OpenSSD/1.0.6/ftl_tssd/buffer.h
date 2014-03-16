#ifndef __BUFFER_H
#define __BUFFER_H

/*
 * Buffer -- DRAM buffer management
 * */
#include "jasmine.h"

#define NULL_BUF_ID	0xFF

/*
 * Return the id of a managed buffer.
 *
 * If the buffer is not a managed buffer, return NULL_BUF_ID.
 * */
UINT8 buffer_id(UINT32 const buf);
/*
 * Allocate a buffer and return the buffer id.
 * Return NULL_BUF_ID if no buffer available
 * */
UINT8 buffer_allocate();
/*
 * Free a buffer allocated before
 * */
void buffer_free(UINT8 const buf_id);

#endif
