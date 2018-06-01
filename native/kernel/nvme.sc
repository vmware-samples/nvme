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
nvme_identification = {
   "name"            : "nvme",
   "module type"     : "device driver",
   "binary compat"   : "yes",
   "summary"         : "Non-Volatile memory controller driver",
   "description"     : "Non-Volatile memory controller driver",
   "version"         : "1.2.1.34",
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
   "identification"  : nvme_identification,
   "source files"    : [ "nvme_module.c",
                         "nvme_driver.c",
                         "nvme_mgmt_kernel.c",
                         "../../common/kernel/nvme_ctrlr.c",
                         "../../common/kernel/nvme_param.c",
                         "../../common/kernel/nvme_ctrlr_mgmt.c",
                         "../../common/kernel/nvme_queue.c",
                         "../../common/kernel/nvme_scsi.c",
                         "../../common/kernel/nvme_debug.c",
                         "../../common/kernel/nvme_io.c",
                         "../../common/kernel/nvme_state.c",
                         "nvme_mgmt_common.c",
                         "../../common/kernel/nvme_core.c",
                         "../../common/kernel/nvme_exc.c",
                         "oslib.c",
                       ],
   "includes"        : [
                         "../../common/",
                       ],
   "cc defs"          : [
                         "VMK_DEVKIT_HAS_API_VMKAPI_MPP",
                         "VMKAPIDDK_VERSION=650",
                        ],
   "cc warnings"     : []
}
nvme_module = defineKernelModule(module_def)

#
# Build the Driver's Device Definition
#
device_def = {
   "identification"  : nvme_identification,
   "device spec"     : "nvme_devices.py",
}
nvme_device_def = defineDeviceSpec(device_def)

#
# Build the VIB
#
nvme_vib_def = {
   "identification"  : nvme_identification,
   "payload"         : [ nvme_module,
                         nvme_device_def,
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
nvme_vib =  defineModuleVib(nvme_vib_def)

#
# Build the Offline Bundle
#
nvme_bulletin_def = {
   "identification" : nvme_identification,
   "vib" : nvme_vib,

   "bulletin" : {
      # These elements show the default values for the corresponding items in bulletin.xml file
      # Uncomment a line if you need to use a different value
      #'severity'    : 'general',
      #'category'    : 'Enhancement',
      #'releaseType' : 'extension',
      #'urgency'     : 'Important',

      'kbUrl'       : 'http://kb.vmware.com/kb/example.html',

      # 1. At least one target platform needs to be specified with 'productLineID'
      # 2. The product version number may be specified explicitly, like 7.8.9,
      # or, when it's None or skipped, be a default one for the devkit
      # 3. 'locale' element is optional
      'platforms'   : [
                        {'productLineID':'ESXi'},
                      ],
   },
}
nvme_bundle =  defineOfflineBundle(nvme_bulletin_def)
