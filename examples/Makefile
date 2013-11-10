CFLAGS=-Wall -g

all: read write

MOUNT_POINT=/mnt/tssda

read: tssd.o

write: tssd.o

clean:
	rm *.o read write

# ============================================================================
# 	Test
# ===========================================================================
user0_file="user0.txt"
user1_file="user1.txt"
user0_key=0
user1_key=1
.PHONY: test
test: all
	sudo cp read write test.py ${MOUNT_POINT}/
	cd $(MOUNT_POINT); \
		sudo ./write ${user0_file} "Secrets of user0" ${user0_key}; \
		sudo ./write ${user1_file} "Secrets of user1" ${user1_key}; \
		sudo ./test.py ${user0_file} ${user0_key} ${user1_file} ${user1_key};





