#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#include "tssd.h"

#define BUFF_SIZE TSSD_MEM_ALIGNMENT 

int main(int argc, char** argv) {
	if(argc < 4) {
		printf("Usage: write <file_path> <content> <session_key>\n");	
		return -1;
    	}
	
	int fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 
				    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | 
				    S_IROTH | S_IWOTH );
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
	/* As the file is opened in O_DIRECT mode, we must write in units of 
	 * 512-bytes sector or the write would fail */
	int cnt = write(fd, buff, BUFF_SIZE);
	if(cnt < 0) {
		printf("Error: failed to write!\n");
		free(buff);
		return -1;
	}
    	free(buff);
	return 0;
}
