FTL	= tssd 
PREFIX 	= arm-none-eabi-
CC 	= $(PREFIX)gcc
AS 	= $(PREFIX)as
LD 	= $(PREFIX)ld
OBJCOPY	= $(PREFIX)objcopy
RM	= rm

# Specify at most ONE unit test to run
#
# If any unit test is specifed, the firmware will be compiled to run unit test 
# withour any SATA communication; otherwise, a normal firmware that can handle 
# SATA request will be generated.
#TEST = ftl_seq_rw
#TEST = ftl_rnd_rw
#TEST = ftl_sparse_rw
#TEST = ftl_long_rw
#TEST = ftl
#TEST = gc
#TEST = gtd
#TEST = pmt
#TEST = page_cache
#TEST = perf
#TEST = sot
#TEST = write_buffer
#TEST = task_engine
#TEST = ftl_read_task
#TEST = ftl_write_task
#TEST = fde
#TEST = flash_mock
#TEST = pmt

INCLUDES = -I../include -I../ftl_$(FTL) -I../sata -I../target_spw -I../test_tssd
# Try -Os, --strip-all, -ffunction-sections, -fdata-sections to optimize size
CFLAGS 	= -mcpu=arm7tdmi-s -mthumb-interwork -ffreestanding -nostdlib -std=c99 -O2 -g -DPROGRAM_MAIN_FW -Wall
ASFLAGS	= -R -mcpu=arm7tdmi-s
# Try --gc-sections to optimize size
LDFLAGS	= -static -nostartfiles -ffreestanding -T ld_script -Wl,-O1,-Map=list.txt
LIBS	= -lgcc
VPATH	= ../ftl_$(FTL):../sata:../target_spw

FTL_SRCS = $(shell find ../ftl_$(FTL) -name '*.c' -printf '%f ')
SRCS 	= sata_identify.c sata_cmd.c sata_isr.c sata_main.c sata_table.c initialize.c mem_util.c flash.c flash_wrapper.c misc.c uart.c syscalls.c ${FTL_SRCS}
INITSRC	= ../target_spw/init_gnu.s
OBJS	= $(SRCS:.c=.o) init.o
DEPS	= $(SRCS:.c=.d)
TARGET 	= firmware
TARGETELF = $(TARGET).elf
TARGETBIN = $(TARGET).bin

ifdef TEST
CFLAGS += -D OPTION_FTL_TEST
VPATH  += :../test_tssd
SRCS   += test_${TEST}.c test_util.c
endif

PROFILING =
ifdef PROFILING
CFLAGS += -D OPTION_PROFILING
SRCS   += profiler.c
endif

$(TARGETBIN): $(TARGETELF)
	$(OBJCOPY) -O binary --strip-all --strip-debug $< $@

$(TARGETELF): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

%.d: %.c
	$(CC) -M $(CFLAGS) -c $(INCLUDES) $< > $@

.c.o:
	$(CC) $(CFLAGS) -c $(INCLUDES) $< -o $@

init.o: $(INITSRC)
	$(AS) $(ASFLAGS) $< -o $@

clean:
	@$(RM) $(OBJS) $(DEPS) $(TARGETELF) $(TARGETBIN)

include $(DEPS)
