#ifndef __FLASH_UTIL_H
#define __FLASH_UTIL_H

#include "flash.h"

void fu_read_page(UINT32 const bank, UINT32 const vpn, 
		  UINT32 const buff_addr, UINT32 const valid_sectors_mask);

#endif
