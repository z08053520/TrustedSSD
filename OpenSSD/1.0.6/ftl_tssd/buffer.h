#ifndef __BUFFER_H
#define __BUFFER_H

/*
 * Buffer -- DRAM buffer management
 * */
#include "jasmine.h"

/*
 * Allocate a buffer and return the address.
 * Return NULL if no buffer available
 * */
UINT32 buffer_allocate();
/*
 * Free a buffer allocated before
 * */
void buffer_free(UINT32 buf);

#endif
