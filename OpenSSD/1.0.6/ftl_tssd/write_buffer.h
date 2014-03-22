#ifndef __WRITE_BUFFER_H
#define __WRITE_BUFFER_H

#include "jasmine.h"

void write_buffer_init();

/*
 * Pull some part of a logical page from write buffer to buffer
 *
 *	Input *lpn* is the logical page number (LPN) of the page;
 *	Input *target_sectors* is the interesting sectors that you want to pull;
 *	Input *uid* is the current user id;
 *	Input *to_buf* is where data will be copied to.
 *
 * The return value is a sector mask that indicates which sectors is copied
 * from write buffer to *to_buf*.
 *
 * Note that write buffer respects ownership of a page. Sectors that is
 * in write buffer but is not owned by current user will be set to 0s.
 * */
sectors_mask_t write_buffer_pull(UINT32 const lpn,
				sectors_mask_t const target_sectors,
#if OPTION_ACL
				user_id_t const uid,
#endif
				UINT32 const to_buf);
/*
 * Push some part of a logical page into write buffer
 *
 *	Input *lpn* is the logical page number (LPN) of the page;
 *	Input *sector_offset* is the first sector to be copied into write buffer;
 *	Input *num_sectors* is the number of sectors to be copied into write buffer;
 *	Input *uid* indicates the onwer of the page;
 *	Input *from_buf* is where the data will be copied from.
 *
 * */
void write_buffer_push(UINT32 const lpn,
		      UINT8  const sector_offset,
		      UINT8  const num_sectors,
#if OPTION_ACL
		      user_id_t const uid,
#endif
		      UINT32 const from_buf);

/*
 * Drop all data of a logical page in write buffer regardless the owner.
 * */
void write_buffer_drop(UINT32 const lpn);

/*
 * Return whether write buffer is full.
 *	If true, the write buffer must be flushed first before push any more
 *	data.
 */
BOOL8 write_buffer_is_full();

/*
 * Flush a buffer in write buffer.
 *
 *	Output *managed_buf_id* is the id of the managed buffer (see buffer.h) that is
 *	flushed. The caller of this function is then responsible to free the
 *	managed buffer after using it.
 *	Output *valid_sectors* indicates which sectors in the buffer is valid;
 *	Output *sp_lpn* keeps the LPN of each sub-page in the flushed buffer;
 *	Output *uid* is the id of the owner of the flushed data;
 * */
void write_buffer_flush(UINT8 *managed_buf_id,
			sectors_mask_t *valid_sectors,
#if OPTION_ACL
			user_id_t *uid,
#endif
			UINT32 sp_lpn[SUB_PAGES_PER_PAGE]);

#endif
