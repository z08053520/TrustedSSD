#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#include "tssd.h"

#define BUFF_SIZE 512

int read_buffer_and_print(int fd, char *buff) {
	int cnt = read(fd, buff, BUFF_SIZE);
	if(cnt > 0) {
		buff[cnt] = 0;
		printf("%s", buff);
	}
	return cnt;
}

int main(int argc, char** argv) {
	if(argc < 3) {
		printf("Usage: read <file_path> <session_key>\n");	
		return -1;
    	}
	
	int fd = tssd_open(argv[1], O_RDONLY);
	if(fd < 0) {
		printf("Error: failed to open file\n");
		return -1;
	}
  	unsigned long session_key = strtoul(argv[2], NULL, 0);
	tssd_use_session_key(fd, session_key);
	char* buff = (char*) tssd_malloc(BUFF_SIZE + 1);
	if(!buff) {
		printf("Error: failed to allocate buffer\n");
		return -1;
	}

	int cnt;
	do {
		cnt = read_buffer_and_print(fd, buff);
		if(cnt < 0) {
			printf("Error: failed to read!\n");
			return -1;
		}
	} while(cnt);

    	free(buff);
	return 0;
}
