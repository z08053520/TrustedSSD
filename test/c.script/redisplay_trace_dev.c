#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>

typedef struct _thread_info {
	int pid;
	char* filename;
} thread_info_t;

const long long MAX_OFFSET = (long long)100 * (long long)1024 * 
                                  (long long)1024 * (long long)1024;

void *thread_handle_x(void *data) {

	unsigned long long total_bytes_write = 0;
	unsigned long long total_bytes_read  = 0;
	unsigned long long offset;
	int pid, nbytes, nresult;
	char wr;

	thread_info_t *thread_info = (thread_info_t *)data;
	char *file_name = thread_info->filename;
	int this_pid    = thread_info->pid;

	/* Read trace file */
	FILE *flip = fopen(file_name, "r");
	if (!flip) {
		printf("Failed to open %s !\n", file_name);
		return (void *)(0);
	}
	
	/* Open device to read/write */
	FILE *fp = fopen("/dev/sdb", "rt+");
	if (!fp) {
		printf("Failed to open /dev/sdb!\n");
		return (void *)(0);
	}
  
	struct timeval s_tv, f_tv;
	gettimeofday(&s_tv, NULL);

	nresult = fscanf(flip, "%d %c %lld %d", &pid, &wr, &offset, &nbytes);
	while (nresult == 4){
    		if (pid % 10 != this_pid) {
      			nresult = fscanf(flip, "%d %c %lld %d", &pid, &wr, &offset, &nbytes);
      			continue;
    		}

		char *buf = (char *)malloc(nbytes * sizeof(char));
		memset(buf, 'x', nbytes);
    		offset = (512 * offset) % MAX_OFFSET;
		fseek(fp, offset, SEEK_SET);

		if (wr == 'W' || wr == 'w') {
			total_bytes_write += nbytes;
			fwrite(buf, sizeof(char), nbytes, fp);
		}
		else {
			total_bytes_read += nbytes;
			fread(buf, sizeof(char), nbytes, fp);
		}

	  	nresult = fscanf(flip, "%d %c %lld %d", &pid, &wr, &offset, &nbytes);
		free(buf);
	}

	gettimeofday(&f_tv, NULL);

	long sec, usec;
	if (f_tv.tv_usec < s_tv.tv_usec) {
		usec = 1000000 + f_tv.tv_usec - s_tv.tv_usec;
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
  	
	fclose(fp);
	/* pthread_exit(NULL); */
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

