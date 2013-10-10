#ifndef __TSSD_H
#define __TSSD_H

#include <stddef.h>
#include <sys/types.h>

#define TSSD_MEM_ALIGNMENT		512

int tssd_open(const char* pathname, int flags, ...);
void* tssd_malloc(size_t size);
void tssd_use_session_key(int fd, unsigned long skey); 

#endif
