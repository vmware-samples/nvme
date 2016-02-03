#############################################################################
# Copyright (c) 2014-2015 VMware, Inc. All rights reserved
#############################################################################

##
# Makefile for nvme-test
#

##
# Common parameters

TC_ROOT = /build/toolchain
CC = $(TC_ROOT)/lin32/gcc-4.6.3-2/bin/i686-linux5.0-gcc
MAKE = $(TC_ROOT)/lin32/make-3.81/bin/make


##
# Calculate VMKAPI_BASE, VMKAPI_MGMTLIB_BASE, and VMKAPI_MGMTLIB_LIBS
#
HAS_VMKAPIDDK := $(shell rpm -qa | grep vmware-esx-vmkapiddk-devtools)

ifdef HAS_VMKAPIDDK
VMKAPIDDK_ROOT = $(shell rpm -q --qf '%{INSTPREFIXES}\n' vmware-esx-vmkapiddk-devtools)
VMKAPI_BASE = $(dir $(shell find $(VMKAPIDDK_ROOT)/src/bora -name vmkapi.h))
VMKAPI_MGMTLIB_BASE = -L$(VMKAPIDDK_ROOT)/lib

HAS_VMKAPIDDK_6 := $(shell rpm -qa | grep vmware-esx-vmkapiddk-devtools-6)
ifdef HAS_VMKAPIDDK_6
VMKAPI_MGMTLIB_LIBS = -lvmkmgmt --sysroot=/build/toolchain/lin32/glibc-2.11.1-0.17.4 
else
VMKAPI_MGMTLIB_LIBS = -lvmkmgmt -lvmlibs -lvmksysinfoNoVob --sysroot=/build/toolchain/lin32/glibc-2.11.1-0.17.4 
endif

else
PWD = $(shell pwd)
BORA = $(abspath $(PWD)/../../../../../../../../bora)
VMKAPI_BASE=$(dir $(shell find $(BORA)/build/build/HEADERS/vmkapi-current-all-public-bincomp/ -name vmkapi.h | head -1))

VMKAPI_MGMTLIB_BASE += -L$(dir $(shell find $(BORA)/build/build/LIBRARIES/vmkmgmtlib/uw32 -name libvmkmgmt.so | grep -v stripped | head -1))
VMKAPI_MGMTLIB_LIBS += -lvmkmgmt

VMKAPI_MGMTLIB_BASE += -L$(dir $(shell find $(BORA)/build/build/LIBRARIES/vmlibs/uw32 -name libvmlibs.so | grep -v stripped | head -1))
VMKAPI_MGMTLIB_LIBS += -lvmlibs

VMKAPI_MGMTLIB_LIBS += --sysroot=/build/toolchain/lin32/glibc-2.11.1-0.17.4
endif

##
# Product include and libs
#
INCLUDE += -I../.. -I../../../common/kernel -I../../kernel -I../nvme-cli
INCLUDE += -Ilibsgutils2/include
LIBS += -Llibsgutils2/lib -lsgutils2

##
# Include boost/regex
#
INCLUDE += -I$(TC_ROOT)/lin32/boost-1.42.0/include
LIBS += -L$(TC_ROOT)/lin32/boost-1.42.0/lib
LIBS += -Wl,-Bstatic -lboost_regex-gcc41-mt-1_42 -Wl,-Bdynamic

##
# Include gtest
INCLUDE += -I$(TC_ROOT)/lin32/gtest-1.6.0/include
LIBS += -L$(TC_ROOT)/lin32/gtest-1.6.0/lib
LIBS += -Wl,-Bstatic -lgtest -Wl,-Bdynamic

##
# Include VMKAPI
#
INCLUDE += -I$(VMKAPI_BASE)
CCFLAGS += -DVMK_DEVKIT_HAS_API_VMKAPI_BASE -DVMK_DEVKIT_USES_PUBLIC_APIS -DVMK_DEVKIT_USES_BINARY_COMPATIBLE_APIS

##
# Include VMKAPI Management Library
LIBS += $(VMKAPI_MGMTLIB_BASE) $(VMKAPI_MGMTLIB_LIBS)

##
# Other misc libraries
#
LIBS += -lstdc++ -lpthread

##
# VPATH includes
#
VPATH += :$(abspath ../nvme-cli)
VPATH += :$(abspath ../../kernel)

##
# Dependencies
#
C_OBJECTS = nvme_lib.o nvme_mgmt_common.o
CXX_OBJECTS = main.o utils.o nvme_hba.o nvme_test_session.o basic_test.o scsi_test.o

##
# Targets
#
nvme-test: $(C_OBJECTS) $(CXX_OBJECTS) libsgutils2
	$(CC) $(C_OBJECTS) $(CXX_OBJECTS) $(LIBS) -o nvme-test 

libsgutils2: libstutils2/lib/libsgutils2.a

libstutils2/lib/libsgutils2.a:
	$(MAKE) -C libsgutils2/lib

$(C_OBJECTS):%.o: %.c
	$(CC) $(INCLUDE) $(CCFLAGS) -c $< -o $@

$(CXX_OBJECTS):%.o: %.cpp
	$(CC) $(INCLUDE) $(CCFLAGS) -c $< -o $@

clean:
	-rm *.o nvme-test
	$(MAKE) -C libsgutils2/lib clean

