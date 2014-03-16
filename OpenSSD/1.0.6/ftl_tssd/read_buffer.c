#include "read_buffer.h"
#include "dram.h"

void read_buffer_init()
{
	// read buffer #0 is always 0xFF...FF
	mem_set_dram(READ_BUF(0), 0xFFFFFFFF, BYTES_PER_PAGE);
}

void read_buffer_get(vp_t const vp, UINT32 *buff)
{
	*buff = (vp.vpn == NULL ? READ_BUF(0) : NULL);
}
