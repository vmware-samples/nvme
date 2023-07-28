"""
* *******************************************************************************
* Copyright (c) 2016-2023 VMware, Inc. All rights reserved.
* *******************************************************************************
"""
# Driver definition for nvme driver.
#
# When developing a driver for release through the async program:
#  * set "vendor" to the name of your company
#  * set "vendor email" to the contact e-mail provided by your company
#  * increment the version number if the source has come from VMware
#  * remove "version_bump" if present
#
# When bringing an async driver inbox at VMware:
#  * leave "version" as is from the async release
#  * set "version_bump" to 1
#  * set "vendor" to 'Vmware, Inc.'
#  * set "vendorEmail" to the VMware contact e-mail address
#
# If updating the driver at VMware:
#  * increment "version bump" or contact the IHV for a new version number
#
# If updating the driver at an async vendor:
#  * increment the version number (do not touch version bump)

#
# identification section
#
nvme_pcie_identification = {
   "name"            : "nvme_pcie",
   "module type"     : "device driver",
   "binary compat"   : "yes",
   "summary"         : "Non-Volatile memory controller driver",
   "description"     : "Non-Volatile memory controller driver",
   "version"         : "1.2.4.13",
   "version_bump"    : 1,
   "license"         : VMK_MODULE_LICENSE_BSD,
   "vendor"          : "VMware",
   "vendor_code"     : "VMW",
   "vendor_email"    : "support@vmware.com",
}

#
# Build the Driver Module
#
module_def = {
   "identification"  : nvme_pcie_identification,
   "source files"    : [
      "nvme_pcie.c",
      "nvme_pcie_adapter.c",
      "nvme_pcie_debug.c",
      "nvme_pcie_driver.c",
      "nvme_pcie_mgmt.c",
      "nvme_pcie_module.c",
      "nvme_pcie_os.c",
                       ],
   "includes"        : [
                       ],
   "cc defs"         : [
                       ],
   "cc warnings"     : [
                       ]
}
nvme_pcie_module = defineKernelModule(module_def)

#
# Build the Driver's Device Definition
#
device_def = {
   "identification"  : nvme_pcie_identification,
   "device spec"     : "nvme_pcie_devices.py",
}
nvme_pcie_device_def = defineDeviceSpec(device_def)

#
# Build the VIB
#
nvme_pcie_vib_def = {
   "identification"  : nvme_pcie_identification,
   "payload"         : [ nvme_pcie_module,
                         nvme_pcie_device_def,
                       ],
   "vib properties"  : {
      "urls":[
             ],
      "provides":
             [
             ],
      "depends":
             [
             ],
      "conflicts":
             [
             ],

      "maintenance-mode":  True,

       "hwplatform":
             [
             ],

      # Can use strings to define Boolean values  - see below
      "acceptance-level": 'certified',
      "live-install-allowed": False,
      "live-remove-allowed": 'false',
      "cimom-restart": False,
      "stateless-ready": 'True',
      "overlay": False,
   }
}
nvme_pcie_vib =  defineModuleVib(nvme_pcie_vib_def)

#
# Build the Component
#
from devkitUtilities import GenerateFullVibVersionNumber

fullVersion = GenerateFullVibVersionNumber(nvme_pcie_identification['version'],
                                           nvme_pcie_identification['vendor_code'],
                                           nvme_pcie_identification['binary compat'],
                                           nvme_pcie_identification['version_bump'])

shortVersion = '%s-%s' % (nvme_pcie_identification['version'],
                          nvme_pcie_identification['version_bump'])

nvme_pcie_bulletin_def = {
   "identification" : nvme_pcie_identification,
   "vib" : nvme_pcie_vib,

   "bulletin" : {
      # These elements show the default values for the corresponding items in
      # bulletin.xml file
      # Uncomment a line if you need to use a different value
      #'severity'    : 'general',
      #'category'    : 'Enhancement',
      #'releaseType' : 'extension',
      #'urgency'     : 'important',

      # If a Knowledge Base (KB) article is needed for this component set it below.
      #'kbUrl'       : 'http://kb.vmware.com/kb/example.html',

      'componentNameSpec' : { 'name' : 'VMware-NVMe-PCIe',
                              'uistring' : 'VMware NVMe PCI Express '
                                           'Storage Driver'
                            },

      'componentVersionSpec' : { 'version' : fullVersion,
                                 'uistring' : shortVersion
                               },

      # 1. At least one target platform needs to be specified with 'productLineID'
      # 2. The product version number may be specified explicitly, like 7.8.9,
      # or, when it's None or skipped, be a default one for the devkit
      # 3. 'locale' element is optional
      'platforms'   : [
                        {'productLineID':'ESXi'},
                      ],
   },
}
nvme_pcie_bundle =  defineComponent(nvme_pcie_bulletin_def)
