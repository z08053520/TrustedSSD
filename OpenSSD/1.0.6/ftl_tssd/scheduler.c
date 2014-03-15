#include "scheduler.h"
#include "fla.h"

signals_t g_scheduler_signals = 0;

static thread_t _head = {
	.state = THREAD_SLEEPING,
	.next_id = NULL_THREAD_ID,
	.handler_id = NULL_THREAD_HANDLER_ID,
	.handler_last_offset = 0,
	.wakeup_signals = 0
};
static thread_t * const head = &_head;
static thread_t * tail = &_head;

static inline void remove(thread_t *this, thread_t *pre)
{
	thread_set_next(pre, thread_get_next(this));
	if (this == tail) tail = pre;
	thread_deallocate(this);
}

void schedule()
{
	g_scheduler_signals = 0;
	/* fla module check the state of banks and notify any state
	 * changes by signals (g_scheduler_signals) */
	fla_update_bank_state();

	thread_handler_t handler = NULL;
	thread_t *thread = thread_get_next(head), *pre = head;
	while (thread) {
		/* wake up sleep thread */
		if (thread->state == THREAD_SLEEPING &&
			(thread->wakeup_signals & g_scheduler_signals) != 0)
			thread->state = THREAD_RUNNABLE;

		if (thread->state != THREAD_RUNNABLE) goto next_thread;

		/* run current thread */
		handler = thread_handler_get(thread->handler_id);
		handler(thread);

		/* next thread */
		if (thread->state == THREAD_STOPPED) {
			remove(thread, pre);
		}
		else {
next_thread:
			pre = thread;
		}
		thread = thread_get_next(pre);
	}
}

void enqueue(thread_t *thread)
{
	ASSERT(thread->handler_id != NULL_THREAD_HANDLER_ID);
	thread->state = THREAD_RUNNABLE;
	thread->next_id = NULL_THREAD_ID;
	thread_set_next(tail, thread);
}
