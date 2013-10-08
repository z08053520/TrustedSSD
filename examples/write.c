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

int main(int argc, char** argv) {
	if(argc < 4) {
		printf("Usage: write <file_path> <content> <session_key>\n");	
		return -1;
    	}
	
	int fd = tssd_open(argv[1], O_RDWR | O_CREAT, 
				    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
	if(fd < 0) {
		printf("Error: failed to open file\n");
		return -1;
	}

  	unsigned long session_key = strtoul(argv[3], NULL, 0);
	tssd_use_session_key(fd, session_key);

	char* buff = (char*) tssd_malloc(BUFF_SIZE);
	if(!buff) {
		printf("Error: failed to allocate buffer\n");
		return -1;
	}
	memset(buff, 0, BUFF_SIZE);
	strcpy(buff, argv[2]);
	int cnt = write(fd, buff, strlen(argv[2]));
	if(cnt < 0) {
		printf("Error: failed to write!\n");
		free(buff);
		return -1;
	}
    	free(buff);
	return 0;
}
