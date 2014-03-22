#include "read_buffer.h"
#include "dram.h"

void read_buffer_init()
{
	// read buffer #0 is always 0xFF...FF
	mem_set_dram(ALL_ONE_BUF, 0xFFFFFFFF, BYTES_PER_PAGE);
	// read buffer #1 is always 0x00...00
	mem_set_dram(ALL_ZERO_BUF, 0x00000000, BYTES_PER_PAGE);
}

void read_buffer_get(vp_t const vp, UINT32 *buff)
{
	*buff = (vp.vpn == NULL ? ALL_ONE_BUF : NULL);
}
