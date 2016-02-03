/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (C) 2012 Intel Corporation
 * All rights reserved.
 *******************************************************************************/

/*
 * nvme_mgmt_common.c --
 *
 *    Driver management interface of native nvme driver, shared by kernel and user
 */
#include "nvme_mgmt.h"

/**
  * Management interface signature definition
  *
  * This is shared between the driver and management clients.
  */
vmk_MgmtCallbackInfo nvmeCallbacks[NVME_MGMT_CTRLR_NUM_CALLBACKS] = {
   {
      .location      = VMK_MGMT_CALLBACK_KERNEL,
      .callback      = kernelCbSmartGet,
      .synchronous   = 1,
      .numParms      = 2,
      .parmSizes     = {sizeof(vmk_uint32),sizeof(struct nvmeSmartParamBundle)},
      .parmTypes     = {VMK_MGMT_PARMTYPE_IN, VMK_MGMT_PARMTYPE_OUT},
      .callbackId    = NVME_MGMT_CB_SMART,
   },
   {
      .location      = VMK_MGMT_CALLBACK_KERNEL,
      .callback      = kernelCbIoctl,
      .synchronous   = 1,
      .numParms      = 2,
      .parmSizes     = {sizeof(vmk_uint32), sizeof(struct usr_io)},
      .parmTypes     = {VMK_MGMT_PARMTYPE_IN, VMK_MGMT_PARMTYPE_INOUT},
      .callbackId    = NVME_MGMT_CB_IOCTL,
   },
#if NVME_DEBUG_INJECT_ERRORS
   {
       .location      = VMK_MGMT_CALLBACK_KERNEL,
       .callback      = kernelCbErrInject,
       .synchronous   = 1,
       .numParms      = 4,
       .parmSizes     = {sizeof(vmk_uint32), sizeof(vmk_uint32), sizeof(vmk_uint32), sizeof(vmk_uint32)},
       .parmTypes     = {VMK_MGMT_PARMTYPE_IN, VMK_MGMT_PARMTYPE_IN, VMK_MGMT_PARMTYPE_IN, VMK_MGMT_PARMTYPE_IN},
       .callbackId    = NVME_MGMT_CB_ERR_INJECT,
   },
#endif
};


/**
 * Global management interface
 */
vmk_MgmtCallbackInfo globalCallbacks[NVME_MGMT_GLOBAL_NUM_CALLBACKS] = {
   {
      .location      = VMK_MGMT_CALLBACK_KERNEL,
      .callback      = NvmeMgmt_ListAdapters,
      .synchronous   = 1,
      .numParms      = 2,
      .parmSizes     = {sizeof(vmk_uint32), sizeof(struct nvmeAdapterInfo) * NVME_MAX_ADAPTERS},
      .parmTypes     = {VMK_MGMT_PARMTYPE_OUT, VMK_MGMT_PARMTYPE_OUT},
      .callbackId    = NVME_MGMT_GLOBAL_CB_LISTADAPTERS,
   },
   {
      .location      = VMK_MGMT_CALLBACK_KERNEL,
      .callback      = NvmeMgmt_SetLogLevel,
      .synchronous   = 1,
      .numParms      = 2,
      .parmSizes     = {sizeof(vmk_uint32), sizeof(vmk_uint32)},
      .parmTypes     = {VMK_MGMT_PARMTYPE_IN, VMK_MGMT_PARMTYPE_IN},
      .callbackId    = NVME_MGMT_GLOBAL_CB_SETLOGLEVEL,
   },
};


/**
 * Global management api signature
 */
vmk_MgmtApiSignature globalSignature = {
   .version         = VMK_REVISION_FROM_NUMBERS(NVME_MGMT_MAJOR, NVME_MGMT_MINOR, NVME_MGMT_UPDATE, NVME_MGMT_PATCH),
   .name.string     = NVME_MGMT_NAME,
   .vendor.string   = NVME_MGMT_VENDOR,
   .numCallbacks    = NVME_MGMT_GLOBAL_NUM_CALLBACKS,
   .callbacks       = globalCallbacks,
};

