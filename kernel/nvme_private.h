/*********************************************************************************
 * Copyright 2013 VMware, Inc. All rights reserved.
 * ******************************************************************************/

/**
*******************************************************************************
** Copyright (c) 2012-2013  Integrated Device Technology, Inc.               **
**                                                                           **
** All rights reserved.                                                      **
**                                                                           **
*******************************************************************************
**                                                                           **
** Redistribution and use in source and binary forms, with or without        **
** modification, are permitted provided that the following conditions are    **
** met:                                                                      **
**                                                                           **
**   1. Redistributions of source code must retain the above copyright       **
**      notice, this list of conditions and the following disclaimer.        **
**                                                                           **
**   2. Redistributions in binary form must reproduce the above copyright    **
**      notice, this list of conditions and the following disclaimer in the  **
**      documentation and/or other materials provided with the distribution. **
**                                                                           **
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS   **
** IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, **
** THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR    **
** PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR         **
** CONTRIBUTORS BE LIABLE FOR ANY DIRECT,INDIRECT, INCIDENTAL, SPECIAL,      **
** EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,       **
** PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR        **
** PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF    **
** LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING      **
** NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS        **
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.              **
**                                                                           **
** The views and conclusions contained in the software and documentation     **
** are those of the authors and should not be interpreted as representing    **
** official policies, either expressed or implied,                           **
** Integrated Device Technology Inc.                                         **
**                                                                           **
*******************************************************************************
**/

/*
 * @file: nvme_private.h --
 *
 *    Private data structures and functions for native nvme driver.
 */

#ifndef _NVME_PRIVATE_H_
#define _NVME_PRIVATE_H_

#include "vmkapi.h"
#include "nvme_os.h"
#include "nvme.h"
#include "../common/nvme_mgmt.h"

/**
 * Stuff in this header file are being splitted into nvme_core.h and oslib.h.
 */
#include "oslib.h"
#include "nvme_core.h"

/**
 * Driver name. This should be the name of the SC file.
 */
#define NVME_DRIVER_NAME "nvme"


/**
 * Driver version. This should always in sync with .sc file.
 */
#define NVME_DRIVER_VERSION "1.0e.0.35"

/**
 * Driver release number. This should always in sync with .sc file.
 */
#define NVME_DRIVER_RELEASE "1"


/**
 * Driver identifier, concatenation of driver name, version, and release
 */
#define NVME_DRIVER_IDENT (NVME_DRIVER_NAME "_" NVME_DRIVER_VERSION "-" NVME_DRIVER_RELEASE "vmw")


#define NVME_MUL_COMPL_WORLD              1

/*****************************************************************************
 Exported Symbols
 ****************************************************************************/

struct NvmeDmaEntry;
struct NvmeCtrlr;
struct NvmeQueueInfo;
struct NvmeSubQueueInfo;
struct NvmeCmdInfo;

extern int  nvme_force_intx;
extern int  max_prp_list;
extern int  admin_sub_queue_size;
extern int  admin_cpl_queue_size;
extern int  io_sub_queue_size;
extern int  io_cpl_queue_size;
extern int  io_command_id_size;
extern int  transfer_size;
extern int  max_io_request;    /** @todo - optimize */
extern int  max_namespaces;
#if NVME_MUL_COMPL_WORLD
extern int  nvme_compl_worlds_num;
#endif


/**
 * Determine whether to enable debugging facilities in the driver
 *
 * 0 - Debugging facilities disabeld
 * 1 - Debugging facilities enabled
 */
#define NVME_DEBUG                        0


/**
 * Determine whether to enable error injection facilities in the driver
 *
 * 0 - Debugging facilities disabled
 * 1 - Debugging facilities enabled
 */
#define NVME_DEBUG_INJECT_ERRORS          (0 && NVME_DEBUG)


/**
 * Determine whether to inject command timeout errors.
 *
 * 0 - timeout injection disabled
 * 1 - timeout injection enabled
 */
#define NVME_DEBUG_INJECT_TIMEOUT         (0 && NVME_DEBUG_INJECT_ERRORS)


/**
 * Determine whether to inject delays during stage stransitions for hot plug
 * testing.
 *
 * 0 - no delays
 * 1 - delay between state transitions
 */
#define NVME_DEBUG_INJECT_STATE_DELAYS    (0 && NVME_DEBUG_INJECT_ERRORS)

#if NVME_DEBUG_INJECT_STATE_DELAYS
/**
 * Time (in microseconds) to delay between state transitions.
 */
#define NVME_DEBUG_STATE_DELAY_US         (5 * 1000 * 1000)
#endif



/*****************************************************************************
 Driver Flags
 ****************************************************************************/

/* Driver Properties */

#if NVME_MUL_COMPL_WORLD
#define NVME_MAX_IO_QUEUES               (16)
#else
#define NVME_MAX_IO_QUEUES               (2)
#endif

/**
 * max completion worlds is equal to max io queues
 */
#define NVME_MAX_COMPL_WORLDS         NVME_MAX_IO_QUEUES

/**
 * initial size of the default heap
 */
#define NVME_DRIVER_PROPS_HEAP_INITIAL (3 * 1024 * 1024)

/**
 * max size of the default heap
 */
#define NVME_DRIVER_PROPS_HEAP_MAX (1024 * 1024 * (NVME_MAX_IO_QUEUES+1) * (NVME_MAX_ADAPTERS))

/**
 * name of the default heap
 */
#define NVME_DRIVER_PROPS_HEAP_NAME "nvmeHeap"

/**
 * name of the default log handle
 */
#define NVME_DRIVER_PROPS_LOG_NAME "nvmeLogHandle"

/**
 * name of the driver handle
 */
#define NVME_DRIVER_PROPS_DRIVER_NAME "nvmeDriver"

/**
 * max number of PRP entries per cmd
 */
#define NVME_DRIVER_PROPS_MAX_PRP_LIST_ENTRIES (32)

/**
 * name of the controller's dma engine
 */
#define NVME_DRIVER_PROPS_CTRLR_DMAENGINE_NAME "nvmeCtrlrDmaEngine"

/**
 * dma constraints for ctrlr
 */
#define NVME_DRIVER_PROPS_CTRLR_DMAENGINE_ADDRMASK (VMK_ADDRESS_MASK_64BIT)
#define NVME_DRIVER_PROPS_CTRLR_DMAENGINE_MAXXFER (NVME_DRIVER_PROPS_MAX_PRP_LIST_ENTRIES * VMK_PAGE_SIZE)
#define NVME_DRIVER_PROPS_CTRLR_DMAENGINE_SGMAXENTRIES (NVME_DRIVER_PROPS_MAX_PRP_LIST_ENTRIES)
#define NVME_DRIVER_PROPS_CTRLR_DMAENGINE_SGELEMMAXSIZE (0)
#define NVME_DRIVER_PROPS_CTRLR_DMAENGINE_SGELEMSIZEMULT (0)
#define NVME_DRIVER_PROPS_CTRLR_DMAENGINE_SGELEMALIGN (VMK_PAGE_SIZE)
#define NVME_DRIVER_PROPS_CTRLR_DMAENGINE_SGELEMSTRADDLE (0)


/**
 * reservation of driver's mempool (shared by all controllers)
 */
#define NVME_DRIVER_PROPS_MPOOL_RESV (10 * 1024 * 1024 / VMK_PAGE_SIZE)

/**
 * limit of driver's mempool (shared by all controllers)
 */
#define NVME_DRIVER_PROPS_MPOOL_LIMIT (4 * 1024 * 1024 * (NVME_MAX_IO_QUEUES+1) / VMK_PAGE_SIZE * (NVME_MAX_ADAPTERS))


/**
 * name of driver's mempool
 */
#define NVME_DRIVER_PROPS_MPOOL_NAME "nvmeMemPool"


/**
 * maximum length of SCSI CDB supported
 */
#define NVME_DRIVER_PROPS_MAX_CMD_LEN (16)


/**
 * Current IDT controller does not return globally unique EUI64 identifier
 * through IDENTIFY NAMESPACE return code, so we cannot generate unique SCSI
 * identifier in VPD83 return code. We have to disable VPD80/83 before we
 * come out a final solution.
 */
#define NVME_ENABLE_SCSI_DEVICEID (1)


/**
 * Controller State
 */

typedef enum {
   NVME_CTRLR_STATE_INIT = 0,
   NVME_CTRLR_STATE_STARTED,
   NVME_CTRLR_STATE_OPERATIONAL,
   NVME_CTRLR_STATE_SUSPEND,
   NVME_CTRLR_STATE_INRESET,
   NVME_CTRLR_STATE_MISSING,
   NVME_CTRLR_STATE_QUIESCED,
   NVME_CTRLR_STATE_DETACHED,
   NVME_CTRLR_STATE_FAILED,
   NVME_CTRLR_STATE_LAST,
} Nvme_CtrlrState;



/**
 * Driver specific status code
 *
 * All requests with driver status code not be 'NVME_DRIVER_STATUS_OK'
 * shall be returned immediately in the issuing path.
 *
 * All requests that has been sent to the hardware successfully
 * shall be returned in the completion path or exceptino handling
 * path.
 */
typedef enum {
   /** driver success, check NVMe code for further information. */
   NVME_DRIVER_STATUS_OK,
   /** generic driver failure */
   NVME_DRIVER_STATUS_FAILED,
   /** queue full */
   NVME_DRIVER_STATUS_QFULL,
   /** quiesced, driver unload or shut down in progress */
   NVME_DRIVER_STATUS_QUIESCED,
   /** task management in progress */
   NVME_DRIVER_STATUS_BUSY_TASK_MGMT,
   /** invalid opcode */
   NVME_DRIVER_STATUS_INVALID_OPCODE,
   /** invalid field in cdb */
   NVME_DRIVER_STATUS_INVALID_FIELD_IN_CDB,
   /** lba out of range */
   NVME_DRIVER_STATUS_LBA_OUT_OF_RANGE,
   /** logical unit not ready */
   NVME_DRIVER_STATUS_LU_NOT_READY,
   /** gate keeper, invalid status code */
   NVME_DRIVER_STATUS_LAST,
} Nvme_DriverStatusCode;


/**
 * Make Nvme_Status code from driver code and status field
 */
#define NVME_MAKE_STATUS(driverStatusCode, nvmeStatusField) \
   ((driverStatusCode & 0xffff) << 16 | 0 << 15 | nvmeStatusField)


/**
 * Get the driver status code from Nvme_Status code
 */
#define NVME_DRIVER_STATUS(nvmeStatus) \
   (nvmeStatus >> 16)


/**
 * Get the NVMe status code type from Nvme_Status code
 */
#define NVME_STATUS_CODE_TYPE(nvmeStatus) \
   ((nvmeStatus >> 8) & 0x3)


/**
 * Get the NVMe status code from Nvme_Status code
 */
#define NVME_STATUS_CODE(nvmeStatus) \
   (nvmeStatus & 0xff);


/**
 * Check whether Nvme_Status indicates success state.
 *
 *    This should check the following combinations:
 *       NVME_DRIVER_STATUS(nvmeStatus) == NVME_DRIVER_STATUS_OK (0) &&
 *       NVME_STATUS_CODE_TYPE(nvmeStatus) == NVME_SCT_GENERIC (0) &&
 *       NVME_STATUS_CODE(nvmeStatus) == NVME_SC_SUCCESS (0)
 *
 *    Since nvmeStatus is a bitwise OR of the above status code, we can
 *    just verify that nvmeStatus == 0 here to check if nvmeStatus
 *    yields success.
 */
#define NVME_STATUS_SUCCEEDED(nvmeStatus) \
   (nvmeStatus == 0)


/**
 * Definition of succeeded NVM command status code
 */
#define NVME_STATUS_OK NVME_MAKE_STATUS(NVME_DRIVER_STATUS_OK, 0)


/**
 * NVMe status code
 *
 *    Higher 16bit is driver defined status code
 *    Mid 1 bit is reserved
 *    Lower 15 bit is Completion Queue Entry Status Field.
 *
 */
/**
 * typedef vmk_uint32 Nvme_Status;
 */


/*****************************************************************************
 Driver Defined Data Structures
 ****************************************************************************/


#define FREE_CONTEXT 0         /* Unused context */
#define ADMIN_CONTEXT   1         /* Driver Admin Command request */
#define BIO_CONTEXT  2         /* Block IO request */
#define IOCTL_CONTEXT   3         /* IOCTL request */
#define EVENT_CONTEXT   4         /* Async. Event request */
#define LOG_CONTEXT  5         /* Log Page request */
#define ERR_CONTEXT  6         /* Error Page request */
#define ABORT_CONTEXT   7         /* Command aborted */

#define MAX_RETRY 2


#if NVME_DEBUG
#define DEVICE_TIMEOUT      100     /* IO Timeout in seconds */
#define TIMEOUT_FREQ     (10*1000)
#else
#define DEVICE_TIMEOUT      10         /* IO Timeout in seconds */
#define TIMEOUT_FREQ     (1000)
#endif

#define TIMEOUT_LIST       ((DEVICE_TIMEOUT*1000)/TIMEOUT_FREQ)
#define ADMIN_TIMEOUT      (2 * 1000 * 1000)  /* 2 seconds in microseconds */

#define MAX_EVENTS       7

#define LOG_PG_SIZE          (512)

#if NVME_MUL_COMPL_WORLD
typedef struct _NvmeIoRequest{
   vmk_SList_Links     link;
   vmk_ScsiCommand    *vmkCmd;
} NvmeIoRequest;

typedef struct _NvmeIoCompletionQueue {
   vmk_Lock            lock;
   vmk_SList           complList;
   vmk_WorldID         worldID;
   struct NvmeCtrlr *ctrlr;
} NvmeIoCompletionQueue;
#endif

struct NvmeCmdInfo {
   /** for list process */
   vmk_ListLinks list;
   /** payload */
   union {
      vmk_ScsiCommand *vmkCmd;
      struct usr_io *uio;
   };
   /** nvme command struct */
   struct nvme_cmd nvmeCmd;
   /** nvme completion entry struct */
   struct cq_entry cqEntry;
   /** type of command */
   vmk_uint32  type;
   /** indicating whether the command is active or not */
   vmk_uint32  status;
   /** cache for the command completion status */
   Nvme_Status cmdStatus;
   /** nvme command identifier */
   vmk_uint16  cmdId;
   /** timeout indicator */
   vmk_uint16  timeoutId;
   /** bytes carried in this request */
   vmk_uint64  count;
   /** number of sub-commands running */
   vmk_uint32  cmdCount;
   /** number of retries */
   vmk_uint16  cmdRetries;
   /** attached namespace info */
   struct NvmeNsInfo *ns;
   /** pointer to the base info, if it is a split command */
   struct NvmeCmdInfo *cmdBase;
   /** pre-allocated PRP pages */
   struct nvme_prp    *prps;
   /** DMA address of the PRP pages */
   vmk_IOA             prpPhy;
   /** structure for tracking the PRP DMA buffer */
   struct NvmeDmaEntry dmaEntry;
   /** for stats */
   union {
      vmk_uint64 startTime;
      vmk_uint64 cmdParam;
   };
   /** Start position in the sg array of the base reqeust */
   vmk_SgPosition sgPosition;
   /** for tracking number of bytes requested */
   vmk_uint32  requestedLength;
   /**
    * total number of bytes required in the sg array, for base request only
    */
   vmk_ByteCount requiredLength;
   /** completion callback */
   NvmeCore_CompleteCommandCb done;
   /** completion callback data */
   void  *doneData;
   /**
    * cleanup callback
    * This callback shall NEVER BLOCK. It only invoked in NvmeCore_SubmitCommandWait()
    * directly or in ISR completion routine (ProcessCq), in both cases, the qinfo->lock
    * is held.
    */
   NvmeCore_CleanupCommandCb cleanup;
   /** cleanup callback data */
   void *cleanupData;
};

struct NvmeSubQueueInfo {
   vmk_Lock lock;
   struct NvmeCtrlr *ctrlr;
   vmk_uint32 flags;
   vmk_uint32 id;
   vmk_uint32 qsize;
   vmk_uint32 entries;
   vmk_uint32 throttle;
   vmk_uint16 tail;
   vmk_uint16 head;
   struct nvme_cmd *subq;
   vmk_IOA subqPhy;
   struct NvmeQueueInfo *compq;
   vmk_IOA doorbell;

   struct NvmeDmaEntry dmaEntry;

   void (*lockFunc)(void *);
   void (*unlockFunc)(void *);
};

struct NvmeQueueInfo {
   vmk_Lock lock;
   struct NvmeCtrlr *ctrlr;

    /* Number of request */
   int      nrReq;
   /* Number of active cmds */
   int      nrAct;
   /* Max. Number of request */
   int      maxReq;

#define  QUEUE_READY (1 << 0)
#define  QUEUE_SUSPEND  (1 << 1)
#define  QUEUE_FLUSH (1 << 2)
#define  QUEUE_BUSY  (1 << 8)

   vmk_uint32 flags;
   vmk_uint32 id;
   vmk_uint32 qsize;
   vmk_uint32 idCount;
   vmk_uint32 prpCount;
   vmk_uint32 node;
   vmk_uint32 intrIndex;
   vmk_uint32 phase;
   vmk_uint32 timeoutId;
   vmk_uint16 tail;
   vmk_uint16 head;
   struct NvmeCmdInfo *cmdList;
   vmk_ListLinks cmdFree;
   vmk_ListLinks cmdActive;
   struct cq_entry *compq;
   vmk_IOA compqPhy;
   vmk_IOA doorbell;

   /** timeout list */
   vmk_uint32 timeout[TIMEOUT_LIST];
   struct NvmeSubQueueInfo *subQueue;

   vmk_SlabID prpSlab;

   struct NvmeDmaEntry dmaEntry;

   void (*lockFunc)(void *);
   void (*unlockFunc)(void *);
};


#define MAX_NR_QUEUES (128)


/**
 * definitions used for handling SMART
 */
#define SMART_VALID_TIME_RANGE	(120 * 1000)	/* 2 minutess in miliseconds */
#define SMART_TIMEOUT_WAIT	(60 * 1000)	/* 1 minute in miliseconds */
#define SMART_MAX_RETRY_TIMES	10		/* retry 4 times before fail the request*/

/**
 * struct NvmeCtrlr - holds a controller (per SBDF)'s instance data.
 */
struct NvmeCtrlr {
   /** Lock */
   vmk_Lock lock;

   /** Semaphore for task management */
   vmk_Semaphore taskMgmtMutex;

   /* List pointer */
   vmk_ListLinks list;

   /** Device handle */
   vmk_Device device;

   /** Controller name */
   vmk_Name name;

   /** PCI device handle, resource*/
   vmk_PCIDevice pciDevice;
   vmk_PCIDeviceID pciId;
   vmk_PCIDeviceAddr sbdf;
   vmk_IOReservation pciResv;

   /** Controller BAR */
   int bar;
   /** Bar size */
   int barSize;
   /** Bar mapped to virtual space */
   vmk_VA regs;

   /** DMA engine */
   vmk_DMAEngine dmaEngine;
   /** Scatter-Gather handle */
   vmk_SgOpsHandle sgHandle;

   /** Interrupt cookies */
   vmk_IntrCookie *intrArray;
   /** Number of interrupt vectors allocated */
   vmk_uint32 numVectors;
   /** MSIx mode enabled */
   vmk_uint32 msixEnabled;

   /** Lock domain */
   /* do we need controller wide lock? */
   vmk_LockDomainID lockDomain;

   /** Device State */
   Nvme_CtrlrState state;


#if 0
   /** Flags to stop thread */
   int threadStop;

#define THREAD_STOP     BIT_0
#define THREAD_RESTART     BIT_1
#define THREAD_AEN      BIT_2
#define THREAD_LOG      BIT_3
#define THREAD_ERR      BIT_4
   /** Flags to request a task */
   vmk_uint32 threadFlags;
#endif

   /* Version */
   vmk_uint32 version;
   /* No of completion queues */
   vmk_uint32 numIoQueues;
   /* Admin queue */
   struct NvmeQueueInfo adminq;
   /* Queue Info */
   struct NvmeQueueInfo *queueList[MAX_NR_QUEUES];
   /* Sub Queue Info */
   struct NvmeSubQueueInfo *subQueueList[MAX_NR_QUEUES];
   /* IO queues */
   struct NvmeQueueInfo *ioq;

   /* No of namespaces */
   int nsCount;

   /* List of namespaces */
   vmk_ListLinks nsList;

   /* Hardware Timeout */
   vmk_uint32 hwTimeout;

   /* PCIe Vendor ID */
   vmk_uint16 pcieVID;
   /* Controller serial number, last position holds terminator */
   char serial[21];
   /* Controller model number, last position holds terminator */
   char model[41];
   /* Firmware version, last position holds terminator */
   char firmwareRev[9];
   /* IEEE OUI */
   vmk_uint8 ieeeOui[3];
   /* Max nr of Async request */
   vmk_uint16 maxAen;
   /* Vendor Admin cmnd config */
   vmk_uint8 admVendCmdCfg;
   /* Vendor NVM cmnd config */
   vmk_uint8 nvmVendCmdCfg;
   /* NVM supported cache config */
   vmk_uint8 nvmCacheSupport;
   /* NVM supported command */
   vmk_uint8 nvmCmdSupport;
   /* Log Page Attributes */
   vmk_uint8 logPageAttr;
   /* Identity data */
   struct iden_controller identify;

   /** Timeout Index */
   int timeoutId;
   /* Max nr of Async request */
   vmk_uint16 curAen;

   /* Scsi Adapter */
   vmk_ScsiAdapter *scsiAdapter;
   /* Scsi DMA Engine */
   vmk_DMAEngine scsiDmaEngine;
   /* Scsi logical device */
   vmk_Device logicalDevice;
   /* Queue depth */
   vmk_uint32 qDepth;

   /* Management handle */
   vmk_MgmtHandle mgmtHandle;
   /* Management interface signature definition */
   vmk_MgmtApiSignature nvmeSignature;
   /* Dma entry for log page */
   struct NvmeDmaEntry smartDmaEntry;
   /* Last Update Time */
   volatile vmk_uint64 smartLastUpdateTime;
#if NVME_MUL_COMPL_WORLD
   /* slab ID for IO completion */
   vmk_SlabID complWorldsSlabID;
   /* IO completion queues */
   NvmeIoCompletionQueue IOCompletionQueue[NVME_MAX_COMPL_WORLDS];
   /* Flag of NVMe controller shutting down */
   vmk_Bool shuttingDown;
   /* Number of completion worlds*/
   vmk_uint32 numComplWorlds;
#endif
};

/**
 * Namespace information block data structure
 */
struct NvmeNsInfo {
   /** Lock that controls this structure */
   vmk_Lock lock;
   /** List of namespaces */
   vmk_ListLinks list;
   /** Controller context */
   struct NvmeCtrlr *ctrlr;
   /** Namespace flags */
   vmk_uint32 flags;
   /** Namespace ID */
   int id;
   /** Namespace Reference count */
   vmk_atomic64     refCount;

#define NS_ONLINE       (1 << 0)
#define NS_FLUSH        (1 << 1)
#define NS_READONLY     (1 << 2)

   /** Size of Namespace(blocks) */
   vmk_uint64       blockCount;
   /** Shift for lba address */
   int              lbaShift;
   /** features set NS_IDENTIFY */
   vmk_uint8        feature;
   /** Formatted LBA size */
   vmk_uint8        fmtLbaSize;
   /** Metadata Capability */
   vmk_uint8        metaDataCap;
   /** End-to-End prot. cap. */
   vmk_uint8        dataProtCap;
   /** End-to-End prot. set */
   vmk_uint8        dataProtSet;
   /** Size of Meta data */
   vmk_uint16       metasize;
   /** EUI64 */
   vmk_uint64       eui64;
};


/**
 * Task management type
 */
typedef enum {
   NVME_TASK_MGMT_RESET_START,
   NVME_TASK_MGMT_LUN_RESET,
   NVME_TASK_MGMT_DEVICE_RESET,
   NVME_TASK_MGMT_BUS_RESET,
   NVME_TASK_MGMT_RESET_END,
} Nvme_ResetType;


/**
 * Reset type to reset type name mapping
 */
static const char *Nvme_ResetTypeName [] = {
   "Invalid Reset",
   "Lun Reset",
   "Device Reset",
   "Bus Reset",
};


/**
 * Get reset type name from reset type
 */
static inline const char *
Nvme_GetResetTypeName(Nvme_ResetType resetType)
{
   if (resetType <= NVME_TASK_MGMT_RESET_START || resetType >= NVME_TASK_MGMT_RESET_END) {
      VMK_ASSERT(0);
      return Nvme_ResetTypeName[0];
   }
   return Nvme_ResetTypeName[resetType];
}


/**
 * Get controller's name
 */
static inline const char *
Nvme_GetCtrlrName(struct NvmeCtrlr *ctrlr)
{
   return vmk_NameToString(&ctrlr->name);
}

/**
 * @brief This function set memory blocks of 64bit aligned data.
 *     This routine assumes all blocke sizes 64bit aligned.
 *
 * @param[in] void *dst Destination address
 * @param[in] u64 val value to set memory.
 * @param[in] int cnt size in u64
 *
 * @return void none.
 */
static inline void
Nvme_Memset64(void *dst, vmk_uint64 val, int cnt)
{
   vmk_uint64 *p1 = dst;
   while(cnt--) {
       *p1++ = val;
   }
}


/**
 * @brief This function duplicates blocks of 64bit aligned data.
 *     This routine assumes all blocke sizes 64bit aligned.
 *
 * @param[in] void *dst Destination address
 * @param[in] void *src Source address
 * @param[in] int cnt size in u64
 *
 * @return void none.
 */
 static inline void
 Nvme_Memcpy64(void *dst, void *src, int cnt)
 {
    vmk_uint64 *p1 = dst, *p2 = src;
    while(cnt--) {
     *p1++ = *p2++;
  }
}


#define Nvme_WaitCond(ctrlr, wait, cond, result) \
{                                                              \
   int maxWait = wait * 10;                                    \
   Nvme_LogDebug("waiting %d.", maxWait);                      \
   result = VMK_OK;                                            \
   do {                                                        \
      result = vmk_WorldSleep(100 * 1000); /* sleep 100ms */   \
      if ((cond)) {                                            \
         break;                                                \
      }                                                        \
      if (result != VMK_OK) { /* some error happend */         \
         break;                                                \
      }                                                        \
      if (! --maxWait) {                                       \
         result = VMK_TIMEOUT;                                 \
         break;                                                \
      }                                                        \
   } while (!(cond));                                          \
   Nvme_LogDebug("cond %d, maxWait: %d, result: 0x%x.",        \
      (cond), maxWait, result);                                \
}


/**
 * Nvme_GetTimeUS - current time.
 *
 * @return vmk_uint64   milli-seconds.
 */
static inline vmk_uint64
Nvme_GetTimeUS(void)
{
   return vmk_TimerUnsignedTCToMS(vmk_GetTimerCycles());
}

VMK_ReturnStatus NvmeDriver_Register();
void NvmeDriver_Unregister();

VMK_ReturnStatus NvmeMgmt_GlobalInitialize();
VMK_ReturnStatus NvmeMgmt_GlobalDestroy();
VMK_ReturnStatus NvmeMgmt_CtrlrInitialize(struct NvmeCtrlr *ctrlr);
void NvmeMgmt_CtrlrDestroy(struct NvmeCtrlr *ctrlr);

VMK_ReturnStatus NvmeCtrlr_Attach(struct NvmeCtrlr *ctrlr);
VMK_ReturnStatus NvmeCtrlr_Detach(struct NvmeCtrlr *ctrlr);
VMK_ReturnStatus NvmeCtrlr_Start(struct NvmeCtrlr *ctrlr);
VMK_ReturnStatus NvmeCtrlr_Stop(struct NvmeCtrlr *ctrlr);
void NvmeCtrlr_SetMissing(struct NvmeCtrlr *ctrlr);

vmk_uint64 NvmeCtrlr_GetNs(struct NvmeNsInfo *ns);
vmk_uint64 NvmeCtrlr_PutNs(struct NvmeNsInfo *ns);
VMK_ReturnStatus NvmeCtrlr_DoTaskMgmtReset(struct NvmeCtrlr *ctrlr, Nvme_ResetType resetType, int nsId);
VMK_ReturnStatus NvmeCtrlr_DoTaskMgmtAbort(struct NvmeCtrlr *ctrlr, vmk_ScsiTaskMgmt *taskMgmt, struct NvmeNsInfo *ns);
VMK_ReturnStatus NvmeCtrlr_IoctlCommon(struct NvmeCtrlr *ctrlr, vmk_uint32 cmd, struct usr_io *uio);

Nvme_CtrlrState NvmeState_SetCtrlrState(struct NvmeCtrlr *ctrlr, Nvme_CtrlrState state, vmk_Bool locked);
const char * NvmeState_GetCtrlrStateString(Nvme_CtrlrState state);
Nvme_CtrlrState NvmeState_GetCtrlrState(struct NvmeCtrlr *ctrlr, vmk_Bool locked);


VMK_ReturnStatus NvmeCtrlrCmd_Identify(struct NvmeCtrlr *ctrlr, int nsId, vmk_IOA dmaAddr);
VMK_ReturnStatus NvmeCtrlrCmd_DeleteSq(struct NvmeCtrlr *ctrlr, vmk_uint16 id);
VMK_ReturnStatus NvmeCtrlrCmd_DeleteCq(struct NvmeCtrlr *ctrlr, vmk_uint16 id);
VMK_ReturnStatus NvmeCtrlrCmd_CreateCq(struct NvmeCtrlr *ctrlr, struct NvmeQueueInfo *qinfo, vmk_uint16 qid);
VMK_ReturnStatus NvmeCtrlrCmd_CreateSq(struct NvmeCtrlr *ctrlr, struct NvmeQueueInfo *qinfo, vmk_uint16 qid);
VMK_ReturnStatus NvmeCtrlrCmd_SetFeature(struct NvmeCtrlr *ctrlr, vmk_uint16 feature, vmk_uint32 option, struct nvme_prp *prp, struct cq_entry *cqEntry);
VMK_ReturnStatus NvmeCtrlrCmd_GetFeature(struct NvmeCtrlr *ctrlr, int ns_id, vmk_uint16 feature, vmk_uint32 option, struct nvme_prp *prp, struct cq_entry *cqEntry);
VMK_ReturnStatus NvmeCtrlrCmd_GetLogPage(struct NvmeCtrlr *ctrlr, vmk_uint32 nsID, struct smart_log *smartLog, vmk_Bool isSyncCmd);

VMK_ReturnStatus NvmeQueue_Construct(struct NvmeQueueInfo *qinfo, int sqsize, int cqsize, int qid, int shared, int intrIndex);
VMK_ReturnStatus NvmeQueue_Destroy(struct NvmeQueueInfo *qinfo);
VMK_ReturnStatus NvmeQueue_ResetAdminQueue(struct NvmeQueueInfo *qinfo);
Nvme_Status NvmeQueue_SubmitIoRequest(struct NvmeQueueInfo *qinfo, struct NvmeNsInfo *ns, vmk_ScsiCommand *vmkCmd, int retries);
void NvmeQueue_Flush(struct NvmeQueueInfo *qinfo, struct NvmeNsInfo *ns, int status);


VMK_ReturnStatus NvmeScsi_Init(struct NvmeCtrlr *ctrlr);
VMK_ReturnStatus NvmeScsi_Destroy(struct NvmeCtrlr *ctrlr);

Nvme_Status NvmeIo_SubmitIo(struct NvmeNsInfo *ns, vmk_ScsiCommand *vmkCmd);
Nvme_Status NvmeIo_SubmitDsm(struct NvmeNsInfo *ns, vmk_ScsiCommand *vmkCmd, struct nvme_dataset_mgmt_data *dsmData, int count);
vmk_ByteCount NvmeIo_ProcessPrps(struct NvmeQueueInfo *qinfo, struct NvmeCmdInfo *cmdInfo);

#if NVME_MUL_COMPL_WORLD
VMK_ReturnStatus Nvme_StartCompletionWorlds(struct NvmeCtrlr *ctrlr);
VMK_ReturnStatus Nvme_EndCompletionWorlds(struct NvmeCtrlr *ctrlr);
void Nvme_IOCOmpletionEnQueue(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd);
#endif

/*TODO: funtions to support error handling in the future*/
//VMK_ReturnStatus NvmeQueue_GetErrorLog(struct NvmeCtrlr *ctrlr, struct NvmeCmdInfo *cmdInfo);
#endif /* _NVME_PRIVATE_H_ */
