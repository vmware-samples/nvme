# Makefile
#
# This builds the Userworld Tool binary for the nvmecli being packaged here.
#
# type:
#	make
# to build the Userworld Tool.
#
# Expected usage is that this Makefile is copied to a new source directory,
# and the variables above the #### line are adjusted appropriately.
#

UWTOOL = nvmecli
UWTOOL_VERSION = 1.2.0.5-1vmw

UWTOOL_SOURCES = nvme_cli.c nvme_lib.c nvme_mgmt_common.c
UWTOOL_INCLUDES = -I../../../common/kernel -I../../kernel
DEV_COMMON_DIR = ../../kernel

DESC_VENDOR_EMAIL=nvmexpress@vmware.com
DESC_VERSION=$(UWTOOL_VERSION)
DESC_VENDOR=VMware
DESC_SUMMARY=Command line interface for VMware NVM Express driver
DESC_DESCRIPTION=Command line interface for VMware NVM Express driver
DESC_RELEASE_DATE=$(shell date -u +"%Y-%m-%dT%H:%M:%S-00:00")
DESC_URLS=descriptor-urls.xml
DESC_DEPENDS=descriptor-depends.xml
DESC_CONFLICTS=
DESC_REPLACES=
DESC_PROVIDES=
DESC_MAINT=1
DESC_LIVE_INSTALL_ALLOWED=true
DESC_LIVE_REMOVE_ALLOWED=true
DESC_STATELESS_READY=true

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

include ../Makefile.inc

