/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (c) 2012-2013  Integrated Device Technology, Inc.
 * All rights reserved.
 *******************************************************************************/

#include "oslib.h"

/**
 * Define and declare a module parameter.
 *
 * Note: a module parameter defined by this macro should also be added to the
 *       Nvme_ValidateModuleParams() function, as well as the `nvme_private.h`
 *       if this parameter will be referenced by other source code modules.
 *
 * @param  name       Name of the module parameter
 * @param  defaultVal Default value
 * @param  minVal     Minimum value
 * @param  maxVal     Maximum value
 * @param  desc       Description of this module parameter
 */
#define NVME_MOD_PARAM(name, defaultVal, minVal, maxVal, desc) \
int name = defaultVal;                                         \
VMK_MODPARAM(name, int, desc);                                 \
static inline void validate_##name() {                         \
   if (name < minVal || name > maxVal) {                       \
      name = defaultVal;                                       \
   }                                                           \
   Nvme_LogNoHandle("%s set to %d.", #name, name);             \
}

NVME_MOD_PARAM(nvme_log_level,
   NVME_LOG_LEVEL_WARNING,
   NVME_LOG_LEVEL_ERROR,
   NVME_LOG_LEVEL_DEBUG,
   "Log level.\n"
   "\t1 - error\n"
   "\t2 - warning\n"
   "\t3 - info (default)\n"
   "\t4 - verbose\n"
   "\t5 - debug")


NVME_MOD_PARAM(admin_sub_queue_size,
   256,
   16,
   256,
   "NVMe number of Admin submission queue entries.")

NVME_MOD_PARAM(admin_cpl_queue_size,
   256,
   16,
   256,
   "NVMe number of Admin completion queue entries")

NVME_MOD_PARAM(io_sub_queue_size,
   1024,
   32,
   1024,
   "NVMe number of IO submission queue entries")

NVME_MOD_PARAM(io_cpl_queue_size,
   1024,
   32,
   1024,
   "NVMe number of IO completion queue entries")

NVME_MOD_PARAM(max_namespaces,
   1024,
   1,
   1024,
   "Maximum number of namespaces supported")

NVME_MOD_PARAM(max_scsi_unmap_requests,
   32,
   8,
   32,
   "Maximum number of scsi unmap requests supported")

#if NVME_DEBUG
NVME_MOD_PARAM(nvme_dbg,
   0,
   VMK_INT32_MIN,
   VMK_INT32_MAX,
   "Driver NVME_DEBUG print level")
#endif

/**
 * Hide these parameters in this version.
 */
int nvme_force_intx = 0;
int max_prp_list = 512;
/** @todo - optimize */
int max_io_request = 1023;
int io_command_id_size = 1024;
int transfer_size = 2048;

/**
 * Validate if a certain module parameter is set within an acceptible range.
 * If a module paramter is not set correctly, use the default value instead.
 */
void Nvme_ValidateModuleParams()
{
   validate_nvme_log_level();
   validate_admin_sub_queue_size();
   validate_admin_cpl_queue_size();
   validate_io_sub_queue_size();
   validate_io_cpl_queue_size();
   validate_max_namespaces();
   validate_max_scsi_unmap_requests();
#if NVME_DEBUG
   validate_nvme_dbg();
#endif
}
