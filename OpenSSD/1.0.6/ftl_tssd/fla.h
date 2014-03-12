#ifndef __FLA_H
#define __FLA_H

/*
 * Fla -- FLAsh utility functions
 * */

#include "jasmine.h"


void fla_format_all(UINT32 const from_vblk);

void fla_update_bank_state();

BOOL8 fla_is_bank_idle(UINT8 const bank);
BOOL8 fla_is_bank_complete(UINT8 const bank);
UINT8 fla_get_idle_bank();

void fla_read_page(vp_t const vp, UINT8 const sect_offset,
			UINT8 const num_sectors, UINT32 const rd_buf);
void fla_write_page(vp_t const vp, UINT8 const sect_offset,
			UINT8 const num_sectors, UINT32 const wr_buf);

void fla_copy_buffer(UINT32 const target_buf, UINT32 const src_buf,
		    sectors_mask_t const mask);
#endif
