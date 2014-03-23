#ifndef __FTL_THREAD_H
#define __FTL_THREAD_H

#include "thread.h"
#include "sata_manager.h"

typedef struct {
	UINT32	lpn;
	UINT8	sect_offset;
	UINT8	num_sectors;
#if OPTION_ACL
	user_id_t uid;
#endif
} ftl_cmd_t;

void ftl_read_thread_init(thread_t *t, const ftl_cmd_t *cmd);
void ftl_write_thread_init(thread_t *t, const ftl_cmd_t *cmd);

#endif
