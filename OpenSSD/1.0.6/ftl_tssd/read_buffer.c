#include "read_buffer.h"
#include "mem_util.h"

void read_buffer_init()
{
	// read buffer #0 is always 000...000
	mem_set_dram(READ_BUF(0), 0, BYTES_PER_PAGE);
}

void read_buffer_get(UINT32 const vpn, UINT32 *buff)
{
	*buff = vpn == NULL ? READ_BUF(0) : NULL;	
}
