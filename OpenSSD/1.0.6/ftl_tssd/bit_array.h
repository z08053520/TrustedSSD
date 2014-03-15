#ifndef __BIT_ARRAY
#define __BIT_ARRAY

/*
 * Bit Array
 * */

#include "jasmine.h"

#define DECLARE_BIT_ARRAY(name, n_bits)	\
		UINT32	name[COUNT_BUCKETS(n_bits, 32)] = {0}

inline void bit_array_set(UINT32 *array, UINT32 const i)
{
	array[i / 32] |= (1 << (i % 32));
}

inline void bit_array_clear(UINT32 *array, UINT32 const i)
{
	array[i / 32] &= ~(1 << i);
}

inline BOOL8 bit_array_test(UINT32 *array, UINT32 const i)
{
	return (array[i / 32] >> (i % 32)) & 1;
}

#endif
