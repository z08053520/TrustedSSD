#include "page_cache_task.h"
#include "page_cache.h"
#include "page_cache_load_task.h"
#include "page_cache_flush_task.h"

task_res_t	page_cache_task_load(pc_key_t const key)
{
	if (page_cache_has(key)) {
		UINT8 flag;
		page_cache_get_flag(key, &flag);
		return is_reserved(flag) ? TASK_PAUSED : TASK_CONTINUED;	
	}

	if (!task_can_allocate(1)) return TASK_BLOCKED;

	/* Flush page cache */ 
	if (page_cache_is_full()) {
		BOOL8 need_flush = page_cache_evict();
		if (need_flush) {
			/* One load task plus one flush task */
			if (!task_can_allocate(2)) return TASK_BLOCKED;

			task_t	*pc_flush_task = task_allocate();
			page_cache_flush_task_init(pc_flush_task);
			task_engine_insert(pc_flush_task);
		}
	}

	/* Load missing page */
	task_t	*pc_load_task = task_allocate();
	page_cache_load_task_init(pc_load_task, key);
	task_engine_insert(pc_load_task);
}
