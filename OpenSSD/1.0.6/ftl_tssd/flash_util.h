#ifndef __FLASH_UTIL_H
#define __FLASH_UTIL_H

#include "jasmine.h"

#define FU_SYNC		0
#define FU_ASYNC	1

void fu_format(UINT32 const from_vblk);

UINT8 fu_get_idle_bank();

void fu_write_page	(vp_t  const  vp,  UINT32 const buff_addr);
void fu_read_sub_page	(vsp_t const  vsp, UINT32 const buff_addr, 
			 BOOL8 const is_async);

/* copy the masked part of src buffer to target buffer */
void fu_copy_buffer	(UINT32 const target_buf, UINT32 const src_buf, 
			 sectors_mask_t const mask);

#endif
