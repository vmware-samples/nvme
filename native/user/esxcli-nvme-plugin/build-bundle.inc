
VIB_TESTCERTS := $(VMW_VIB_BASE)/testcerts

CERTIFIED_CERTPATH = $(VIB_TESTCERTS)/vmware.cert
CERTIFIED_KEYPATH = $(VIB_TESTCERTS)/vmware.key
CERTIFIED_DEV_SIGNING_PREFIX = $(VIB_TESTCERTS)/vmware

ACCEPTED_CERTPATH = $(VIB_TESTCERTS)/accepted.cert
ACCEPTED_KEYPATH = $(VIB_TESTCERTS)/accepted.key
ACCEPTED_DEV_SIGNING_PREFIX = $(VIB_TESTCERTS)/accepted

PARTNER_CERTPATH = $(VIB_TESTCERTS)/partner.cert
PARTNER_KEYPATH = $(VIB_TESTCERTS)/partner.key
PARTNER_DEV_SIGNING_PREFIX = $(VIB_TESTCERTS)/partner

RM_VIBID_SED = $(ESXCLI_BASE)/vide/tools/rm_vibid.sed

vib-bundle-level:
	$(SED) -i -f $(RM_VIBID_SED) $(VIB_BULLETIN_XML)
	@ac=`grep "<acceptance-level>.*</acceptance-level>" $(VIB_DESCRIPTOR_XML) | $(SED) -e "s@.*<acceptance-level>\(.*\)</acceptance-level>.*@\1@"`; \
        if [ "$${ac}" == "community" ] ; \
        then \
           $(MAKE) DEV_SIGNING_PREFIX= CERTPATH= KEYPATH= -f Makefile vib-bundle ; \
        elif [ "$${ac}" == "certified" ] ; \
        then \
           $(MAKE) DEV_SIGNING_PREFIX='$(CERTIFIED_DEV_SIGNING_PREFIX)' -f Makefile vib-bundle ; \
        elif [ "$${ac}" == "accepted" ] ; \
        then \
           $(MAKE) DEV_SIGNING_PREFIX='$(ACCEPTED_DEV_SIGNING_PREFIX)' -f Makefile vib-bundle ; \
        elif [ "$${ac}" == "partner" ] ; \
        then \
           $(MAKE) DEV_SIGNING_PREFIX='$(PARTNER_DEV_SIGNING_PREFIX)' -f Makefile vib-bundle ; \
        else \
           echo 'Error: Bad acceptance level is specified in the descriptor file [$(VIB_DESCRIPTOR_XML)]' 1>&2 ; \
           exit 1; \
        fi;

