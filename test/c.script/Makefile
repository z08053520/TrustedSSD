#!/usr/bin/bash

.PHONY : all
all : redisplay_trace_dev redisplay_trace_fs 

redisplay_trace_dev : redisplay_trace_dev.o
	gcc -pthread -o redisplay_trace_dev redisplay_trace_dev.o
redisplay_trace_dev.o : redisplay_trace_dev.c
	gcc -pthread -c redisplay_trace_dev.c -o redisplay_trace_dev.o

redisplay_trace_fs : redisplay_trace_fs.o
	gcc -D_FILE_OFFSET_BITS=64 -pthread -o redisplay_trace_fs redisplay_trace_fs.o
redisplay_trace_fs.o : redisplay_trace_fs.c
	gcc -D_FILE_OFFSET_BITS=64 -pthread -c redisplay_trace_fs.c -o redisplay_trace_fs.o

.PHONY : clean
clean :
	-rm *.o redisplay_trace_dev redisplay_trace_fs 
