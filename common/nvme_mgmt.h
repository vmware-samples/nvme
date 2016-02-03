/*********************************************************************************
 * Copyright 2013 - 2014 VMware, Inc. All rights reserved.
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
 * nvme_mgmt.h --
 *
 *    Driver management interface of native nvme driver, shared by kernel and
 *    user space tools.
 */

#include <vmkapi.h>

/**
 * NVM data structures are required
 */
#include "../kernel/nvme.h"

#ifndef _NVME_MGMT_H_
#define _NVME_MGMT_H_

#define NVME_MGMT_NAME "nvmeMgmt"
#define NVME_MGMT_VENDOR "VMware"
#define NVME_MGMT_MAJOR (1)
#define NVME_MGMT_MINOR (0)
#define NVME_MGMT_UPDATE (0)
#define NVME_MGMT_PATCH (0)

#define NVME_MAX_ADAPTERS (64)

typedef enum {
   NVME_SMART_HEALTH_STATUS                 = 0,
   NVME_SMART_MEDIA_WEAROUT_INDICATOR       = 1,
   NVME_SMART_WRITE_ERROR_COUNT             = 2,
   NVME_SMART_READ_ERROR_COUNT              = 3,
   NVME_SMART_POWER_ON_HOURS                = 4,
   NVME_SMART_POWER_CYCLE_COUNT             = 5,
   NVME_SMART_REALLOCATED_SECTOR_CT         = 6,
   NVME_SMART_RAW_READ_ERROR_RATE           = 7,
   NVME_SMART_DRIVE_TEMPERATURE             = 8,
   NVME_SMART_DRIVE_RATED_MAX_TEMPERATURE   = 9,
   NVME_SMART_WRITE_SECTORS_TOT_CT          = 10,
   NVME_SMART_READ_SECTORS_TOT_CT           = 11,
   NVME_SMART_INITIAL_BAD_BLOCK_COUNT       = 12,
   NVME_SMART_MAX_PARAM                     = 13
} nvmeSmartAttribute;

typedef enum {
   NVME_SMART_HEALTH_OK,
   NVME_SMART_HEALTH_WARNING,
   NVME_SMART_HEALTH_IMPENDING_FAILURE,
   NVME_SMART_HEALTH_FAILURE,
   NVME_SMART_HEALTH_UNKNOWN
} nvmeSmartHealthStatus;

typedef struct nvmeSmartParam {
   int value;
   int worst;
   int threshold;
   struct {
      int value       : 1;
      int worst       : 1;
      int threshold   : 1;
   } valid;
} nvmeSmartParam;

typedef struct nvmeSmartParamBundle {
   nvmeSmartParam params[NVME_SMART_MAX_PARAM];
} nvmeSmartParamBundle;

/* full name space for get log page query*/
#define NVME_FULL_NAMESPACE  0xFFFFFFFF


#define XFER_TO_DEV           0
#define XFER_FROM_DEV         1


/**
 * User pass through data structure definition
 * NOTE: The structure is used by userworld and vmkernel. In 32bits userworld, it aligned
 * to 4 bytes, while in vmkernel, it aligned to 8 bytes. The difference will impact the size
 * of the sturcture. To ensure size of structure is the same, we should always align 8 bytes.
 * See PR #1213822
 */
struct usr_io {
   struct nvme_cmd cmd;         /* Submission queue entry. */
   struct cq_entry comp;        /* Completion entry */
   vmk_uint8    namespace;      /* Namespace ID, -1 for non-specific */
   vmk_uint8    direction;      /* direction TO_DEVICE/FROM_DEVICE */
   vmk_uint16   reserved;       /* reserved */
   vmk_uint32   status;         /* Command status */
   vmk_uint32   length;         /* data length */
   vmk_uint32   meta_length;    /* meta data length */
   vmk_uint64   timeoutUs;      /* timeout in microseconds */
   vmk_uint64   addr;           /* data address */
   vmk_uint64   meta_addr;      /* meta data address */
};


/**
 * Event notification request data structure definition.
 */
struct event_req {
   vmk_uint16   event_id;       /* Event Identification */
   vmk_uint16   event_mask;     /* Event Identification mask */
   vmk_uint32   length;         /* Event Page data length */
   vmk_uint64   addr;           /* Event Page data address */
};


/**
 * Ioctl function command definitions
 */
enum {
   NVME_IOCTL_START,             /* Placeholder, no-op */
   NVME_IOCTL_ADMIN_CMD,         /* Pass-through admin command */
   NVME_IOCTL_IO_CMD,            /* Pass-through NVM command */
   NVME_IOCTL_RESTART,           /* Restart controller */
   NVME_IOCTL_HOTREMOVE,         /* Hot remove */
   NVME_IOCTL_HOTADD,            /* Hot add device */
   NVME_IOCTL_EVENT,             /* Acquire asynchronous events */
   NVME_IOCTL_SET_CACHE,         /* Configure cache */
   NVME_IOCTL_DUMP_REGS,         /* Dump NVM registers */
   NVME_IOCTL_SET_CTRLR_ONLINE,  /* Online all namespaces on the controller */
   NVME_IOCTL_SET_CTRLR_OFFLINE, /* Offline all namespaces on the controller */
};


/**
 * Callback IDs
 */
enum {
   NVME_MGMT_CB_START = VMK_MGMT_RESERVED_CALLBACKS,
   NVME_MGMT_CB_SMART,
   NVME_MGMT_CB_IOCTL,
   NVME_MGMT_CB_END,
};
#define NVME_MGMT_CTRLR_NUM_CALLBACKS (NVME_MGMT_CB_END - NVME_MGMT_CB_START - 1)

/**
  * Callback definitions
  */
#ifdef VMKERNEL
   #if VMKAPIDDK_VERSION >= 600
      VMK_ReturnStatus kernelCbSmartGet(vmk_MgmtCookies *cookies, vmk_MgmtEnvelope *envelope,
                                     vmk_uint32* nsID, struct nvmeSmartParamBundle* bundle);
      VMK_ReturnStatus kernelCbIoctl(vmk_MgmtCookies *cookies, vmk_MgmtEnvelope *envelope,
                              vmk_uint32 *cmd, struct usr_io *uio);
   #else
      VMK_ReturnStatus kernelCbSmartGet(vmk_uint64 cookie, vmk_uint64 instanceId,
                                     vmk_uint32* nsID, struct nvmeSmartParamBundle* bundle);
      VMK_ReturnStatus kernelCbIoctl(vmk_uint64 cookie, vmk_uint64 instanceId,
                              vmk_uint32 *cmd, struct usr_io *uio);
   #endif
#else
#define kernelCbSmartGet (NULL)
#define kernelCbIoctl (NULL)
#endif


/**
 * Signature and callback declaration for per-controller management handle
 *
 * The definition is in nvme_mgmt_common.c and is shared by both UW and kernel
 */
extern vmk_MgmtCallbackInfo nvmeCallbacks[NVME_MGMT_CTRLR_NUM_CALLBACKS];

/**
 * Status of adapter
 */
#define OFFLINE (0)
#define ONLINE (1)

/**
 * Struct that passes adapter information between kernel and user world
 */
struct nvmeAdapterInfo {
   /** vmhba name */
   char           name[VMK_MISC_NAME_MAX];
   /** Management signature */
   char           signature[VMK_MISC_NAME_MAX];
   /** Status of adapter */
   vmk_uint64   status;
   /** Cookie, should be the pointer to ctrlr */
   vmk_uint64     cookie;
};


/**
 * Callback IDs of global management handle
 */
enum {
   NVME_MGMT_GLOBAL_CB_START = VMK_MGMT_RESERVED_CALLBACKS,
   NVME_MGMT_GLOBAL_CB_LISTADAPTERS,
   NVME_MGMT_GLOBAL_CB_END,
};
#define NVME_MGMT_GLOBAL_NUM_CALLBACKS (NVME_MGMT_GLOBAL_CB_END - NVME_MGMT_GLOBAL_CB_START - 1)


/**
 * Global callback definitions
 */
#ifdef VMKERNEL
   #if VMKAPIDDK_VERSION >= 600
      VMK_ReturnStatus NvmeMgmt_ListAdapters(vmk_MgmtCookies *cookies,
         vmk_MgmtEnvelope *envelope,
         vmk_uint32 *numAdapters,
         struct nvmeAdapterInfo *adapterInfo);
   #else
      VMK_ReturnStatus NvmeMgmt_ListAdapters(vmk_uint64 cookie,
         vmk_uint64 instanceId,
         vmk_uint32 *numAdapters,
         struct nvmeAdapterInfo *adapterInfo);
   #endif
#else
   #define NvmeMgmt_ListAdapters (NULL)
#endif


/**
 * Signature declaration for global management handle
 *
 * The definition is in nvme_mgmt_common.c and is shared by both UW and kernel
 */
extern vmk_MgmtApiSignature globalSignature;

#endif /* _NVME_MGMT_H_ */
