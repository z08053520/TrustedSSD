#ifndef __FTL_WRITE_TASK
#define __FTL_WRITE_TASK
#include "task_engine.h"

void ftl_write_task_register();
void ftl_write_task_init(task_t *task,
#if	OPTION_ACL
			 user_id_t const uid,
#endif
			 UINT32 const lpn,
		   	 UINT8 const offset,
			 UINT8 const num_sectors);

#endif
