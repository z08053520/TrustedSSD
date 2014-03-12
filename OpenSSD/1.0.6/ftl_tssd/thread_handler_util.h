#ifndef __THREAD_HANDLER_UTIL_H
#define __THREAD_HANDLER_UTIL_H

/*
 * Thread handler -- The function that is executed by a thread
 *
 * Thread handler is a workload for a thread. Thread handler provides mechanism
 * to save and restore stack information so that the thread can resume its
 * work after the thread sleeps and then wakes up.
 *
 * To simulate context switch (yes, it is a simulation, as we can't save the
 * whole stack information), we must be able to save and restore variables
 * used by thread handlers between context switches.
 *
 * */

#include "thread.h"
#include "page_lock.h"

/*
 * Thread variables
 * */
#define begin_thread_stack		\
	struct thread_stack_t

#define end_thread_stack		\
	;

#define THREAD_STACK_SIZE		256
UINT8 __thread_stack[THREAD_STACK_SIZE];

#define var(name)	(((thread_stack_t*)__thread_stack)->name)

/*
 * Thread handler
 * */
#define begin_thread_handler					\
		static void __thread_handler(thread_t *__t) {	\
			restore_thread_variables(__t);		\
			jump_to_last_postion(__t);		\
		__begin:

#define end_thread_handler					\
			end();					\
		}

#define get_thread_handler()	__thread_handler

/*
 * Thread phase
 * */
#define phase(name)						\
		save_position(__t, name);			\
	__##name:

#define goto_phase(new_name)	do {				\
		save_position(__t, new_name);			\
		goto __##new_name;				\
	} while(0)

#define phase2offset(name)		(&&(__##name) - &&(__begin))
#define offset2phase(offset)		(*(&&(__begin) + offset))

/*
 * Context switch
 * */
#define sleep(signals)	do {					\
		__t->wakeup_signals = (signals);		\
		schedule(THREAD_SLEEPING);			\
	} while(0)

#define run_later()	do {					\
		__t->wakeup_signals = 0;			\
		schedule(THREAD_RUNNABLE);			\
	} while(0)

#define end()		do {					\
		__t-->state = THREAD_STOPPED;			\
		return;						\
	} while(0)

#define schedule(new_state)	do {				\
		__t-->state = (new_state);			\
		save_thread_variables(__t);			\
		return;						\
	} while(0)
/*
 * Save and restore thread information
 * */
#define jump_to_last_position(t)	\
		goto offset2phase((t)->handler_last_offset)
#define save_position(t, name)	\
		(t)->handler_last_offset = phase2offset(name)

void restore_thread_variables(thread_t *t);
void save_thread_variables(thread_t *t);

/*
 * Page lock
 * */
#define lock_page(lpn, lock_type)	\
		page_lock(thread_id(__t), (lpn), (lock_type))
#define unlock_page(lpn)		\
		page_unlock(thread_id(__t), (lpn))
#endif
#endif