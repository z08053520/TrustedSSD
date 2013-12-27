#ifndef __WRITE_BUFFER_H
#define __WRITE_BUFFER_H

#include "jasmine.h"

void write_buffer_init();

void write_buffer_get(UINT32 const lspn, 
		      UINT8  const sector_offset_in_sp, 
		      UINT8  const num_sectors_in_sp, 
		      UINT32 *buf);
void write_buffer_put(UINT32 const lpn, 
		      UINT8  const sector_offset, 
		      UINT8  const num_sectors,
		      UINT32 const sata_wr_buf);
void write_buffer_drop(UINT32 const lpn);
#endif
