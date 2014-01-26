#ifndef __PAGE_CACHE_FLUSH_TASK_H
#define __PAGE_CACHE_FLUSH_TASK_H
#include "task_engine.h"

void page_cache_flush_task_register();
void page_cache_flush_task_init(task_t *task);

#endif
