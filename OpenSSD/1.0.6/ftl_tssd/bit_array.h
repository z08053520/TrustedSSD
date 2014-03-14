#ifndef __BIT_ARRAY
#define __BIT_ARRAY

/*
 * Bit Array
 * */

#include "jasmine.h"

#define DECLARE_BIT_ARRAY(name, n_bits)	\
		UINT32	name[COUNT_BUCKETS(n_bits, 32)] = {0}

#define bit_array_set(array, i)		\
		(name[(i) / 32] |= (1 << ((i) % 32)))

#define bit_array_clear(array, i)	\
		(name[(i) / 32] &= ~(1 << ((i) % 32)))

#define bit_array_test(array, i)	\
		((name[(i) / 32] >> ((i) % 32)) & 1)

#endif
