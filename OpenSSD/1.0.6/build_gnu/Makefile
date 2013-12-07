FTL	= tssd 
PREFIX 	= arm-none-eabi-
CC 	= $(PREFIX)gcc
AS 	= $(PREFIX)as
LD 	= $(PREFIX)ld
OBJCOPY	= $(PREFIX)objcopy
RM	= rm

INCLUDES = -I../include -I../ftl_$(FTL) -I../sata -I../target_spw -I../test_tssd
# Try -Os, --strip-all, -ffunction-sections, -fdata-sections to optimize size
CFLAGS 	= -mcpu=arm7tdmi-s -mthumb-interwork -ffreestanding -nostdlib -std=c99 -O2 -g -DPROGRAM_MAIN_FW -Wall
ASFLAGS	= -R -mcpu=arm7tdmi-s
# Try --gc-sections to optimize size
LDFLAGS	= -static -nostartfiles -ffreestanding -T ld_script -Wl,-O1,-Map=list.txt
LIBS	= -lgcc
VPATH	= ../ftl_$(FTL):../sata:../target_spw:../test_tssd

FTL_SRCS = $(shell find ../ftl_$(FTL) -name '*.c' -printf '%f ')
# Comment TEST_SRC if you don't want to do any unit test
#TEST_SRC = test_ftl.c 
SRCS 	= sata_identify.c sata_cmd.c sata_isr.c sata_main.c sata_table.c initialize.c mem_util.c flash.c flash_wrapper.c misc.c uart.c syscalls.c ${FTL_SRCS} ${TEST_SRC}
INITSRC	= ../target_spw/init_gnu.s
OBJS	= $(SRCS:.c=.o) init.o
DEPS	= $(SRCS:.c=.d)
TARGET 	= firmware
TARGETELF = $(TARGET).elf
TARGETBIN = $(TARGET).bin

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
	$(RM) $(OBJS) $(DEPS) $(TARGETELF) $(TARGETBIN)

include $(DEPS)
