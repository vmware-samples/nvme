/*********************************************************************************
 * Copyright 2013 VMware, Inc. All rights reserved.
 * ******************************************************************************/

/*-
 * Copyright (C) 2012 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

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

