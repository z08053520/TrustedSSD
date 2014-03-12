#ifndef __THREAD_H
#define __THREAD_H
/*
 * Thread -- an user-space multithreading implementation.
 *
 * The main motivation of multithreading is to maximize the utility of
 * flash modules. Threads can be executed indepedently with each other.
 * When I/O operations happen in a thread, the thread will be put into
 * sleep and waken up (by signals) as soon as the I/O operation is done.
 *
 * */
#include "signal.h"

typedef UINT8 thread_id_t;
/* thread_id_t is 6-bits */
#define NULL_THREAD_ID		0x3F;

typedef enum {
	THREAD_RUNNABLE,
	THREAD_SLEEPING,
	THREAD_STOPPED
} thread_state_t;

typedef void (*thread_handler_t)(thread_t *__t);
typedef UINT8 thread_handler_id_t;
#define NULL_THREAD_HANDLER_ID	0xFF;

typedef struct {
	thread_state_t		state:2;
	thread_id_t		next_id:6;
	thread_handler_id_t	handler_id;
	UINT16			handler_last_offset;
	signals_t		wakeup_signals;
} thread_t;

thread_t*	thread_allocate();
void		thread_deallocate(thread_t *t);

thread_id_t	thread_id(thread_t *t);
thread_t*	thread_get_next(thread_t *t);
void		thread_set_next(thread_t *t, thread_t *n);

thread_handler_id_t	thread_handler_register(thread_handler_t handler);
thread_handler		thread_handler_get(thread_handler_id_t const handler_id);
#endif
