# Makefile
#
# This builds the Userworld Tool binary for the example being packaged here.
#
# type:
#	make
# to build the Userworld Tool.
#
# Expected usage is that this Makefile is copied to a new source directory,
# and the variables above the #### line are adjusted appropriately.
#

UWTOOL = esxcli-nvme-plugin
UWTOOL_VERSION = 1.2.0.37-1vmw

# mgmtInterface.c is in DEV_COMMON_DIR, the build system looks
# there for sources as well as here.
UWTOOL_SOURCES = main.c esxcli_xml.c nvme_lib.c nvme_mgmt_common.c
UWTOOL_INCLUDES = -I../../../common/kernel -I../../kernel
DEV_COMMON_DIR = ../../kernel

PLUGIN_XML = esxcli-nvme.xml

DESC_VENDOR_EMAIL=nvmexpress@vmware.com
DESC_VERSION=$(UWTOOL_VERSION)
DESC_VENDOR=VMware
DESC_SUMMARY=esxcli plugin form VMware nvme driver
DESC_DESCRIPTION=esxcli plugin for VMware nvme driver
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

include ../Makefile.inc

