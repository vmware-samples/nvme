#############################################################################
# Copyright (c) 2014-2015 VMware, Inc. All rights reserved
#############################################################################

##
# Makefile.inc
#
#    Common Makefile for NVMe user world tools
#

DESC_ACCEPTANCE_LEVEL=certified

# When this is not empty, the infrastructure will look for the files
# $(DEV_SIGNING_PREFIX).key and $(DEV_SIGNING_PREFIX).cert.  If the
# prefix contains a slash, it is assumed to be a full path to the
# directory containing the key/cert files, and that simply appending
# ".key" and ".cert" will give the actual file names.  Otherwise, it
# looks in the vib-suite installation directory's "testcerts"
# directory for the files.
DEV_SIGNING_PREFIX=vmware

BULL_KBURL=http://kb.vmware.com/kb/example.html
BULL_PLATFORMS=bulletin-platforms.url
BULL_VENDOR_CODE=VMW

# Calculate VMKAPIDDK_ROOT, BUILD_NUMBER

VMKAPIDDK_ROOT = $(shell rpm -q --qf '%{INSTPREFIXES}\n' vmware-esx-vmkapiddk-devtools)
BUILD_NUMBER=$(shell echo $(VMKAPIDDK_ROOT) | cut -d '-' -f3)

# include $(VMKAPIDDK_ROOT)/src/partners/samples/common-mgmt-infrastructure/Makefile.user

# Makefile
#
# This builds the Userworld Tool binary for the example being packaged here.
#
# type:
#  make
# to build the Userworld Tool.
#
# Expected usage is that this Makefile is copied to a new source directory,
# and the variables above the "Nothing beyond this point" comment are
# adjusted appropriately.
#

# This is a common Makefile for all of the examples.  "UWTOOL",
# "UWTOOL_SOURCES", "UWTOOL_VERSION", and "COMMON_DIR"  must all
# be defined by the individual makefiles for each sample.


DEV_VIB_BASENAME = vmware-esx-$(UWTOOL)

DESC_NAME=$(DEV_VIB_BASENAME)

HAS_VMKAPIDDK_6 := $(shell rpm -qa | grep vmware-esx-vmkapiddk-devtools-6)
#ifdef HAS_VMKAPIDDK_6
DESC_ESX_VERSION=6.0.0
#else
DESC_ESX_VERSION=5.5.0
#endif

################################################################
# Set build configuration options here.
#
# Variables starting with "DEV_" are reserved for communicating
# build parameters into the core makefiles (in .../samples/common).
#
# Those starting with "VMW_" are reserved for VMware use, and should
# be considered unsupported with no compatibility guarantees unless
# explicitly documented otherwise.
#
# Note that many variables do not currently follow the above
# patterns.  This is to minimize changes required in existing
# partner code.

################
# First, define the end environment(s) with the following two
# variables.  As shown here, one is defined.  If more than one is
# necessary, these values will need to be set on a per-target basis,
# or in a loop that recursively invokes make with each set of values
# provided on the command line.
#
# This parameter must be set.

# Legal values for DEV_TARGET_CONTEXT are "vmkernel" or "uw".
DEV_TARGET_CONTEXT = uw

################
# This controls which class of compiler options to use.  Allowed
# values are "beta" (for debugging) and "release".  The default is
# the type of the ESX build that produced this devkit.
DEV_OBJDIR = beta

################
# Typically, it won't be necessary to modify this setting, but the
# substitutions pattern may need to put the intermediate files in a
# more convenient location.
DEV_$(DEV_OBJDIR)_OBJECTS = $(UWTOOL_SOURCES:%.c=$(BINARIES_DIR)/%.o)

################
# If creating a standalone userworld application, as opposed to a
# shared object that gets loaded by some other program, uncomment
# the following assignment.  The value needs to be empty or non-
# empty.  If the latter, any content may be used.
DEV_APP = 1

################
# Extra compiler and preprocessor flags based on kind of build
# (release or beta).  These default to being empty.
# DEV_beta_COMPILEFLAGS = -Dmy_debug_flag
# DEV_release_COMPILEFLAGS = -Dmy_optimized_flag

################
# Define here any additional libraries required by uncommenting the
# appropriate variable(s) and modifying as needed.  If the library is
# not included in the devkit distribution, include a -L /some/path
# parameter before the -lname parameter so that the linker can find
# it.  The *PRE* variables are included as the first library
# directives.  The *POST* variables are included as the last library
# directives.
#
# The default values are empty.

# DEV_release_PRE_LIBS =
# DEV_beta_PRE_LIBS =

# The third assignment below is semantically equivalent to the first
# two assignments.

# DEV_release_POST_LIBS = -lm
# DEV_beta_POST_LIBS = -lm
# or
# DEV_$(DEV_OBJDIR)_POST_LIBS = -lm

################
# If the vdsPublic library needs to be included in the compilation,
# uncomment the following assignment.  Currently, that library only
# comes as part of the "kmdk-devtools-vds" devkit, so it and any
# other devkit components it depends on will need to be installed
# on the same development host as this devkit.  The value must be
# empty for not needing vdsPublic, and non-empty for needing it.
# The actual content is not examined.

# DEV_NEEDVDSLIB = 1

################
# Request that the management library be included when we link.  This
# has to be explicitly done here because we're hooking into the base
# HExDK's build system, and it doesn't do that inclusion by default.

DEV_NEED_VMKMGMT = 1

################################################################
# Nothing beyond this point should require modification.

GREP := /build/toolchain/lin32/grep-2.5.1a/bin/grep

IMPORT_DEVKIT = $(shell rpm -q --qf '%{INSTPREFIXES}\n' $(1) | $(GREP) $(BUILD_NUMBER))

HEXDK_BASE := $(call IMPORT_DEVKIT,vmware-esx-esxcli-devtools)
VMKAPIDDK_BASE := $(call IMPORT_DEVKIT,vmware-esx-vmkapiddk-devtools)

#ifdef HAS_VMKAPIDDK_6
VMKAPI_LOCATION = $(VMKAPIDDK_BASE)/src/bora/vmkernel/include/vmkapi/v2_3_0_0
#else
VMKAPI_LOCATION = $(VMKAPIDDK_BASE)/src/bora/vmkernel/include/vmkapi/v2_2_0_0
#endif

VMKAPI_LOCATION = $(dir $(shell find $(VMKAPIDDK_ROOT)/src/bora -name vmkapi.h))

# Only need to evaluate once (aka use ":=").
CURRENT_DIR := $(shell pwd)
BUILD_DIR := build
SCRATCH_DIR := $(BUILD_DIR)/scratch
VIB_DIR := $(BUILD_DIR)/vib
BUNDLE_DIR := $(BUILD_DIR)/bundle
STAGE_DIR := $(BUILD_DIR)/stage
TARGET_XML_DIR := /usr/lib/vmware/esxcli/ext

VIB_DESCRIPTOR_XML := $(SCRATCH_DIR)/descriptor.xml
VIB_BULLETIN_XML := $(SCRATCH_DIR)/bulletin.xml

# Need to evaluate at every reference, as DEV_OBJDIR, etc, can change
# during a build (aka use "=").  Also, some things get set/reset in
# common included files, we reference them, and we haven't included the files
# yet.
BINARIES_DIR = $(BUILD_DIR)/$(DEV_OBJDIR)
LINKED_UWTOOL = $(BINARIES_DIR)/$(UWTOOL)
INSTALL_DIR = /opt/vmware/nvme
UWTOOL_DIR = $(STAGE_DIR)/$(INSTALL_DIR)
STAGE_XML_DIR = $(STAGE_DIR)$(TARGET_XML_DIR)
VIB_BULLETIN_ID = vmware-esx-$(DESC_ESX_VERSION)-$(UWTOOL)-$(UWTOOL_VERSION)

#
# Check paths
#

ifeq ($(wildcard $(HEXDK_BASE)/samples/common/userworld-tool.inc),)
$(error Host-Extensions devkit build $(BUILD_NUMBER) required but not installed.)
endif

ifeq ($(wildcard $(VMKAPI_LOCATION)/vmkapi.h),)
$(error VMKAPI DDK devkit build $(BUILD_NUMBER) required but not installed.)
endif

# Note that this hard-codes the prefix path at installation
# time.  This allows for having multiple different versions
# of the devkit installed and usable simultaneously.

include $(HEXDK_BASE)/samples/common/userworld-tool.inc

VMW_EXTRA_DEFS += -I$(VMKAPI_LOCATION) -I$(DEV_COMMON_DIR) $(UWTOOL_INCLUDES) \
	-DVMK_DEVKIT_HAS_API_VMKAPI_BASE \
	-DVMK_DEVKIT_USES_PUBLIC_APIS \
	-DVMK_DEVKIT_USES_BINARY_COMPATIBLE_APIS

VPATH += :$(abspath $(DEV_COMMON_DIR))

.PHONY: clean

all: binaries vib
all-workbench: binaries xml-checks package
vib: build-dirs minimal-descriptor fill-bulletin xml-checks package

binary binaries: $(LINKED_UWTOOL)
vib-binary vib-binaries: INCLUDE_XML=no
vib-binary vib-binaries: vib

# This target exists mainly to provide a place to put in special
# staging operations if they are needed.  Make doesn't really like the
# idea of doing some actions and then going off to build a different
# target (essentially, a subroutine), so the easiest solution is to
# recursively invoke ourselves for the actual vib/bundle creation.
package:
	# Start with a fresh staging area, and make it
	# look exactly like what we want on the target host.
	$(RM) -rf $(STAGE_DIR)
	$(MKDIR) -p $(UWTOOL_DIR)
	$(CP) $(LINKED_UWTOOL) $(UWTOOL_DIR)/$(UWTOOL)
	if [ no != "$(INCLUDE_XML)" -a -n "$(PLUGIN_XML)" ]; then \
		$(MKDIR) -p $(STAGE_XML_DIR); \
		$(CP) $(PLUGIN_XML) \
			$(STAGE_XML_DIR)/$(PLUGIN_XML); \
	fi
	# Now put it all together for the ESX installer.
	$(MAKE) -f Makefile vib-bundle


# Note that the resulting UWTOOL is not stripped.
$(LINKED_UWTOOL): build-dirs $(DEV_$(DEV_OBJDIR)_OBJECTS)
	$(LINK_CMD)

$(BINARIES_DIR)/%.o: %.c
	$(CC_CMD)

build-dirs:
	$(MKDIR) -p $(BUILD_DIR) $(SCRATCH_DIR) $(STAGE_DIR) $(VIB_DIR) \
		$(BINARIES_DIR)

clean:
	$(RM) -rf $(BUILD_DIR) $(SCRATCH_DIR) $(STAGE_DIR) $(VIB_DIR) core \
		$(BINARIES_DIR)
