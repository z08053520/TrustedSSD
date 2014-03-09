#ifndef __FTL_READ_THREAD_H
#define __FTL_READ_THREAD_H

/*
 * FTL read thread -- a thread that handles a FTL read request to a page
 * */

#include "thread.h"

void ftl_read_thread_init(thread_t *t, UINT32 lpn, UINT8 sect_offset,
				UINT8 num_sectors);

#endif
