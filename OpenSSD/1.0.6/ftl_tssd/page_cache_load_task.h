#ifndef __PAGE_CACHE_LOAD_TASK_H
#define __PAGE_CACHE_LOAD_TASK_H
#include "task_engine.h"

void page_cache_load_task_register();
void page_cache_load_task_init(task_t *task, UINT32 const lpn, 
		   		UINT8 const offset, UINT8 const num_sectors);

#endif
