#ifndef __SIGNAL_H
#define __SIGNAL_H
/*
 * Signal -- A signal is a notification that an event has occurred. Signals are
 * used to wake up threads when the time is right.
 * */
#include "jasmine.h"

typedef UINT32 signals_t;

#define SIG_BANK(i)		(1 << (i))
#define SIG_ALL_BANKS		0xFFFF
#define SIG_BANKS(banks)	(SIG_ALL_BANKS & (banks))
#define SIG_PMT_LOADED		(1 << 16)
#define SIG_PMT_READY		(1 << 17)

#define signals_clear(signals)			((signals) = 0)
#define signals_is_empty(signals)		((signals) == 0)
#define signals_set(signals, more_signals)	((signals) |= (more_signals))
#define signals_reset(signals, less_signals)	((signals) &= ~(less_signals))

#endif
