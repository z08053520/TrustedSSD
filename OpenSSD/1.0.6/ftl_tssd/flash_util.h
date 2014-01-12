#ifndef __FLASH_UTIL_H
#define __FLASH_UTIL_H

#include "jasmine.h"

#define FU_SYNC		0
#define FU_ASYNC	1

void fu_format(UINT32 const from_vblk);

UINT8 fu_get_idle_bank();

void fu_write_page	(vp_t  const  vp,  UINT32 const buff_addr);
void fu_read_sub_page	(vsp_t const  vsp, UINT32 const buff_addr, BOOL8 const is_async);

#endif
