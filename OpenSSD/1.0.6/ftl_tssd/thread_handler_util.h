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
UINT8 * const __thread_stack;

#define begin_thread_variables			\
	typedef struct {			\
		void* __handler_last_position;

#define end_thread_variables		\
	} thread_variables_t;

#define var(name)	(((thread_variables_t*)__thread_stack)->name)

/*
 * Thread handler
 * */
#define begin_thread_handler					\
		static void __thread_handler(thread_t *__t) {	\
			thread_id_t __tid = thread_id(__t);	\
			restore_thread_variables(__tid);	\
			jump_to_last_position(__t);

			/* uart_print("last position = %u",	\ */
			/* 		__t->__handler_last_position);\ */
			/* uart_print("enter thread handler %u",	\ */
			/* 		__t->handler_id);	\ */

#define end_thread_handler					\
			end();					\
		}

#define get_thread_handler()	__thread_handler

/*
 * Thread phase
 *
 * Note the semicolon after the colon of a label. This is a workaround
 * for the limitation of C standard that a label must belong to a statement.
 * */
#define phase(name)						\
		save_position(__t, name);			\
	__##name:;

/* uart_print("enter phase at line %u", __LINE__);	\ */

#define goto_phase(new_name)	do {				\
		save_position(__t, new_name);			\
		goto __##new_name;				\
	} while(0)

/*
 * Context switch
 * */
#define sleep(signals)	do {					\
		__t->wakeup_signals = (signals);		\
		context_switch(THREAD_SLEEPING);		\
	} while(0)

#define run_later()	do {					\
		__t->wakeup_signals = 0;			\
		context_switch(THREAD_RUNNABLE);		\
	} while(0)

#define end()		do {					\
		__t->state = THREAD_STOPPED;			\
		return;						\
	} while(0)

#define context_switch(new_state)	do {			\
		__t->state = (new_state);			\
		save_thread_variables(__tid);			\
		return;						\
	} while(0)
/*
 * Save and restore thread information
 * */
#define jump_to_last_position(t)				\
		if (var(__handler_last_position))			\
			goto *(var(__handler_last_position))
#define save_position(t, name)	\
		var(__handler_last_position)= &&__##name

#define init_thread_variables(tid)	do {		\
		var(__handler_last_position) = NULL;	\
		save_thread_variables(tid);		\
	} while(0)

void restore_thread_variables(thread_id_t const tid);
void save_thread_variables(thread_id_t const tid);

/*
 * Page lock
 * */
#define lock_page(lpn, lock_type)	\
		page_lock(__tid, (lpn), (lock_type))
#define unlock_page(lpn)		\
		page_unlock(__tid, (lpn))
#endif
