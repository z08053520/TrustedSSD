#ifndef __PAGE_CACHE_LOAD_TASK_H
#define __PAGE_CACHE_LOAD_TASK_H
#include "task_engine.h"
#include "gtd.h"

void page_cache_load_task_register();
void page_cache_load_task_init(task_t *task, UINT32 const pmt_idx);

#endif
