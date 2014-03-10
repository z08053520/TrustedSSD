#ifndef __FTL_WRITE_THREAD_H
#define __FTL_WRITE_THREAD_H

/*
 * FTL write thread -- a thread that handles a FTL read request to a page
 * */

#include "thread.h"

void ftl_write_thread_init(thread_t *t, UINT32 lpn, UINT8 sect_offset,
				UINT8 num_sectors);

#endif
