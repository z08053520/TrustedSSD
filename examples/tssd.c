/* define _GNU_SOURCE macro before including fcntl.h to enable O_DIRECT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include "tssd.h"

#define TSSD_CMD_SET_SESSION_KEY	_IOW('f', 20, unsigned long)

int tssd_open(const char* pathname, int flags, ...) {
	int mode;

	if(flags & O_CREAT) {
		va_list arg;
		va_start(arg, flags);
		mode = va_arg(arg, mode_t);
		va_end(arg);
	}

	return open(pathname, flags | O_DIRECT, mode);
}

void* tssd_malloc(size_t size) {
	void* res = NULL;
	if( posix_memalign(&res, TSSD_MEM_ALIGNMENT, size) )
		return NULL;
	return res;
}

void tssd_use_session_key(int fd, unsigned long skey) {
    ioctl(fd, TSSD_CMD_SET_SESSION_KEY, skey);
}

