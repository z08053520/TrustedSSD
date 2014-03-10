#include "buffer.h"
#include "dram.h"

 /* 1 is available, 0 is occupied */
static UINT32 buffer_usage = 0xFFFFFFFF;

#define first_available_buf_id()	(__builtin_clz(buffer_usage))
#define mark_buf_occupied(id)		(buffer_usage &= ~(1 << (id)))
#define mark_buf_free(id)		(buffer_usage |= (1 << (id)))
#define is_buf_occupied(id)		((buffer_usage & (1 << (id))) == 0)

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
	/* FIXME: can buffer run out? if there is no bug */
	ASSERT(buffer_usage > 0);

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
