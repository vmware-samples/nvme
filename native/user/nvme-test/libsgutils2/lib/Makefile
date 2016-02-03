TC_ROOT=/build/toolchain
CC=$(TC_ROOT)/lin32/gcc-4.6.3-2/bin/i686-linux5.0-gcc

objects = sg_cmds_basic.o sg_cmds_basic2.o sg_cmds_extra.o sg_cmds_mmc.o sg_io_linux.o sg_lib.o sg_lib_data.o sg_pt_common.o sg_pt_linux.o

all: libsgutils2

libsgutils2: $(objects)
	ar cru libsgutils2.a $(objects)

$(objects):%.o: %.c
	$(CC) -I../include -Wall -W -g -O2 -c $< -o $@

.PHONY: clean
clean:
	-rm *.o libsgutils2.a