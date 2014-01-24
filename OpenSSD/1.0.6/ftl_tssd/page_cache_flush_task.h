#ifndef __PAGE_CACHE_FLUSH_TASK_H
#define __PAGE_CACHE_FLUSH_TASK_H
#include "task_engine.h"

void task_engine_flush_task_register();
void task_engine_flush_task_init(task_t *task, UINT32 const lpn, 
		   		UINT8 const offset, UINT8 const num_sectors);

#endif
