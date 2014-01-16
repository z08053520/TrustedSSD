#ifndef __WRITE_BUFFER_H
#define __WRITE_BUFFER_H

#include "jasmine.h"

void write_buffer_init();

void write_buffer_get(UINT32 const lpn, 
		      UINT32 *buf, 
		      sectors_mask_t *valid_sectors);
void write_buffer_put(UINT32 const lpn, 
		      UINT8  const sector_offset, 
		      UINT8  const num_sectors,
		      UINT32 const buf);

void write_buffer_drop(UINT32 const lpn);

BOOL8 write_buffer_is_full();
void write_buffer_flush(UINT32 const buf, 
			UINT32 *lspn, 
			sectors_mask_t *valid_sectors);
#endif
