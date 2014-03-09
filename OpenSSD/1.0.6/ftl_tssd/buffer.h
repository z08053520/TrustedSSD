#ifndef __BUFFER_H
#define __BUFFER_H

/*
 * Buffer -- DRAM buffer management
 * */
#include "jasmine.h"

#define NULL_BUF_ID	0xFF

/*
 * Allocate a buffer and return the address.
 * Return NULL if no buffer available
 * */
UINT8 buffer_allocate();
/*
 * Free a buffer allocated before
 * */
void buffer_free(UINT8 buf_id);

#endif
