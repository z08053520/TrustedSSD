#include "thread.h"
#include "slab.h"

/* Allocate thread objects statically */
#define MAX_NUM_THREADS		16
define_slab_interface(thread, thread_t);
define_slab_implementation(thread, thread_t, MAX_NUM_THREADS);

/* thread_id_t is 6-bits */
#define NULL_THREAD_ID		0x3F;
#define NULL_THREAD_HANDLER_ID	0xFF;

#define thread2id(t)						\
		((t) ? (slab_thread_obj_t*)(t) - slab_thread_buf	\
			: NULL_THREAD_ID)
#define id2thread(id)						\
		((id) >= NULL_THREAD_ID ? NULL :		\
			(thread_t*)(& slab_thread_buf[id]))

#define MAX_NUM_THREAD_HANDLERS	8
thread_handler_t handlers[MAX_NUM_THREAD_HANDLERS] = {NULL};
UINT8 num_handlers = 0;

thread_t* thread_allocate()
{
	thread* t = slab_allocate_task();
	t->state	= THREAD_RUNNABLE;
	t->next_id	= NULL_THREAD_ID;
	t->handler_id	= NULL_THREAD_HANDLER_ID;
	t->handler_last_offset	= 0;
	t->wakeup_signals	= 0;
	return t;
}

void thread_deallocate(thread_t *t)
{
	slab_deallocate_task(t);
}

thread_id_t	thread_id(thread_t *t)
{
	return thread2id(t);
}

thread_t* thread_get_next(thread_t *t)
{
	return id2thread(t->next_id);
}

void thread_set_next(thread_t *t, thread_t *n)
{
	t->next_id = thread2id(n);
}

thread_handler_id_t thread_handler_register(thread_handler_t handler)
{
	ASSERT(handler != NULL);
	if (num_handlers >= MAX_NUM_THREAD_HANDLERS)
		return NULL_THREAD_HANDLER_ID;

	handlers[num_handlers] = handler;
	return num_handlers++;
}

thread_handler thread_handler_get(thread_handler_id_t handler_id)
{
	ASSERT(handler_id < num_handlers);
	return handlers[handler_id];
}
