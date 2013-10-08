# _GNU_SOURCE macro must be defined to enable O_DIRECT flag in open()
CFLAGS=-Wall -g -D_GNU_SOURCE

all: read write

read: tssd.o

write: tssd.o

clean:
	rm *.o read write
