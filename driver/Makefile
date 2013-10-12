# Comment/uncomment the following line to disable/enable debugging
DEBUG = y

# Add your debugging flag (or not) to ccflags-y
ifeq ($(DEBUG),y)
  DEBFLAGS = -O -g -DTSSD_DEBUG # "-O" is needed to expand inlines
else
  DEBFLAGS = -O2
endif

ccflags-y += $(DEBFLAGS)
# ccflags-y += -I..

ifneq ($(KERNELRELEASE),)
# call from kernel build system

obj-m	:= tssd.o

else

KERNELDIR ?= ../kernel-patch/linux-3.2.0 
PWD       := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif

module="tssd"
device=tssd
mode=666
group=staff

#major="$(shell cat /proc/devices | awk '$$2==\"tssd\" {print $$1}')"; 
load:
	# Group: since distributions do it differently, look for wheel or use staff
	# invoke insmod with all arguments we got
	# and use a pathname, as newer modutils don't look in . by default
	sudo insmod ${module}.ko || exit 1
	# Remove stale nodes and replace them, then give gid and perms
	sudo rm -f /dev/${device}a /dev/${device}
	sudo mknod /dev/${device}a b `cat /proc/devices | awk '$$2=="tssd" {print $$1}'` 0 
	sudo chgrp ${group} /dev/${device}a*
	sudo chmod ${mode} /dev/${device}a*

unload:
	# invoke rmmod with all arguments we got
	sudo rmmod ${module} $* || exit 1
	# Remove stale nodes
	sudo rm -f /dev/${device}a /dev/${device}

mount:
	sudo gdisk /dev/${device}a
	sudo mkfs -t ext4 /dev/${device}a
	sudo mkdir -p /mnt/${device}a
	sudo mount /dev/${device}a /mnt/${device}a

umount:
	sudo umount /dev/${device}a

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c *.mod.o Module.symvers modules.order .tmp_versions

depend .depend dep:
	$(CC) $(ccflags-y) -M *.c > .depend

ifeq (.depend,$(wildcard .depend))
include .depend
endif
