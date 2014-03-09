#ifndef __SIGNAL_H
#define __SIGNAL_H
/*
 * Signal -- A signal is a notification that an event has occurred. Signals are
 * used to wake up threads when the time is right.
 * */
#include "jasmine.h"

typedef UINT32 signal_t;

#define SIG_BANK(i)		(1 << (i))
#define SIG_PMT_LOAD		(1 << 16)
#define SIG_PMT_FLUSHED		(1 << 17)

#define signal_clear(signal)			((signal) = 0)
#define signal_set(signal, more_signal)		((signal) |= (more_signal))
#define signal_reset(signal, less_signal)	((signal) &= ~(less_signal))

#endif
