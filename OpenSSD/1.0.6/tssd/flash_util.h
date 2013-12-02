#ifndef __FLASH_UTIL_H
#define __FLASH_UTIL_H

#include "jasmine.h"

void fu_format(UINT32 const from_vblk);

void fu_read_page (UINT32 const bank, UINT32 const vpn, UINT32 const buff_addr, 
			UINT32 const valid_sectors_mask);
void fu_write_page(UINT32 const bank, UINT32 const vpn, UINT32 const buff_addr);

void fu_read_pages_in_parallel (UINT32 vpn[], UINT32 buff_addr[],
				UINT32 valid_sectors_mask[]);
void fu_write_pages_in_parallel(UINT32 vpn[], UINT32 buff_addr[]);

#endif
