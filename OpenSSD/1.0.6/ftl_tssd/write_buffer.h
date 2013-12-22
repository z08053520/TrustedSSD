#ifndef __WRITE_BUFFER_H
#define __WRITE_BUFFER_H

#include "jasmine.h"
#include "ftl.h"

#define NUM_

void write_buffer_init();
void write_buffer_push(UINT32 const lpn, UINT8 const sector_offset, 
		       UINT8 const num_sectors);

#endif
