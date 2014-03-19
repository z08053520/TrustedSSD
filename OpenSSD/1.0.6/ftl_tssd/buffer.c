#include "buffer.h"
#include "dram.h"

#if NUM_MANAGED_BUFFERS >= 64
	#error too many managed buffers
#endif

 /* 1 is available, 0 is occupied */
static UINT64 buffer_usage = ((1ULL << (NUM_MANAGED_BUFFERS)) - 1);

#define first_available_buf_id()	(__builtin_ctzll(buffer_usage))
#define mark_buf_occupied(id)		(buffer_usage &= ~(1ULL << (id)))
#define mark_buf_free(id)		(buffer_usage |= (1ULL << (id)))
#define is_buf_occupied(id)		((buffer_usage & (1ULL << (id))) == 0)

UINT8 buffer_id(UINT32 const buf)
{
	if (buf < MANAGED_BUF_ADDR ||
		buf >= MANAGED_BUF_ADDR + MANAGED_BUF_BYTES)
		return NULL_BUF_ID;
	UINT8 buf_id = (buf - MANAGED_BUF_ADDR) / BYTES_PER_PAGE;
	return buf_id;
}

UINT8 buffer_allocate()
{
	/* 64 buffers should be enough */
	ASSERT(buffer_usage != 0);

	UINT8 buf_id = first_available_buf_id();
	mark_buf_occupied(buf_id);
	return buf_id;
}

void buffer_free(UINT8 buf_id)
{
	ASSERT(buf_id < NUM_MANAGED_BUFFERS);
	ASSERT(is_buf_occupied(buf_id));

	mark_buf_free(buf_id);
}
