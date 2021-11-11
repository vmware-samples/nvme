/*******************************************************************************
 * Copyright (c) 2016  VMware, Inc. All rights reserved.
 *******************************************************************************/

/*
 * @file: nvme_pcie_int.h --
 *
 *   Internal header file of nvme_pcie driver.
 */

#ifndef _NVME_PCIE_INT_H_
#define _NVME_PCIE_INT_H_

/*
 * XXX Specify the code modules that are allowed to use this interface.
 * XXX Possible values are
 * XXX    INCLUDE_ALLOW_DISTRIBUTE
 * XXX    INCLUDE_ALLOW_MODULE
 * XXX    INCLUDE_ALLOW_USERLEVEL
 * XXX    INCLUDE_ALLOW_VMCORE
 * XXX    INCLUDE_ALLOW_VMKERNEL
 * XXX    INCLUDE_ALLOW_VMK_MODULE
 * XXX    INCLUDE_ALLOW_VMIROM
 * XXX    INCLUDE_ALLOW_VMMON
 * XXX    INCLUDE_ALLOW_VMX
 */
#define INCLUDE_ALLOW_VMK_MODULE
//#include "includeCheck.h"

#include "nvme_pcie.h"
#include "nvme_pcie_os.h"
#include "nvme_pcie_debug.h"

/**
 * Driver name. This should be the name of the SC file.
 */
#define NVME_PCIE_DRIVER_NAME "nvme_pcie"

/**
 * Driver version. This should always in sync with .sc file.
 */
#define NVME_PCIE_DRIVER_VERSION "1.2.3.2"

/**
 * Driver release number. This should always in sync with .sc file.
 */
#define NVME_PCIE_DRIVER_RELEASE "1"

/**
 * Driver identifier, concatenation of driver name, version, and release
 */
#define NVME_PCIE_DRIVER_IDENT (NVME_PCIE_DRIVER_NAME "_" NVME_PCIE_DRIVER_VERSION \
                                "-" NVME_PCIE_DRIVER_RELEASE "vmw")

// TODO: define the maximum controller and queue numbers
#define NVME_PCIE_MAX_CONTROLLERS 64
#define NVME_PCIE_MAX_IO_QUEUES 16

#define NVME_INVALID_SQ_HEAD 0xffffffff
/**
 *TODO: temporarily set max queue size to 1024, which is used in native nvme driver.
 *      Theoretically this value could be VMK_NVME_MAX_IO_QUEUE_SIZE.
 */
#define NVME_PCIE_MAX_IO_QUEUE_SIZE 1024
// TODO: estimate heap alloc size
#define NVME_PCIE_HEAP_EST VMK_MEGABYTE

/* Admin command timeout, 2 seconds in microseconds */
#define ADMIN_TIMEOUT (2 * 1000 * 1000)

#define NVME_PCIE_PRP_ENTRY_SIZE sizeof(vmk_uint64)
#define NVME_PCIE_MAX_PRPS (VMK_PAGE_SIZE/NVME_PCIE_PRP_ENTRY_SIZE)
#define NVME_PCIE_MAX_TRANSFER_SIZE (NVME_PCIE_MAX_PRPS * VMK_PAGE_SIZE)

typedef struct NVMEPCIEController NVMEPCIEController;
typedef struct NVMEPCIECmdInfo NVMEPCIECmdInfo;
typedef struct NVMEPCIEQueueInfo NVMEPCIEQueueInfo;

/**
 * Submission queue
 */
typedef struct NVMEPCIESubQueueInfo {
   vmk_Lock lock;
   vmk_uint32 id;
   vmk_uint16 head;
   vmk_uint16 tail;
   vmk_uint32 qsize;
   vmk_atomic32 pendingHead;
   vmk_NvmeSubmissionQueueEntry *subq;
   vmk_IOA subqPhy;
   vmk_IOA doorbell;
   NVMEPCIEDmaEntry dmaEntry;
} NVMEPCIESubQueueInfo;

/**
 * Completion queue
 */
typedef struct NVMEPCIECompQueueInfo {
   vmk_Lock lock;
   vmk_uint32 id;
   vmk_uint16 head;
   vmk_uint16 tail;
   vmk_uint32 qsize;
   vmk_NvmeCompletionQueueEntry *compq;
   vmk_IOA compqPhy;
   vmk_IOA doorbell;
   vmk_uint32 phase;
   vmk_uint32 intrIndex;
   NVMEPCIEDmaEntry dmaEntry;
} NVMEPCIECompQueueInfo;

/**
 * Callback to be invoked when a command is completed by hardware
 */
typedef void (*NVMEPCIECompleteCommandCb)(NVMEPCIEQueueInfo *qinfo,
                                          NVMEPCIECmdInfo *cmdInfo);

typedef enum NVMEPCIECmdType {
   NVME_PCIE_FREE_CONTEXT,
   NVME_PCIE_ASYNC_CONTEXT,  /** Async command */
   NVME_PCIE_SYNC_CONTEXT,   /** Internal sync command */
   NVME_PCIE_ABORT_CONTEXT,  /** Command aborted */
} NVMEPCIECmdType;

typedef enum NVMEPCIECmdStatus {
   NVME_PCIE_CMD_STATUS_FREE,
   NVME_PCIE_CMD_STATUS_ACTIVE,
   NVME_PCIE_CMD_STATUS_DONE,
   NVME_PCIE_CMD_STATUS_FREE_ON_COMPLETE,
} NVMEPCIECmdStatus;

/**
 * Nvme command info
 */
typedef struct NVMEPCIECmdInfo {
   /** for list process */
   vmk_ListLinks list;
   /** Command ID */
   vmk_uint16 cmdId;
   /** payload */
   vmk_NvmeCommand *vmkCmd;
   /** Completion callback */
   NVMEPCIECompleteCommandCb done;
   /** Command type */
   vmk_uint32 type;
   /** Completion callback data */
   void *doneData;
   /** Indicate if the command is active or not */
   vmk_atomic32 atomicStatus;
   /** point to next free cmdInfo */
   vmk_uint32 freeLink;
} NVMEPCIECmdInfo;

typedef union NVMEPCIEPendingCmdInfo {
   struct {
      vmk_uint32 cmdOffset;
      vmk_uint32 freeListLength;
   };
   vmk_atomic64 atomicComposite;
} NVMEPCIEPendingCmdInfo;

/**
 * Nvme command list
 */
typedef struct NVMEPCIECmdInfoList {
   vmk_Lock lock;
   int nrAct;
   NVMEPCIEPendingCmdInfo pendingFreeCmdList;
   vmk_uint32 freeCmdList;
   NVMEPCIECmdInfo *list;
   int idCount;
} NVMEPCIECmdInfoList;

typedef enum NVMEPCIEQueueState {
   NVME_PCIE_QUEUE_NON_EXIST,
   NVME_PCIE_QUEUE_SUSPENDED,
   NVME_PCIE_QUEUE_ACTIVE,
} NVMEPCIEQueueState;

/**
 * Queue info
 */
typedef struct NVMEPCIEQueueInfo {
   int id;
   vmk_atomic32 state;
   vmk_atomic32 refCount;
   NVMEPCIEController *ctrlr;
   NVMEPCIESubQueueInfo *sqInfo;
   NVMEPCIECompQueueInfo *cqInfo;
   NVMEPCIECmdInfoList *cmdList;
} NVMEPCIEQueueInfo;

/* to mark the special device needs some workaround */
typedef enum NVMEPCIEWorkaround {
   NVME_PCIE_WKR_ALL_AWS = 1,
   NVME_PCIE_WKR_MAX,
} NVMEPCIEWorkaround;
/**
 * NVMEPCIEController structure (per SBDF)
 */
typedef struct NVMEPCIEController {
   vmk_Name name;
   vmk_ListLinks list;
   int bar;
   int barSize;
   vmk_VA regs;
   vmk_atomic32 numIoQueues;
   vmk_uint32 maxIoQueues;
   NVMEPCIECtrlrOsResources osRes;
   NVMEPCIEQueueInfo *queueList;
   vmk_Bool isRemoved;
   NVMEPCIEWorkaround workaround;
   vmk_uint32 dstrd;
} NVMEPCIEController;

/**
 * Get controller's name
 */
static inline const char *
NVMEPCIEGetCtrlrName(NVMEPCIEController *ctrlr)
{
   return vmk_NameToString(&ctrlr->name);
}

/**
 * Return the heap allocation for each queue construction
 */
static inline vmk_ByteCount
NVMEPCIEQueueAllocSize(void)
{
   return (sizeof(NVMEPCIEQueueInfo) +
           sizeof(NVMEPCIESubQueueInfo) +
           sizeof(NVMEPCIECompQueueInfo) +
           sizeof(NVMEPCIECmdInfo) * NVME_PCIE_MAX_IO_QUEUE_SIZE +
           vmk_SpinlockAllocSize(VMK_SPINLOCK) * 3);
}

/**
 * Read 32bit MMIO
 */
static inline vmk_uint32
NVMEPCIEReadl(vmk_VA addr)
{
   vmk_CPUMemFenceRead();
   return (*(volatile vmk_uint32 *)(addr));
}

/**
 * Write to 32bit MMIO
 */
static inline void
NVMEPCIEWritel(vmk_uint32 value, vmk_VA addr)
{
   vmk_CPUMemFenceWrite();
   (*(volatile vmk_uint32 *)(addr)) = value;
}


/**
 * Read 64bit MMIO
 */
static inline vmk_uint64
NVMEPCIEReadq(vmk_VA addr)
{
   vmk_CPUMemFenceRead();
   return (*(volatile vmk_uint64 *)(addr));
}


/**
 * Write to 64bit MMIO
 */
static inline void
NVMEPCIEWriteq(vmk_uint64 value, vmk_VA addr)
{
   vmk_CPUMemFenceWrite();
   NVMEPCIEWritel(value & 0xffffffff, addr);
   NVMEPCIEWritel((value & 0xffffffff00000000UL) >> 32, addr+4);
}

/**
 * Return true if this is an AWS EBS data volume device.
 *
 * Add this special case to customize some configuration (IO queue number and DMA
 * constraints sgElemAlignment & sgElemSizeMult) for AWS EBS data volume device.
 * Refer to PR #2126797.
 */
static inline vmk_Bool
NVMEPCIEIsEBSCustomDevice(NVMEPCIEController *ctrlr)
{
   vmk_PCIDeviceID *pciId = &ctrlr->osRes.pciId;

   return (
           /* r5.metal */
           (pciId->vendorID == 0x1d0f && pciId->deviceID == 0x0065) ||
           /* r5.xlarge  */
           (pciId->vendorID == 0x1d0f && pciId->deviceID == 0x8061)
          );
}

/**
 * Return true if this is an AWS local NVMe device.
 *
 * Add this special case to customize DMA constraints sgElemAlignment & sgElemSizeMult
 * for AWS local NVMe device.
 */
static inline vmk_Bool
NVMEPCIEIsAWSLocalDevice(NVMEPCIEController *ctrlr)
{
   vmk_PCIDeviceID *pciId = &ctrlr->osRes.pciId;

   return (
           /* AWS EC2 */
           (pciId->vendorID == 0x1d0f && pciId->deviceID == 0xcd00)
          );
}

static inline void
NVMEPCIEDetectWorkaround(NVMEPCIEController *ctrlr)
{
   vmk_PCIDeviceID *pciId = &ctrlr->osRes.pciId;
   /* enable AQA workaround for all AWS hardware.
    * m5.xlarge vid=0x1d0f, devid=0x8061,
    * i3.metal vid=0x1d0f, devid=0xcd00,
    * r5.metal vid=0x1d0f, devid=0x0065,
    */
   if (pciId->vendorID == 0x1d0f) {
      ctrlr->workaround = NVME_PCIE_WKR_ALL_AWS;
   }
}

/** Queue functions */
VMK_ReturnStatus NVMEPCIEQueueCreate(NVMEPCIEController *ctrlr,
                                       vmk_uint32 qid,
                                       vmk_uint32 qsize);
VMK_ReturnStatus NVMEPCIEQueueDestroy(NVMEPCIEController *ctrlr,
                                      vmk_uint32 qid,
                                      vmk_NvmeStatus status);
VMK_ReturnStatus NVMEPCIEStartQueue(NVMEPCIEQueueInfo *qinfo);
VMK_ReturnStatus NVMEPCIEStopQueue(NVMEPCIEQueueInfo *qinfo,
                                   vmk_NvmeStatus status);
VMK_ReturnStatus NVMEPCIEResumeQueue(NVMEPCIEQueueInfo *qinfo);
void NVMEPCIESuspendQueue(NVMEPCIEQueueInfo *qinfo);
void NVMEPCIEProcessCq(NVMEPCIEQueueInfo *qinfo);

/** vmk nvme adapter and controller init/cleanup functions */
VMK_ReturnStatus NVMEPCIEAdapterInit(NVMEPCIEController *ctrlr);
VMK_ReturnStatus NVMEPCIEAdapterDestroy(NVMEPCIEController *ctrlr);
VMK_ReturnStatus NVMEPCIEControllerInit(NVMEPCIEController *ctrlr);
VMK_ReturnStatus NVMEPCIEControllerDestroy(NVMEPCIEController *ctrlr);

/** IO functions */
VMK_ReturnStatus NVMEPCIESubmitAsyncCommand(NVMEPCIEController *ctrlr,
                                            vmk_NvmeCommand *vmkCmd,
                                            vmk_uint32 qid);
VMK_ReturnStatus NVMEPCIESubmitSyncCommand(NVMEPCIEController *ctrlr,
                                           vmk_NvmeCommand *vmkCmd,
                                           vmk_uint32 qid,
                                           vmk_uint8 *buf,
                                           vmk_uint32 length,
                                           int timeoutUs);
VMK_ReturnStatus NVMEPCIEIdentify(NVMEPCIEController *ctrlr,
                                  vmk_NvmeCnsField cns,
                                  vmk_uint32 nsID,
                                  vmk_uint8 *data);

/** Interrupt functions */
VMK_ReturnStatus NVMEPCIEIntrAlloc(NVMEPCIEController *ctrlr,
                                   vmk_PCIInterruptType type,
                                   vmk_uint32 numDesired);
void NVMEPCIEIntrFree(NVMEPCIEController *ctrlr);

VMK_ReturnStatus NVMEPCIEQueueIntrAck(void *handlerData,
                                      vmk_IntrCookie intrCookie);
void NVMEPCIEQueueIntrHandler(void *handlerData,
                              vmk_IntrCookie intrCookie);

/** Debug functions */
void NVMEPCIEDumpSqe(NVMEPCIEController *ctrlr,
                     vmk_NvmeSubmissionQueueEntry *sqe);
void NVMEPCIEDumpCqe(NVMEPCIEController *ctrlr,
                     vmk_NvmeCompletionQueueEntry *cqe);
void NVMEPCIEDumpCommand(NVMEPCIEController *ctrlr,
                         vmk_NvmeCommand *vmkCmd);
#endif // ifndef _NVME_PCIE_INT_H_
