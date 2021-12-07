/*****************************************************************************
 * Copyright (c) 2020  VMware, Inc. All rights reserved.
 *****************************************************************************/

/*
 * nvme_mgmt.h --
 *
 *    Driver management interface of nvme driver, shared by kernel and
 *    user space tools.
 */
#ifndef _NVME_MGMT_H_
#define _NVME_MGMT_H_

#include <vmkapi.h>

#define NVME_MGMT_NAME "nvmeMgmt"
#define NVME_MGMT_VENDOR "VMware"
#define NVME_MGMT_MAJOR (1)
#define NVME_MGMT_MINOR (0)
#define NVME_MGMT_UPDATE (0)
#define NVME_MGMT_PATCH (0)

#define XFER_TO_DEV           0
#define XFER_FROM_DEV         1
#define XFER_NO_DATA          2

#define ADAPTER_ONLINE        1
#define ADAPTER_OFFLINE       0

#define NS_ONLINE             1
#define NS_OFFLINE            0

#define NVME_MGMT_MAX_ADAPTERS 64

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
   NVME_IOCTL_DUMP_STATS_DATA,   /* Dump statistics data */
   NVME_IOCTL_SET_NS_ONLINE,     /* Online specific namespace */
   NVME_IOCTL_SET_NS_OFFLINE,    /* Offline specific namespace */
   NVME_IOCTL_UPDATE_NS,         /* Update namespace attributes */
   NVME_IOCTL_GET_NS_STATUS,     /* Get status of specific namespace */
   NVME_IOCTL_GET_INT_VECT_NUM,  /* Get number of interrupt vectors */
   NVME_IOCTL_SET_TIMEOUT,       /* Set timeout value */
   NVME_IOCTL_GET_TIMEOUT,       /* Get timeout value */
   NVME_IOCTL_UPDATE_NS_LIST,    /* Update namespace list */
   NVME_IOCTL_GET_MAX_XFER_LEN,  /* Get controller max data transfer length */
};

enum {
   NVME_MGMT_GLOBAL_CB_START = VMK_MGMT_RESERVED_CALLBACKS,
   NVME_MGMT_GLOBAL_CB_LISTADAPTERS,
   NVME_MGMT_GLOBAL_CB_END,
};
#define NVME_MGMT_GLOBAL_NUM_CALLBACKS (NVME_MGMT_GLOBAL_CB_END - NVME_MGMT_GLOBAL_CB_START - 1)

enum {
   NVME_MGMT_CB_START = VMK_MGMT_RESERVED_CALLBACKS,
   NVME_MGMT_CB_SMART,
   NVME_MGMT_CB_IOCTL,
   NVME_MGMT_CB_END,
};
#define NVME_MGMT_ADAPTER_NUM_CALLBACKS (NVME_MGMT_CB_END - NVME_MGMT_CB_START - 1)

typedef struct NvmeAdapterInfo {
   char           name[VMK_MISC_NAME_MAX];
   /** Management signature */
   char           signature[VMK_MISC_NAME_MAX];
   /** Status of adapter */
   vmk_uint64     status;
   /** Cookie, should be the pointer to ctrlr */
   vmk_uint64     cookie;
} NvmeAdapterInfo;

typedef struct NvmeUserIo {
   union {
      /* NVM identify command specific info. */
      vmk_NvmeIdentifyCmd identify;
      /* NVM set features command specific info. */
      vmk_NvmeSetFeaturesCmd setFeatures;
      /* NVM get features command specific info. */
      vmk_NvmeGetFeaturesCmd getFeatures;
      /* NVM namespace management command specific info. */
      vmk_NvmeNamespaceManagementCmd nsMgmt;
      /* NVM firmware activate command specific info. */
      vmk_NvmeFirmwareCommitCmd firmwareActivate;
      /* NVM firmware download command specific info. */
      vmk_NvmeFirmwareDownloadCmd firmwareDownload;
      /* NVM namespace attachment command specific info. */
      vmk_NvmeNamespaceAttachmentCmd nsAttach;
      /* NVM get log page command specific data. */
      vmk_NvmeGetLogPageCmd getLogPage;
      /* NVM Format Media command specific data. */
      vmk_NvmeFormatNVMCmd format;
      /* NVM Vendor Specific Command */
      vmk_NvmeVendorSpecificCmd vendorSpecificCmd;
      /* Submission queue entry. */
      vmk_NvmeSubmissionQueueEntry cmd;
   } cmd;
   vmk_NvmeCompletionQueueEntry comp; /* Completion entry */
   vmk_uint8 namespaceID;             /* Namespace ID, -1 for non-specific */
   vmk_uint8 direction;               /* direction TO_DEVICE/FROM_DEVICE */
   vmk_uint16 reserved;               /* reserved */
   vmk_uint32 status;                 /* Command status */
   vmk_uint32 length;                 /* data length */
   vmk_uint32 metaLen;                /* meta data length */
   vmk_uint64 timeoutUs;              /* timeout in microseconds */
   vmk_uint64 addr;                   /* data address */
   vmk_uint64 metaAddr;               /* meta data address */
} NvmeUserIo;

#endif /* _NVME_MGMT_H_ */
