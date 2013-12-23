#ifndef __FLASH_UTIL_H
#define __FLASH_UTIL_H

#include "jasmine.h"

void fu_format(UINT32 const from_vblk);

UINT8 fu_get_idle_bank();

void fu_write_page	(const vp_t  vp,  const UINT32 buff_addr);
void fu_read_sub_page	(const vsp_t vsp, const UINT32 buff_addr);

#endif
