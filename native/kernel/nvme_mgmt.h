/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (C) 2012 Intel Corporation
 * All rights reserved.
 *******************************************************************************/

/*
 * nvme_mgmt.h --
 *
 *    Driver management interface of native nvme driver, shared by kernel and
 *    user space tools.
 */
#ifndef _NVME_MGMT_H_
#define _NVME_MGMT_H_

#include <vmkapi.h>

/**
 * NVM data structures are required
 */
#include "../../common/kernel/nvme.h"
#include "../../common/kernel/nvme_debug.h"

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
 * Callback IDs
 */
enum {
   NVME_MGMT_CB_START = VMK_MGMT_RESERVED_CALLBACKS,
   NVME_MGMT_CB_SMART,
   NVME_MGMT_CB_IOCTL,
#if NVME_DEBUG_INJECT_ERRORS
   NVME_MGMT_CB_ERR_INJECT,
#endif
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
      VMK_ReturnStatus kernelCbErrInject(vmk_MgmtCookies *cookies, vmk_MgmtEnvelope *envelope,
                                         vmk_uint32 *globalFlag, vmk_uint32 *errType,
                                         vmk_uint32 *likelyhood, vmk_uint32 *flag);
   #else
      VMK_ReturnStatus kernelCbSmartGet(vmk_uint64 cookie, vmk_uint64 instanceId,
                                     vmk_uint32* nsID, struct nvmeSmartParamBundle* bundle);
      VMK_ReturnStatus kernelCbIoctl(vmk_uint64 cookie, vmk_uint64 instanceId,
                              vmk_uint32 *cmd, struct usr_io *uio);
      VMK_ReturnStatus kernelCbErrInject(vmk_uint64 cookie, vmk_uint64 instanceId,
                                         vmk_uint32 *globalFlag, vmk_uint32 *errType,
                                         vmk_uint32 *likelyhood, vmk_uint32 *enableFlag);
   #endif
#else
#define kernelCbSmartGet (NULL)
#define kernelCbIoctl (NULL)
#define kernelCbErrInject (NULL)
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
   NVME_MGMT_GLOBAL_CB_SETLOGLEVEL,
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
 * Global callback definitions
 */
#ifdef VMKERNEL
   #if VMKAPIDDK_VERSION >= 600
      VMK_ReturnStatus NvmeMgmt_SetLogLevel(vmk_MgmtCookies *cookies,
         vmk_MgmtEnvelope *envelope,
         vmk_uint32 *loglevel,
         vmk_uint32 *debuglevel);
   #else
      VMK_ReturnStatus NvmeMgmt_SetLogLevel(vmk_uint64 cookie,
         vmk_uint64 instanceId,
         vmk_uint32 *loglevel,
         vmk_uint32 *debuglevel);
   #endif
#else
   #define NvmeMgmt_SetLogLevel (NULL)
#endif

/**
 * Signature declaration for global management handle
 *
 * The definition is in nvme_mgmt_common.c and is shared by both UW and kernel
 */
extern vmk_MgmtApiSignature globalSignature;

#endif /* _NVME_MGMT_H_ */
