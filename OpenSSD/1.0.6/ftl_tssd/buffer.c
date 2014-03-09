#include "buffer.h"
#include "dram.h"

 /* 1 is available, 0 is occupied */
static UINT32 buffer_usage = 0xFFFFFFFF;

#define first_available_buf_id()	(__builtin_clz(buffer_usage))
#define mark_buf_occupied(id)		(buffer_usage &= ~(1 << (id)))
#define mark_buf_free(id)		(buffer_usage |= (1 << (id)))
#define is_buf_occupied(id)		((buffer_usage & (1 << (id))) == 0)

UINT32 buffer_allocate()
{
	if (buffer_usage == 0) return NULL;

	UINT8 buf_id = first_available_buf_id();
	mark_buf_occupied(buf_id);
	return MANAGED_BUF(buf_id);
}

void buffer_free(UINT32 buf)
{
	ASSERT(buf >= MANAGED_BUF_ADDR);

	UINT32 offset = buf - MANAGED_BUF_ADDR;
	ASSERT(offset % BYTES_PER_PAGE == 0);

	UINT8 buf_id = offset / BYTES_PER_PAGE;
	ASSERT(buf_id < NUM_MANAGED_BUFFERS);
	ASSERT(is_buf_occupied(buf_id));

	mark_buf_free(buf_id);
}
