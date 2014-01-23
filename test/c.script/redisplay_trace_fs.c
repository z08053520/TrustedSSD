/* 
 * Two solutions to solve the limitation of C Language
 * which normally only can open the file whose size is
 * (less than) limited to 2GiB under 32bits OS
 * Solution 1 : 
 *          #define _GNU_SOURCE
 *          int fd = open("large.file", O_RDONLY | O_LARGEFILE)
 * Solution 2 :
 *          #define __USE_FILE_OFFSET64
 *          #define __USE_LARGEFILE
 *          #define _LARGEFILE64_SOURCE
 *          int fd = open("large.file", O_RDONLY | O_LARGEFILE)
 * PS : all the two solutions should put the macro defines 
 *      before any include<headfiles> due to the facts that some
 *      head files are macro defines dependened, and for your 
 *      good, you can put '-D_FILE_OFFSET_BITS=64' when you use
 *      gcc command to do the compile and link.
*/

#define __USE_FILE_OFFSET64
#define __USE_LARGEFILE
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

const int FILE_DESC_LEN    = 20;
const int FS_FILE_PATH_LEN = 128;
const long long MAX_OFFSET = 107374182400L; // 100 GiB

typedef struct _thread_info {
	int pid;
	char* filename;
} thread_info_t;

typedef int FILE_DESC;

typedef struct _file_info {
	int fptr;
	char* filename;   // char filename[FS_FILE_PATH_LEN];
} file_info_t;

void get_file_info(char *filepath, file_info_t *file_info) {
	int idx    = 0;
	int flag   = 0;
	int fptr_t = 0;
                    // char filename_t[FS_FILE_PATH_LEN];
	char* filename_t = (char *)malloc(FS_FILE_PATH_LEN * sizeof(char));
	memset(filename_t, 0, FS_FILE_PATH_LEN);

	while (*(filepath + idx) != 0) {
		if (*(filepath + idx) == '(') {
			flag = 1;
			idx++;
    		}

    		if (!flag) {
      			filename_t[idx] = *(filepath + idx);
    		}
    		else {
      			if (*(filepath + idx) == ')') break;
      			fptr_t = 10 * fptr_t + *(filepath + idx) - '0';
      			filename_t[idx] = 0;
    		}
    		idx++;
  	}
                    // strcpy(file_info->filename, filename_t);
	file_info->filename = filename_t;
	file_info->fptr     = fptr_t;
}

void *thread_handle_x(void *data) {
	unsigned long long total_bytes_write = 0;
	unsigned long long total_bytes_read  = 0;

	int i = 0;
  	FILE_DESC *fd_ptr = (FILE_DESC *)malloc(FILE_DESC_LEN * sizeof(FILE_DESC));
  	for (i = 0; i < FILE_DESC_LEN; ++i) {
    		fd_ptr[i] = -1;
  	}

	unsigned long long offset;
	int pid, nbytes, nresult;
	char wr;
	char fs_file_path[FS_FILE_PATH_LEN];
	memset(fs_file_path, 0, FS_FILE_PATH_LEN);

	thread_info_t *thread_info = (thread_info_t *)data;
	char *file_name = thread_info->filename;
	int  this_pid   = thread_info->pid;
	
  	struct timeval s_tv, f_tv;
	gettimeofday(&s_tv, NULL);

	FILE *flip = fopen(file_name, "r");
	if (!flip) {
		printf("Failed to open %s !\n", file_name);
		return (void *)(0);
	}
  
  	int cnt = 0;
	nresult = fscanf(flip, "%d %c %lld %d %s", &pid,
                          	 &wr, &offset, &nbytes, fs_file_path);
	while (nresult == 5) {
    		cnt ++;
    		if (pid % 10 != this_pid) {
      			memset(fs_file_path, 0, FS_FILE_PATH_LEN);
      			nresult = fscanf(flip, "%d %c %lld %d %s", &pid,
                        			&wr, &offset, &nbytes, fs_file_path);
      			continue;
    		}

    		file_info_t file_info;
    		get_file_info(fs_file_path, &file_info);
   
 	   	char sz_file_name[FS_FILE_PATH_LEN];
    		strcpy(sz_file_name, file_info.filename);
    		FILE_DESC fd; 
    		if (fd_ptr[file_info.fptr] == -1) {
      			fd = open(sz_file_name, O_RDWR | O_LARGEFILE);
		    	if (fd < 0) {
				printf("Failed to open %s !\n", sz_file_name);
       				nresult = fscanf(flip, "%d %c %lld %d %s", &pid,
                             		&wr, &offset, &nbytes, fs_file_path);
       				continue;
	   		}
      			fd_ptr[file_info.fptr] = fd;
    		} 
 		else fd = fd_ptr[file_info.fptr];

		char *buf = (char *)malloc(nbytes * sizeof(char));
		memset(buf, 'x', nbytes);
    		offset = (512 * offset) % MAX_OFFSET;
		lseek(fd, offset, SEEK_SET);
		if (wr == 'W' || wr == 'w') {
			total_bytes_write += nbytes;
			write(fd, buf, nbytes);
		}
		else {
			total_bytes_read += nbytes;
			read(fd, buf, nbytes);
		}
	  
    		memset(fs_file_path, 0, FS_FILE_PATH_LEN);
    		nresult = fscanf(flip, "%d %c %lld %d %s", &pid,
       		                      &wr, &offset, &nbytes, fs_file_path);
		free(buf);
	}	// while

	gettimeofday(&f_tv, NULL);
	long sec, usec;
	if (f_tv.tv_usec < s_tv.tv_usec) {
		usec = f_tv.tv_usec - s_tv.tv_usec + 1000000; 
		sec  = f_tv.tv_sec - s_tv.tv_sec - 1;
	}
	else {
		usec = f_tv.tv_usec - s_tv.tv_usec;
		sec  = f_tv.tv_sec - s_tv.tv_sec;
	}
	
  	printf("\n\tThread id : %d\n", this_pid);
	printf("\t%s Total Bytes Write : %llu MiB\n", file_name, 
                                   total_bytes_write / 1024 / 1024);
	printf("\t%s Total Bytes Read  : %llu MiB\n", file_name,
                                   total_bytes_read / 1024 / 1024);
	printf("\t%s ----------------- : %03ld seconds %07ld microseconds\n",
                                   file_name, sec, usec);
	printf("\t%s Total     Entries : %d\n", file_name, cnt);

  	for (i = 0; i < FILE_DESC_LEN; ++i) {
    		if (fd_ptr[i] != -1) close(fd_ptr[i]);
  	}
  	
  	// pthread_exit(NULL);
}

int main(int argc, char **argv){

	if (argc < 2) {
		printf("\t\nYou should specified the file name to run the test !\n");
		return -1;
	}

	int i;
	pthread_t thread_x[10];
  	thread_info_t thread_info[10]; 

	for (i = 0; i < 10; i++) {
		thread_info[i].filename = argv[1];
    		thread_info[i].pid = i;  
    		pthread_create(&thread_x[i], NULL, thread_handle_x, &thread_info[i]);
	}
  
  	for (i = 0; i < 10; i++){
    		pthread_join(thread_x[i], NULL);
  	}

	return 0;
}

