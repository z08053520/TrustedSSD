#include "thread.h"
#include "slab.h"

/* Allocate thread objects statically */
define_slab_interface(thread, thread_t);
define_slab_implementation(thread, thread_t, MAX_NUM_THREADS);

#define thread2id(t)						\
		((t) ? (slab_thread_obj_t*)(t) - slab_thread_buf	\
			: NULL_THREAD_ID)
#define id2thread(id)						\
		((id) >= NULL_THREAD_ID ? NULL :		\
			(thread_t*)(& slab_thread_buf[id]))

BOOL8 thread_can_allocate()
{
	return slab_thread_num_free > 0;
}

thread_t* thread_allocate()
{
	thread_t* t = slab_allocate_thread();
	t->state	= THREAD_RUNNABLE;
	t->next_id	= NULL_THREAD_ID;
	t->handler_id	= NULL_THREAD_HANDLER_ID;
	t->handler_last_offset	= 0;
	t->wakeup_signals	= 0;
	return t;
}

void thread_deallocate(thread_t *t)
{
	slab_deallocate_thread(t);
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

#define MAX_NUM_THREAD_HANDLERS	4
static thread_handler_t handlers[MAX_NUM_THREAD_HANDLERS] = {NULL};
static UINT8 num_handlers = 0;

thread_handler_id_t thread_handler_register(thread_handler_t handler)
{
	ASSERT(handler != NULL);
	ASSERT(num_handlers < MAX_NUM_THREAD_HANDLERS);

	thread_handler_id_t handler_id = num_handlers;
	handlers[handler_id] = handler;
	num_handlers++;
	return handler_id;
}

thread_handler_t thread_handler_get(thread_handler_id_t const handler_id)
{
	ASSERT(handler_id < num_handlers);
	return handlers[handler_id];
}
