#-------------------------------------------------------------------------------
# Makefile
#
#    Provides a wrapper to compile drivers in the vmkapi DDK devkit.
#
#    This is a generic Makefile and no changes are required to compile
#    drivers provided that a single .sc file exists in the current directory.
#-------------------------------------------------------------------------------

#-------------------------------------------------------------------------------
# Tools
#-------------------------------------------------------------------------------
CP           = /build/toolchain/lin32/coreutils-6.9/bin/cp
MKDIR        = /build/toolchain/lin32/coreutils-6.9/bin/mkdir
MV           = /build/toolchain/lin32/coreutils-6.9/bin/mv
RM           = /build/toolchain/lin32/coreutils-6.9/bin/rm

AWK          = /build/toolchain/lin32/gawk-3.1.5/bin/awk
GREP         = /build/toolchain/lin32/grep-2.5.1a/bin/grep
MAKE         = /build/toolchain/lin32/make-3.81/bin/make
SED          = /build/toolchain/lin32/sed-4.1.5/bin/sed

#-------------------------------------------------------------------------------
# Parameters to the scons command
#-------------------------------------------------------------------------------
PRODUCT      = vmkapiddk
VMKAPIDDK    = $(shell rpm  -q vmware-esx-vmkapiddk-devtools --qf %{INSTPREFIXES})
SRC          = $(VMKAPIDDK)/src/
FIND_RPM     = $(VMKAPIDDK)/tools/find_rpm_install_dir.sh
VIBTOOLS     = $(shell $(FIND_RPM) vmware-esx-vib-suite)

SCONS        = cd $(SRC); $(SRC)/scons/bin/scons
FULL_PWD     = $(shell pwd)
REL_PWD      = $(shell pwd | $(SED) -e 's,$(SRC),,')
SC           = $(shell echo *.sc | $(AWK) '{print $$1}')
MANIFEST     = $(REL_PWD)/$(SC)
BUILDTYPE    = beta
BUILD_NUMBER = 00000
BUILD        = build
SCONS_BUILD  = .build
TARGET       = $(FULL_PWD)/$(BUILD)
SCONS_TARGET = $(FULL_PWD)/$(SCONS_BUILD)

all vib:
	@$(SCONS) native-driver-modules native-driver-vibs native-driver-bundle \
	       RELEASE_PACKAGES_DIR=${TARGET} \
	       TOOLKIT_MANIFEST=${MANIFEST} \
	       PRODUCT=$(PRODUCT) \
	       BUILDTYPE=$(BUILDTYPE) \
	       BUILD_NUMBER=$(BUILD_NUMBER) \
	       ESX_SIGN_BINARIES=0 \
	       SKIP_BIG_BROTHER=1 \
	       VIBPATH=$(VIBTOOLS) \
	       BUILDROOT=$(SCONS_TARGET)

clean:
	@echo Cleaning out build directories ...
	@$(RM) -rf $(TARGET) $(SCONS_TARGET)
	@echo Done.
