/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (c) 2012-2013  Integrated Device Technology, Inc.
 * All rights reserved.
 *******************************************************************************/

#include "vmkapi.h"
#include "oslib.h"
#include "nvme_os.h"
#include "../../common/kernel/nvme_scsi_cmds.h"
#include "../../common/kernel/nvme_private.h"


/**
 * NvmeCore_DisableIntr - refer to the header file
 */
Nvme_Status
NvmeCore_DisableIntr(struct NvmeCtrlr *ctrlr)
{
   struct NvmeQueueInfo *qinfo;
   VMK_ReturnStatus      vmkStatus;
   Nvme_Status           nvmeStatus = NVME_STATUS_SUCCESS;
   int                   i;

   if (ctrlr->ctrlOsResources.msixEnabled) {
      /**
       * Disable interrupt of admin queue
       */
      qinfo = &ctrlr->adminq;
      vmkStatus = vmk_IntrDisable(ctrlr->ctrlOsResources.intrArray[qinfo->intrIndex]);
      if (vmkStatus != VMK_OK) {
         /**
          * This should always return VMK_OK.
          */
         nvmeStatus = NVME_STATUS_FAILURE;
         VMK_ASSERT(0);
      }

      /**
       * Disable interrupts of io queues
       */
      for (i = 0; i < ctrlr->numIoQueues; i++) {
         qinfo = &ctrlr->ioq[i];
         vmkStatus = vmk_IntrDisable(ctrlr->ctrlOsResources.intrArray[qinfo->intrIndex]);
         if (vmkStatus != VMK_OK) {
            /**
             * This should always return VMK_OK.
             */
            nvmeStatus = NVME_STATUS_FAILURE;
            VMK_ASSERT(0);
         }
      }
   }

   WPRINT("XXX: handle non-MSIX cases here.");

   return nvmeStatus;
}

/**
 * NvmeCore_EnableIntr - refer to the header file
 */
Nvme_Status
NvmeCore_EnableIntr(struct NvmeCtrlr *ctrlr)
{
   struct NvmeQueueInfo *qinfo;
   VMK_ReturnStatus      vmkStatus;
   Nvme_Status           nvmeStatus = NVME_STATUS_SUCCESS;
   int                   i;

   if (ctrlr->ctrlOsResources.msixEnabled) {
      /**
       * Disable interrupt of admin queue
       */
      qinfo = &ctrlr->adminq;
      vmkStatus = vmk_IntrEnable(ctrlr->ctrlOsResources.intrArray[qinfo->intrIndex]);
      if (vmkStatus != VMK_OK) {
         /**
          * This should always return VMK_OK.
          */
         nvmeStatus = NVME_STATUS_FAILURE;
         VMK_ASSERT(0);
      }

      /**
       * Disable interrupts of io queues
       */
      for (i = 0; i < ctrlr->numIoQueues; i++) {
         qinfo = &ctrlr->ioq[i];
         vmkStatus = vmk_IntrEnable(ctrlr->ctrlOsResources.intrArray[qinfo->intrIndex]);
         if (vmkStatus != VMK_OK) {
            /**
             * This should always return VMK_OK.
             */
            nvmeStatus = NVME_STATUS_FAILURE;
            VMK_ASSERT(0);
         }
      }
   }

   WPRINT("XXX: handle non-MSIX cases here.");

   return nvmeStatus;
}


/******************************************************************************
 * NVMe Queue Management Routines
 *****************************************************************************/


/**
 * NvmeCore_DisableQueueIntr - refer to the header file.
 */
Nvme_Status
NvmeCore_DisableQueueIntr(struct NvmeQueueInfo *qinfo)
{
   struct NvmeCtrlr *ctrlr = qinfo->ctrlr;
   if (ctrlr->ctrlOsResources.msixEnabled) {
      vmk_IntrDisable(ctrlr->ctrlOsResources.intrArray[qinfo->intrIndex]);
   }

   return NVME_STATUS_SUCCESS;
}


/**
 * NvmeCore_EnableQueueIntr - refer to the header file.
 */
Nvme_Status
NvmeCore_EnableQueueIntr(struct NvmeQueueInfo *qinfo)
{
   struct NvmeCtrlr *ctrlr = qinfo->ctrlr;
   if (ctrlr->ctrlOsResources.msixEnabled) {
      vmk_IntrEnable(ctrlr->ctrlOsResources.intrArray[qinfo->intrIndex]);
   }
   return NVME_STATUS_SUCCESS;
}


void
Nvme_SpinlockLock(void *arg)
{
   vmk_Lock lock  = (vmk_Lock) arg;
   vmk_SpinlockLock(lock);
}

void
Nvme_SpinlockUnlock(void *arg)
{
   vmk_Lock lock = (vmk_Lock) arg;
   vmk_SpinlockUnlock(lock);
}

void
Nvme_GetCpu(void *arg)
{
   VMK_NOT_REACHED();
}

void
Nvme_PutCpu(void *arg)
{
   VMK_NOT_REACHED();
}


/**
 * Acknowledge interrupt
 *
 * @param [in] handlerData queue instance
 * @param [in] intrCookie interrupt cookie
 *
 * @return VMK_OK this interrupt is for us and should schedule interrupt
 *            handler
 */
VMK_ReturnStatus
NvmeQueue_IntrAck(void *handlerData, vmk_IntrCookie intrCookie)
{
   return VMK_OK;
}


/**
 * Interrupt handler
 *
 * Handles interrupts by processing completion queues
 *
 * @param handlerData queue instance
 * @param intrCookie interrupt cookie
 */
void
NvmeQueue_IntrHandler(void *handlerData, vmk_IntrCookie intrCookie)
{
   struct NvmeQueueInfo *qinfo = (struct NvmeQueueInfo *)handlerData;

   LOCK_COMPQ(qinfo);

   #if (NVME_ENABLE_IO_STATS == 1)
      STATS_Increment(qinfo->ctrlr->statsData.TotalInterrupts);
   #endif


   nvmeCoreProcessCq(qinfo, 0);

   UNLOCK_COMPQ(qinfo);
}

/**
 * Request irq handler for a given queue
 *
 * @param [in] qinfo queue instance
 */
VMK_ReturnStatus
NvmeQueue_RequestIrq(struct NvmeQueueInfo *qinfo)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeCtrlr *ctrlr = qinfo->ctrlr;

   if (!ctrlr->ctrlOsResources.msixEnabled) {
      /* Per-queue interrupt is only available for MSIx mode */
      return VMK_BAD_PARAM;
   }
   if (qinfo->intrIndex >= ctrlr->ctrlOsResources.numVectors) {
      /* Invalid interrupt index. */
      return VMK_BAD_PARAM;
   }

   vmkStatus = OsLib_IntrRegister(ctrlr->ctrlOsResources.device,
      ctrlr->ctrlOsResources.intrArray[qinfo->intrIndex],
      qinfo, qinfo->id, NvmeQueue_IntrAck, NvmeQueue_IntrHandler);

   return vmkStatus;
}


/**
 * Free interrupt handler for a given queue
 *
 * @param [in] qinfo queue instance
 */
VMK_ReturnStatus
NvmeQueue_FreeIrq(struct NvmeQueueInfo *qinfo)
{
   struct NvmeCtrlr *ctrlr = qinfo->ctrlr;

   if (!ctrlr->ctrlOsResources.msixEnabled) {
      /* Per-queue interrupt is only available for MSIx mode */
      return VMK_BAD_PARAM;
   }

   if (qinfo->intrIndex >= ctrlr->ctrlOsResources.numVectors) {
      /* Invalid interrupt index. */
      return VMK_BAD_PARAM;
   }

   if (!NvmeCore_IsQueueSuspended(qinfo)) {
      EPRINT("trying to unregister interrupts on an active queue %d.", qinfo->id);
      VMK_ASSERT(0);
      return VMK_BUSY;
   }

   return OsLib_IntrUnregister(ctrlr->ctrlOsResources.intrArray[qinfo->intrIndex], qinfo);
}


/**
 * Create a spinlock with no rank
 *
 */
VMK_ReturnStatus
OsLib_LockCreateNoRank(const char *name, vmk_Lock *lock)
{
   vmk_SpinlockCreateProps props;

   props.moduleID = vmk_ModuleCurrentID;
   props.heapID = NVME_DRIVER_RES_HEAP_ID;
   props.type = VMK_SPINLOCK;
   props.domain = VMK_LOCKDOMAIN_INVALID;
   props.rank = VMK_SPINLOCK_UNRANKED;
   vmk_NameInitialize(&props.name, name);

   return vmk_SpinlockCreate(&props, lock);
}


/**
 * Create a spinlock.
 *
 * All locks created under the same controller share the same lock domain.
 *
 * @param [in] ctrlr controller instance of the lock
 * @param [in] name name of the lock
 * @param [in] rank rank of the lock
 * @param [out] lock pinter to the lock to be created
 */
VMK_ReturnStatus
OsLib_LockCreate(struct NvmeCtrlOsResources* ctrlOsResources, vmk_LockRank rank,
                 const char *name, vmk_Lock *lock)
{
   vmk_SpinlockCreateProps props;

   props.moduleID = vmk_ModuleCurrentID;
   props.heapID = NVME_DRIVER_RES_HEAP_ID;
   props.type = VMK_SPINLOCK;
   props.domain = ctrlOsResources->lockDomain;
   props.rank = rank;
   vmk_NameInitialize(&props.name, name);

   return vmk_SpinlockCreate(&props, lock);
}


/**
 * OsLib_SemaphoreCreate - create a semaphore
 *
 * @param [IN]  name  name of the semaphore to be created
 * @param [IN]  value initial value
 * @param [OUT] sema  pointer to the semaphore to be created
 */
VMK_ReturnStatus
OsLib_SemaphoreCreate(const char *name, int value, vmk_Semaphore *sema)
{
#if VMKAPIDDK_VERSION >= 650
   return vmk_SemaCreate(sema, vmk_ModuleGetHeapID(vmk_ModuleCurrentID), name, value);
#else
   return vmk_SemaCreate(sema, vmk_ModuleCurrentID, name, value);
#endif
}


/**
 * OsLib_SemaphoreDestroy - destroy a semaphore
 *
 * @param [IN]  sema  pointer to the semaphore to be destroyed
 */
VMK_ReturnStatus
OsLib_SemaphoreDestroy(vmk_Semaphore *sema)
{
   vmk_SemaDestroy(sema);
   *sema = NULL;

   return VMK_OK;
}


/**
 * Destroy spinlock
 *
 * @param [in] ctrlr controller instance
 * @param [in] lock lock to be destroyed
 */
VMK_ReturnStatus
OsLib_LockDestroy(vmk_Lock *lock)
{
   vmk_SpinlockDestroy(*lock);
   *lock = VMK_LOCK_INVALID;

   return VMK_OK;
}


/**
 * Allocate physically contiguous dma memory
 *
 * @param [in] ctrlr controller instance
 * @param [in] size size in bytes to be allocated
 * @param [in] timeout for allocating memory 
 * @param [out] dmaEntry used to save intermediate data that is used during dma free
 */
VMK_ReturnStatus
OsLib_DmaAlloc(struct NvmeCtrlOsResources *ctrlOsResources, vmk_ByteCount size,
               struct NvmeDmaEntry *dmaEntry, vmk_uint32 timeout)
{
   VMK_ReturnStatus vmkStatus;
   vmk_MemPoolAllocProps allocProps;
   vmk_MemPoolAllocRequest allocRequest;
   vmk_MapRequest mapRequest;
   vmk_DMAMapErrorInfo err;

   /* always assume bi-direction in current implementation. */
   dmaEntry->direction = VMK_DMA_DIRECTION_BIDIRECTIONAL;
   dmaEntry->size = size;

   /* first, allocate a physically contiguous region of pages */
   allocProps.physContiguity = VMK_MEM_PHYS_CONTIGUOUS;
   allocProps.physRange = VMK_PHYS_ADDR_ANY;
   allocProps.creationTimeoutMS = timeout;

   allocRequest.numPages = VMK_UTIL_ROUNDUP(size, VMK_PAGE_SIZE) >> VMK_PAGE_SHIFT;
   allocRequest.numElements = 1;
   allocRequest.mpnRanges = &dmaEntry->mpnRange;

   vmkStatus = vmk_MemPoolAlloc(NVME_DRIVER_RES_MEMPOOL, &allocProps, &allocRequest);
   if (vmkStatus != VMK_OK) {
      DPRINT_CTRLR("failed to allocate pages from mem pool, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   /* now, we need to map the pages to virtual addresses */
   mapRequest.mapType = VMK_MAPTYPE_DEFAULT;
   mapRequest.mapAttrs = VMK_MAPATTRS_READWRITE;
   mapRequest.numElements = 1;
   mapRequest.mpnRanges = &dmaEntry->mpnRange;
   mapRequest.reservation = NULL;

   vmkStatus = vmk_Map(vmk_ModuleCurrentID, &mapRequest, &dmaEntry->va);
   if (vmkStatus != VMK_OK) {
      DPRINT_CTRLR("failed to map pages, 0x%x.", vmkStatus);
      goto free_pages;
   }

   /* lastly, need to map machine addresses throught IOMMU */
   vmkStatus = vmk_SgAllocWithInit(ctrlOsResources->sgHandle, &dmaEntry->sgIn, (void *)dmaEntry->va, size);
   if (vmkStatus != VMK_OK) {
      DPRINT_CTRLR("failed to alloc sg array, 0x%x.", vmkStatus);
      goto unmap;
   }

   VMK_ASSERT(dmaEntry->sgIn->numElems == 1);

   vmkStatus = vmk_DMAMapSg(ctrlOsResources->dmaEngine, dmaEntry->direction,
      ctrlOsResources->sgHandle, dmaEntry->sgIn, &dmaEntry->sgOut, &err);
   if (vmkStatus != VMK_OK) {
      DPRINT_CTRLR("failed to map sg array, %s, 0x%x.",
         vmk_DMAMapErrorReasonToString(err.reason),
         vmkStatus);
      goto free_sg;
   }

   dmaEntry->ioa = dmaEntry->sgOut->elem[0].ioAddr;

   return VMK_OK;

free_sg:
   vmk_SgFree(ctrlOsResources->sgHandle, dmaEntry->sgIn);
   dmaEntry->sgIn = NULL;

unmap:
   vmk_Unmap(dmaEntry->va);
   dmaEntry->va = 0;

free_pages:
   /* allocRequest should hold the data for free already */
   vmk_MemPoolFree(&allocRequest);

   return vmkStatus;
}


/**
 * Free dma memory
 *
 * @param [in] ctrlr controller instance
 * @param [in] dmaEntry dma entry to be freed
 */
VMK_ReturnStatus
OsLib_DmaFree(struct NvmeCtrlOsResources *ctrlOsResources, struct NvmeDmaEntry *dmaEntry)
{
   VMK_ReturnStatus vmkStatus;
   vmk_MemPoolAllocRequest allocRequest;
   int errors = 0;

   vmkStatus = vmk_DMAUnmapSg(ctrlOsResources->dmaEngine, dmaEntry->direction,
      ctrlOsResources->sgHandle, dmaEntry->sgOut);
   if (vmkStatus != VMK_OK) {
      DPRINT_CTRLR("failed to unmap sg array, 0x%x.", vmkStatus);
      errors ++;
   }
   dmaEntry->sgOut = NULL;

   vmkStatus = vmk_SgFree(ctrlOsResources->sgHandle, dmaEntry->sgIn);
   if (vmkStatus != VMK_OK) {
      DPRINT_CTRLR("failed to free sg array, 0x%x.", vmkStatus);
      errors ++;
   }
   dmaEntry->sgIn = NULL;

   vmk_Unmap(dmaEntry->va);

   allocRequest.numPages = VMK_UTIL_ROUNDUP(dmaEntry->size, VMK_PAGE_SIZE) >> VMK_PAGE_SHIFT;
   allocRequest.numElements = 1;
   allocRequest.mpnRanges = &dmaEntry->mpnRange;

   vmkStatus = vmk_MemPoolFree(&allocRequest);
   if (vmkStatus != VMK_OK) {
      DPRINT_CTRLR("failed to free mem pages, 0x%x.", vmkStatus);
      errors ++;
   }

   if (!errors) {
      return VMK_OK;
   } else {
      return VMK_FAILURE;
   }
}


/**
 * Register a interrupt handler
 *
 * @param [in] intrCookie interrupt cookie
 * @param [in] handlerData handler data to be passed into interrupt handler
 * @param [in] idx MSI-X interrupt id, 0 for INTx based interrupts
 * @param [in] intrAck interruptAcknowledge callback function
 * @param [in] intrHandler intrHandler callback function
 */
VMK_ReturnStatus
OsLib_IntrRegister(vmk_Device device, vmk_IntrCookie intrCookie,
                   void *handlerData, int idx,
                   vmk_IntrAcknowledge intrAck, vmk_IntrHandler intrHandler)
{
   VMK_ReturnStatus vmkStatus;
   vmk_IntrProps props;

   props.device = device;
   props.acknowledgeInterrupt = intrAck;
   props.handler = intrHandler;
   props.handlerData = handlerData;
   props.attrs = 0;
   vmk_NameFormat(&props.deviceName, "nvmeIntr-%d", idx);

   vmkStatus = vmk_IntrRegister(vmk_ModuleCurrentID,
      intrCookie, &props);

   return vmkStatus;
}


/**
 * Unregister interrupt handler
 *
 * @param [in] intrCookie interrupt cookie
 * @param [in] handlerData handler data to be passed into interrupt handler
 */
VMK_ReturnStatus
OsLib_IntrUnregister(vmk_IntrCookie intrCookie, void *handlerData)
{
   return vmk_IntrUnregister(vmk_ModuleCurrentID, intrCookie, handlerData);
}

/**
 * Initialize driver's DMA engine and Scatter-Gather Handle
 *
 * This DMA engine is for allocating DMA buffers for submission/completion
 * queues, etc., which is suitable for allocating large physically contiguous
 * buffers. IOs should use a separate DMA engine which has more constraints
 * than this engine.
 *
 * @param [in] ctrlr controller instance
 */
VMK_ReturnStatus
OsLib_DmaInit(struct NvmeCtrlOsResources *ctrlOsResources)
{
   VMK_ReturnStatus vmkStatus;
   vmk_DMAEngineProps props;
   vmk_DMAConstraints constraints;

   /* Create dma engine first */
   constraints.addressMask = NVME_DRIVER_PROPS_CTRLR_DMAENGINE_ADDRMASK;
   constraints.maxTransfer = NVME_DRIVER_PROPS_CTRLR_DMAENGINE_MAXXFER;
   constraints.sgMaxEntries = NVME_DRIVER_PROPS_CTRLR_DMAENGINE_SGMAXENTRIES;
   constraints.sgElemMaxSize = NVME_DRIVER_PROPS_CTRLR_DMAENGINE_SGELEMMAXSIZE;
   constraints.sgElemSizeMult = NVME_DRIVER_PROPS_CTRLR_DMAENGINE_SGELEMSIZEMULT;
   constraints.sgElemAlignment = NVME_DRIVER_PROPS_CTRLR_DMAENGINE_SGELEMALIGN;
   constraints.sgElemStraddle = NVME_DRIVER_PROPS_CTRLR_DMAENGINE_SGELEMSTRADDLE;

   props.module = vmk_ModuleCurrentID;
   props.flags = 0;
   props.device = ctrlOsResources->device;
   props.bounce = NULL;
   props.constraints = &constraints;
   vmk_NameInitialize(&props.name, NVME_DRIVER_PROPS_CTRLR_DMAENGINE_NAME);

   vmkStatus = vmk_DMAEngineCreate(&props, &ctrlOsResources->dmaEngine);
   if (vmkStatus != VMK_OK) {
      EPRINT("unable to create dma engine, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   /* Create SG handle */
   vmkStatus = vmk_SgCreateOpsHandle(NVME_DRIVER_RES_HEAP_ID,
      &ctrlOsResources->sgHandle,
      NULL, /* custom ops */
      NULL /* private data */
      );
   if (vmkStatus != VMK_OK) {
      EPRINT("unable to create sg ops handle, 0x%x.", vmkStatus);
      goto destroy_dmaengine;
   }

   return VMK_OK;

destroy_dmaengine:
   vmk_DMAEngineDestroy(ctrlOsResources->dmaEngine);
   ctrlOsResources->dmaEngine = VMK_DMA_ENGINE_INVALID;

   return vmkStatus;
}


/**
 * Cleanup dma engine and SG handle
 *
 * @param [in] ctrlr controller instance
 */
VMK_ReturnStatus
OsLib_DmaCleanup(struct NvmeCtrlOsResources *ctrlOsResources)
{
   vmk_SgDestroyOpsHandle(ctrlOsResources->sgHandle);
   ctrlOsResources->sgHandle = NULL;

   vmk_DMAEngineDestroy(ctrlOsResources->dmaEngine);
   ctrlOsResources->dmaEngine = VMK_DMA_ENGINE_INVALID;

   return VMK_OK;
}


/**
 * Create lock domain.
 *
 * @param [in] ctrlr controller instance
 */
VMK_ReturnStatus
OsLib_LockDomainCreate(struct NvmeCtrlOsResources *ctrlOsResources, const char* ctrlName)
{
   vmk_Name name;

   vmk_NameFormat(&name, "nvmeLockDom-%s", ctrlName);
   return vmk_LockDomainCreate(vmk_ModuleCurrentID, NVME_DRIVER_RES_HEAP_ID,
      &name, &ctrlOsResources->lockDomain);
}


/**
 * Destroy lock domain
 *
 * @param [in] ctrlr controller instance
 */
VMK_ReturnStatus
OsLib_LockDomainDestroy(struct NvmeCtrlOsResources *ctrlOsResources)
{
   vmk_LockDomainDestroy(ctrlOsResources->lockDomain);
   ctrlOsResources->lockDomain = VMK_LOCKDOMAIN_INVALID;

   return VMK_OK;
}


/**
 * interruptAcknowledge callback for INTx mode
 *
 * @param [in] handlerData controller instance of type struct NvmeCtrlr
 * @param [in] intrCookie interrupt cookie
 */
VMK_ReturnStatus
NvmeCtrlr_IntxAck(void *handlerData, vmk_IntrCookie intrCookie)
{
   DPRINT_CTRLR("intr acked for cookie: 0x%x.", intrCookie);
   return VMK_OK;
}


/**
 * intrHandler callback for INTx mode
 * @param [in] handlerData controller instance of type struct NvmeCtrlr
 * @param [in] intrCookie interrupt cookie
 */
void
NvmeCtrlr_IntxHandler(void *handlerData, vmk_IntrCookie intrCookie)
{
   DPRINT_CTRLR("intr handled for cookie: 0x%x.", intrCookie);
}

VMK_ReturnStatus ScsiCommand(void *clientData, vmk_ScsiCommand *vmkCmd, void *deviceData)
{
    return scsiProcessCommand(clientData, vmkCmd, deviceData);
}

/**
 * SCSI DMA Engine constraints
 */
#define SCSI_ADDR_MASK (VMK_ADDRESS_MASK_64BIT)
#define SCSI_MAX_XFER (NVME_DRIVER_PROPS_MAX_PRP_LIST_ENTRIES * VMK_PAGE_SIZE)
#define SCSI_SG_MAX_ENTRIES (NVME_DRIVER_PROPS_MAX_PRP_LIST_ENTRIES)
#define SCSI_SG_ELEM_MAX_SIZE (0)
#define SCSI_SG_ELEM_SIZE_MULT (512)
/**
 * NVMe spec requires that the first PRP entry (DMA address of the first SG
 * element) to have last two bits as '0'.
 */
#define SCSI_SG_ELEM_ALIGNMENT (4)
#define SCSI_SG_ELEM_STRADDLE (VMK_ADDRESS_MASK_32BIT + 1)

static void ScsiDumpIntrHandler(void *clientData, vmk_IntrCookie intrCookie)
{
    return;
}

/**
 * Initialize SCSI layer
 *
 * @param [in] ctrlr Handle to the controller instance
 *
 * @return VMK_OK if SCSI layer initialization completes successfully
 */
VMK_ReturnStatus
NvmeScsi_Init(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   vmk_ScsiAdapter *adapter;
   vmk_DMAConstraints scsiConstraints;
   vmk_DMAEngineProps scsiProps;

   DPRINT_TEMP("enter");

   /**
    * According to spec, "One entry in each queue is not available for use
    * due to Head and Tail entry pointer definition". So each queue should
    * report a queue depth of (queue size -1) to PSA to avoid QFULL issue.
    *
    * TODO: Enhance the queue depth reporting considering command splitting
    *       and queue imbalance.
    */
   ctrlr->qDepth = (ctrlr->ioSubQueueSize - 1) * ctrlr->numIoQueues;

   /* Create a DMA engine for SCSI IO */
   scsiConstraints.addressMask = SCSI_ADDR_MASK;
   scsiConstraints.maxTransfer = ctrlr->maxXferLen;
   scsiConstraints.sgMaxEntries = SCSI_SG_MAX_ENTRIES;
   scsiConstraints.sgElemMaxSize = SCSI_SG_ELEM_MAX_SIZE;
   scsiConstraints.sgElemSizeMult = SCSI_SG_ELEM_SIZE_MULT;
   scsiConstraints.sgElemAlignment = SCSI_SG_ELEM_ALIGNMENT;
   scsiConstraints.sgElemStraddle = SCSI_SG_ELEM_STRADDLE;

   /* Override some of the parameters */
   scsiConstraints.sgMaxEntries = max_prp_list;

   vmk_NameFormat(&scsiProps.name, "%s-scsiDmaEngine", Nvme_GetCtrlrName(ctrlr));
   scsiProps.module = vmk_ModuleCurrentID;
   scsiProps.flags = 0;
   scsiProps.device = ctrlr->ctrlOsResources.device;
   scsiProps.constraints = &scsiConstraints;
   scsiProps.bounce = NULL;

   vmkStatus = vmk_DMAEngineCreate(&scsiProps, &ctrlr->ctrlOsResources.scsiDmaEngine);
   if (vmkStatus != VMK_OK) {
      return vmkStatus;
   }

   /* Now allocate and initialize scsi adapter */
   adapter = vmk_ScsiAllocateAdapter();
   if (!adapter) {
      EPRINT("failed to allocate scsi adapter, out of memory.");
      vmk_DMAEngineDestroy(ctrlr->ctrlOsResources.scsiDmaEngine);
      return VMK_NO_MEMORY;
   }

   vmk_NameInitialize(&adapter->driverName, NVME_DRIVER_NAME);

   adapter->device = ctrlr->ctrlOsResources.device;
   adapter->hostMaxSectors = ctrlr->maxXferLen / VMK_SECTOR_SIZE;
   adapter->qDepthPtr = &ctrlr->qDepth;

   adapter->command = ScsiCommand;
   adapter->taskMgmt = ScsiTaskMgmt;
   adapter->dumpCommand = ScsiDumpCommand;
   adapter->procInfo = ScsiProcInfo;
   adapter->dumpQueue = ScsiDumpQueue;
   adapter->dumpPollHandler = ScsiDumpPollHandler;
   adapter->dumpPollHandlerData = ctrlr;
   adapter->ioctl = ScsiIoctl;
   adapter->discover = ScsiDiscover;
   adapter->modifyDeviceQueueDepth = ScsiModifyDeviceQueueDepth;
   adapter->queryDeviceQueueDepth = ScsiQueryDeviceQueueDepth;
   adapter->checkTarget = ScsiCheckTarget;

   adapter->moduleID = vmk_ModuleCurrentID;
   adapter->clientData = ctrlr;
   adapter->channels = 1;
   adapter->maxTargets = 1;
   adapter->targetId = -1;
   adapter->maxLUNs = ctrlr->nn;
   adapter->paeCapable = VMK_TRUE;
   adapter->maxCmdLen = NVME_DRIVER_PROPS_MAX_CMD_LEN;

   adapter->flags = VMK_SCSI_ADAPTER_FLAG_NO_PERIODIC_SCAN;

#if ((NVME_MUL_COMPL_WORLD) && (VMKAPIDDK_VERSION >= 600))
   vmkStatus = vmk_ScsiAdapterSetCapabilities(adapter,
         VMK_SCSI_ADAPTER_CAP_DRIVER_COMPL_WORLDS);
   /* Stall driver loading if fail to set capabilities. */
   if (vmkStatus != VMK_OK) {
      EPRINT("Fail to set capacity of multiple completion worlds.\n");
      vmk_DMAEngineDestroy(ctrlr->ctrlOsResources.scsiDmaEngine);
      vmk_ScsiFreeAdapter(adapter);
      return vmkStatus;
   }
#endif

#if ((NVME_PROTECTION) && (VMKAPIDDK_VERSION >= 600))
   vmk_ScsiProtTypes protMask= 0;
   struct NvmeNsInfo *ns;
   vmk_ListLinks     *itemPtr;
   itemPtr = vmk_ListFirst(&ctrlr->nsList);
   ns = VMK_LIST_ENTRY(itemPtr, struct NvmeNsInfo, list);
   if (END2END_CAP_TYPE(ns->dataProtCap) & END2END_CAP_TYPE1) {
      protMask |= VMK_SCSI_TYPE1_PROT | VMK_SCSI_DIX_TYPE1_PROT;
   }
   if (END2END_CAP_TYPE(ns->dataProtCap) & END2END_CAP_TYPE3) {
      protMask |= VMK_SCSI_TYPE3_PROT | VMK_SCSI_DIX_TYPE3_PROT;
   }
   /*
    * Currently driver only support protection data as seperate buffer. 
    */
   if ((ns->metaDataCap & 0x2) == 0) {
      protMask = 0;
   }

   if (protMask) {
      vmkStatus = vmk_ScsiAdapterSetCapabilities(adapter,VMK_SCSI_ADAPTER_CAP_DATA_INTEGRITY);
      if (vmkStatus != VMK_OK) {
         EPRINT("Fail to set capacity of data integrity.");
         vmk_DMAEngineDestroy(ctrlr->ctrlOsResources.scsiDmaEngine);
         vmk_ScsiFreeAdapter(adapter);
         return vmkStatus;
      }
      vmk_ScsiAdapterSetProtMask(adapter, protMask);
      vmk_ScsiAdapterSetSupportedGuardTypes(adapter, VMK_SCSI_GUARD_CRC); 
   }
#endif

   /* TODO: create NVMe transport */
   adapter->mgmtAdapter.transport = VMK_STORAGE_ADAPTER_PSCSI;

   adapter->notifyIOAllowed = ScsiNotifyIOAllowed;
   adapter->engine = ctrlr->ctrlOsResources.scsiDmaEngine;

   ctrlr->ctrlOsResources.scsiAdapter = adapter;
   /* adapterName is "Invalid" since the adapter has not been registered by PSA. */
   vmk_NameCopy(&ctrlr->adapterName, &adapter->name);
   DPRINT_CTRLR("adpterName: %s", vmk_NameToString(&ctrlr->adapterName));

   vmk_ScsiRegisterIRQ(adapter,
         ctrlr->ctrlOsResources.intrArray[0],
         (vmk_IntrHandler)ScsiDumpIntrHandler,
         ctrlr);

   return VMK_OK;
}


/**
 * Tear down and free SCSI layer resources
 *
 * @param [in] ctrlr Handle to the controller instance
 *
 * @return VMK_OK if SCSI layer cleanup succeed
 */
VMK_ReturnStatus
NvmeScsi_Destroy(struct NvmeCtrlr *ctrlr)
{
   DPRINT_TEMP("enter");

   vmk_ScsiFreeAdapter(ctrlr->ctrlOsResources.scsiAdapter);
   ctrlr->ctrlOsResources.scsiAdapter = NULL;

   vmk_DMAEngineDestroy(ctrlr->ctrlOsResources.scsiDmaEngine);

   return VMK_OK;
}

void OsLib_StrToUpper(char *str, int length)
{
   int i;

   for (i = 0; i < length; i++) {
      if (str[i] >= 'a' && str[i] <= 'z') {
         str[i] -= ('a' - 'A');
      }
   }
}

#if NVME_MUL_COMPL_WORLD
/**
 * Do commands completion in a local list
 */
static void
DoLocalCmdCompl(struct NvmeCtrlr *ctrlr, vmk_SList *localComplCmds)
{
   vmk_ScsiCommand *vmkCmd;
   NvmeIoRequest *IORequest;

   VMK_ASSERT(!vmk_SListIsEmpty(localComplCmds));
   while (!vmk_SListIsEmpty(localComplCmds)) {

      vmk_SList_Links *IOEventList = vmk_SListFirst(localComplCmds);
      IORequest = VMK_LIST_ENTRY(IOEventList, NvmeIoRequest, link);
      vmkCmd = IORequest->vmkCmd;
      VMK_ASSERT(vmkCmd->done);

      vmk_SListRemove(localComplCmds, IOEventList, NULL);

      /*
       * Remember that preallocated IORequest frames are destroyed along with
       * the owning SCSI commands. We must not touch the IORequest after the
       * command is done with.
       */
      vmkCmd->done(vmkCmd);

#if VMKAPIDDK_VERSION < 650
      vmk_SlabFree(ctrlr->complWorldsSlabID, IORequest);
#endif

      /* Yield the CPU to avoid CPU heartbeat NMI PSOD's. See PR #1451047 */
      vmk_WorldYield();
   }
}

/**
 * @brief  This function do commands completion.
 * Driver IO Completion Worlds is controller based.
 *
 * @param[in] IOCompletionQueue which is owned by dedicated completion world
 *
 * @return VMK_OK
 */
VMK_ReturnStatus OsLib_CompletionWorld(void *data)
{
   VMK_ReturnStatus status = VMK_OK;
   NvmeIoCompletionQueue  *IOCompletionQueue = (NvmeIoCompletionQueue *)data;
   struct NvmeCtrlr *ctrlr = IOCompletionQueue->ctrlr;
   vmk_SList localComplCmds;

   status = vmk_SpinlockLock(IOCompletionQueue->lock);
   VMK_ASSERT(status == VMK_OK);
   /*
    * Handle IO compeltion requests if any. Or else goto sleep
    * until new request arriving
    */
   vmk_SListInit(&localComplCmds);
   while (!ctrlr->shuttingDown) {
      if (vmk_SListIsEmpty(&IOCompletionQueue->complList)) {
         status = vmk_WorldWait((vmk_WorldEventID)IOCompletionQueue,   \
               IOCompletionQueue->lock,                                \
               VMK_TIMEOUT_UNLIMITED_MS,                               \
               "NVMe I/O Completion Queue: no work to do");
         if ((status != VMK_OK) && (!ctrlr->shuttingDown)) {
            EPRINT("In %s: vmk_WorldWait failed with status <%s>",   \
                   __func__, vmk_StatusToString(status));
            VMK_ASSERT(VMK_FALSE);
         }
      }
      else {
         /*
          * There are new pending requests. Copy all of them into
          * a local list and complete them.
          */
         vmk_SListSplitHead(&IOCompletionQueue->complList, &localComplCmds,
               vmk_SListLast(&IOCompletionQueue->complList));
         vmk_SpinlockUnlock(IOCompletionQueue->lock);
         DoLocalCmdCompl(ctrlr, &localComplCmds);
      }
      VMK_ASSERT(vmk_SListIsEmpty(&localComplCmds));
      status = vmk_SpinlockLock(IOCompletionQueue->lock);
   }

   vmk_SpinlockUnlock(IOCompletionQueue->lock);
   vmk_WorldExit(VMK_OK);
   return VMK_OK;
}

/**
 * Flush completion queue
 */
VMK_ReturnStatus
OsLib_FlushCompletionQueue(struct NvmeCtrlr *ctrlr,    \
      NvmeIoCompletionQueue *IOCompletionQueue)
{
   vmk_SList localComplCmds;
   VMK_ReturnStatus status = VMK_OK;

   status = vmk_SpinlockLock(IOCompletionQueue->lock);
   /* No pending request */
   if (vmk_SListIsEmpty(&IOCompletionQueue->complList)) {
      vmk_SpinlockUnlock(IOCompletionQueue->lock);
      return VMK_OK;
   }

   /*
    * There are new pending requests. Copy all of them into
    * a local list and complete them.
    */
   vmk_SListInit(&localComplCmds);
   vmk_SListSplitHead(&IOCompletionQueue->complList, &localComplCmds,
         vmk_SListLast(&IOCompletionQueue->complList));
   vmk_SpinlockUnlock(IOCompletionQueue->lock);
   DoLocalCmdCompl(ctrlr, &localComplCmds);
   VMK_ASSERT(vmk_SListIsEmpty(&localComplCmds));

   return VMK_OK;
}

#if VMKAPIDDK_VERSION < 650
/**
 * Create a slab for commands completion
 */
static VMK_ReturnStatus
CreateIoCompletionSlab(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;
   vmk_SlabCreateProps compl_worlds_slab_props;
   vmk_Name slabName;

   vmk_Memset(&compl_worlds_slab_props, 0, sizeof(vmk_SlabCreateProps));

   /* Creating slab */
   compl_worlds_slab_props.type = VMK_SLAB_TYPE_SIMPLE;
   vmk_NameFormat(&slabName, "nvme_compl_io_slab_%s", Nvme_GetCtrlrName(ctrlr));
   vmk_NameInitialize(&compl_worlds_slab_props.name, (const char *)&slabName);
   compl_worlds_slab_props.module = vmk_ModuleCurrentID;
   compl_worlds_slab_props.objSize = sizeof(NvmeIoRequest);
   compl_worlds_slab_props.alignment = VMK_L1_CACHELINE_SIZE;
   /* TODO: double check this value */
   compl_worlds_slab_props.ctrlOffset = 0;
   compl_worlds_slab_props.minObj = ctrlr->ioCompQueueSize * nvme_compl_worlds_num / 2;
   compl_worlds_slab_props.maxObj = ctrlr->ioCompQueueSize * nvme_compl_worlds_num;

   vmkStatus = vmk_SlabCreate(&compl_worlds_slab_props,    \
         &ctrlr->complWorldsSlabID);
   if (vmkStatus != VMK_OK) {
      EPRINT("Unable to create slab. vmkStatus: 0x%x.", vmkStatus);
      return vmkStatus;
   }
   return vmkStatus;
}
#endif

static void DestroyComplWorldLocks(struct NvmeCtrlr*  ctrlr, vmk_int32 numLocks)
{
   vmk_int32 lockNum;
   for (lockNum=numLocks-1; lockNum>=0; lockNum--) {
      OsLib_LockDestroy(&ctrlr->IOCompletionQueue[lockNum].lock);
   }
}

void DestroyComplWorldWorlds(struct NvmeCtrlr*  ctrlr, vmk_int32 numWorlds)
{
   vmk_int32 worldNum;
   for (worldNum=numWorlds-1; worldNum>=0; worldNum++) {
         vmk_WorldDestroy(ctrlr->IOCompletionQueue[worldNum].worldID);
   }
}

/**
 * Create multiple completion worlds
 */
VMK_ReturnStatus
OsLib_StartCompletionWorlds(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus        status = VMK_OK;
   vmk_WorldProps          worldProps;
   char propName[VMK_MISC_NAME_MAX];
   NvmeIoCompletionQueue    *IOCompletionQueue;
   vmk_uint32 qID=0;
   vmk_int32 lockNum=0;

#if VMKAPIDDK_VERSION < 650
   status = CreateIoCompletionSlab(ctrlr);
   if (status != VMK_OK) {
      VMK_ASSERT(VMK_FALSE);
      return status;
   }
#endif

   ctrlr->shuttingDown = VMK_FALSE;
   ctrlr->numComplWorlds = ctrlr->numIoQueues;

#if VMKAPIDDK_VERSION >= 650
   /*
    * For the affinity mask ideally we should use a number of completion worlds
    * that is power of 2, since modulo operations are expensive for each IO.
    * Determine the affinity mask for the current number of numComplWorlds.
    */

   ctrlr->affinityMask = 0;
   while (ctrlr->affinityMask < ctrlr->numComplWorlds) {
      ctrlr->affinityMask = (ctrlr->affinityMask << 1) | 1;
   }
   ctrlr->affinityMask = ctrlr->affinityMask ? ctrlr->affinityMask >> 1 : 0;
#endif

   for(lockNum=0; lockNum < ctrlr->numComplWorlds; lockNum++) {
      IOCompletionQueue = &(ctrlr->IOCompletionQueue[lockNum]);
      IOCompletionQueue->ctrlr = ctrlr;
      vmk_SListInit(&(IOCompletionQueue->complList));

      /* Create a completion queue lock */
      vmk_StringFormat(propName, VMK_MISC_NAME_MAX, NULL,   \
            "nvmeComplQLock-%s-%d", Nvme_GetCtrlrName(ctrlr), lockNum);

      status = OsLib_LockCreate(&ctrlr->ctrlOsResources, NVME_LOCK_RANK_ULTRA,
        propName, &IOCompletionQueue->lock);
      if (status != VMK_OK) {
         VMK_ASSERT(VMK_FALSE);
         goto cleanup;
      }
   }


   for(qID=0; qID < ctrlr->numComplWorlds; qID++) {
      IOCompletionQueue = &(ctrlr->IOCompletionQueue[qID]);
      IOCompletionQueue->ctrlr = ctrlr;

      /* create a new IO completion world */
      vmk_StringFormat(propName, VMK_MISC_NAME_MAX, NULL,   \
            "NVMeComplWorld-%s-%d", Nvme_GetCtrlrName(ctrlr), qID);
      worldProps.name = propName;
      worldProps.moduleID = vmk_ModuleCurrentID;
      worldProps.startFunction = OsLib_CompletionWorld;
      worldProps.data = IOCompletionQueue;
      worldProps.schedClass = VMK_WORLD_SCHED_CLASS_QUICK;
#if (VMKAPIDDK_VERSION >= 600)
      worldProps.heapID = NVME_DRIVER_RES_HEAP_ID;
#endif
      status = vmk_WorldCreate(&worldProps, &IOCompletionQueue->worldID);
      if (status != VMK_OK)
      {
         EPRINT("%s: Failed to create world <%s>", __func__,   \
               vmk_StatusToString(status));
         VMK_ASSERT(VMK_FALSE);
         goto cleanup;
      }
   }

   return VMK_OK;

cleanup:
   DestroyComplWorldLocks(ctrlr,lockNum);
   DestroyComplWorldWorlds(ctrlr,qID);
#if VMKAPIDDK_VERSION < 650
   vmk_SlabDestroy(ctrlr->complWorldsSlabID);
#endif
   return status;

}

/**
 * terminate multiple completion worlds and clean up resource
 */
VMK_ReturnStatus
OsLib_EndCompletionWorlds(struct NvmeCtrlr *ctrlr)
{
   NvmeIoCompletionQueue    *IOCompletionQueue;
   vmk_uint32 i=0;

   ctrlr->shuttingDown = VMK_TRUE;
   for(i=0; i<ctrlr->numComplWorlds; i++) {
      IOCompletionQueue = &ctrlr->IOCompletionQueue[i];
      vmk_WorldWakeup((vmk_WorldEventID) IOCompletionQueue);
      vmk_WorldWaitForDeath(IOCompletionQueue->worldID);
      OsLib_FlushCompletionQueue(ctrlr, IOCompletionQueue);
      OsLib_LockDestroy(&IOCompletionQueue->lock);
   }
#if VMKAPIDDK_VERSION < 650
   /* A single slab for all of completion worlds */
   vmk_SlabDestroy(ctrlr->complWorldsSlabID);
#endif
   ctrlr->numComplWorlds = 0;

#if VMKAPIDDK_VERSION >= 650
   ctrlr->useQueueAffinityHint = VMK_FALSE;
   ctrlr->affinityMask = 0;
#endif

   return VMK_OK;
}

/**
 * Enqueue IO completion request.
 */
void OsLib_IOCompletionEnQueue(struct NvmeCtrlr *ctrlr,
                               vmk_ScsiCommand *vmkCmd)
{
   VMK_ReturnStatus status;
   NvmeIoCompletionQueue *IOCompletionQueue;
   NvmeIoRequest *IORequest;
   vmk_Bool needWakeup = VMK_FALSE;
   vmk_uint32 qID;

   qID = OsLib_GetQueue(ctrlr, vmkCmd);
   VMK_ASSERT(qID < ctrlr->numIoQueues);
   IOCompletionQueue = &(ctrlr->IOCompletionQueue[qID]);
   VMK_ASSERT(IOCompletionQueue);

#if VMKAPIDDK_VERSION >= 650
   /*
    * PSA provides a small amount of preallocated memory per SCSI command,
    * which can be used (for any purposes) by device drivers. The address
    * of said memory block (which is guaranteed to be cacheline-aligned)
    * can be obtained by calling vmk_ScsiCmdGetDriverFrame(). The size of
    * the block (which is constant but may change between VMKAPI versions)
    * can be obtained by calling vmk_ScsiCmdGetDriverFrameSize(). Note
    * that querying the memory block address does not allocate anything;
    * as such, the call always succeeds (so there's no need to check if
    * the returned address is NULL and handle out-of-memory conditions).
    * There's also no need for deallocation upon command completion.
    *
    * Using preallocated frames is fast and convenient; however, the
    * amount of preallocated memory is limited. Drivers that choose to
    * use preallocated frames MUST call vmk_ScsiCmdGetDriverFrameSize()
    * to ensure that the preallocated frame is large enough for its
    * intended use (see the assert in init_module() in nvme_module.c).
    * If more memory for per-command data is needed, the driver must
    * employ traditional allocation methods (that is, allocating from
    * private heaps or slabs).
    */
   IORequest = vmk_ScsiCmdGetDriverFrame(vmkCmd);
#else
   IORequest = vmk_SlabAlloc(ctrlr->complWorldsSlabID);
   /* complete command immediately if out of memory */
   if (!IORequest) {
      VPRINT("Failed to allocate memory.   \
            Fallback to PSA default completion handler.");
      vmk_ScsiSchedCommandCompletion(vmkCmd);
      return;
   }
#endif

   IORequest->vmkCmd = vmkCmd;
   status = vmk_SpinlockLock(IOCompletionQueue->lock);
   VMK_ASSERT(status == VMK_OK);
   if (vmk_SListIsEmpty(&IOCompletionQueue->complList)) {
      needWakeup = VMK_TRUE;
   }
   vmk_SListInsertAtTail(&IOCompletionQueue->complList, &IORequest->link);
   vmk_SpinlockUnlock(IOCompletionQueue->lock);

   if(needWakeup) {
      vmk_WorldWakeup((vmk_WorldEventID) IOCompletionQueue);
   }
}

/**
 * Bind interrupt to compeltion world corresponding to a given queue to make sure
 * the interrupt delivered to the same PCPU with the completion world running
 *
 * @param [in] qinfo queue instance
 */
VMK_ReturnStatus
NvmeQueue_BindCompletionWorld(struct NvmeQueueInfo *qinfo)
{
   vmk_WorldID worldID;
   vmk_IntrCookie intrCookie;
   struct NvmeCtrlr *ctrlr = qinfo->ctrlr;

   if (!ctrlr->ctrlOsResources.msixEnabled) {
      /* Per-queue interrupt is only available for MSIx mode */
      return VMK_BAD_PARAM;
   }
   if (qinfo->intrIndex >= ctrlr->ctrlOsResources.numVectors) {
      /* Invalid interrupt index. */
      return VMK_BAD_PARAM;
   }

   worldID = ctrlr->IOCompletionQueue[qinfo->id-1].worldID;
   intrCookie = ctrlr->ctrlOsResources.intrArray[qinfo->intrIndex];

   return vmk_WorldInterruptSet(worldID, intrCookie);
}

/**
 * Unbind interrupt to compeltion world corresponding to a given queue.
 *
 * @param [in] qinfo queue instance
 */
VMK_ReturnStatus
NvmeQueue_UnbindCompletionWorld(struct NvmeQueueInfo *qinfo)
{
   vmk_WorldID worldID;
   vmk_IntrCookie intrCookie;
   struct NvmeCtrlr *ctrlr = qinfo->ctrlr;

   if (!ctrlr->ctrlOsResources.msixEnabled) {
      /* Per-queue interrupt is only available for MSIx mode */
      return VMK_BAD_PARAM;
   }
   if (qinfo->intrIndex >= ctrlr->ctrlOsResources.numVectors) {
      /* Invalid interrupt index. */
      return VMK_BAD_PARAM;
   }

   worldID = ctrlr->IOCompletionQueue[qinfo->id-1].worldID;
   intrCookie = ctrlr->ctrlOsResources.intrArray[qinfo->intrIndex];

   return vmk_WorldInterruptUnset(worldID, intrCookie);
}

#endif // end of NVME_MUL_COMPL_WORLD

/**
 * Callback to notify when IO is allowed to adapter.
 *
 * @param [in]  logicalDevice Handle to the logical device.
 * @param [in]  ioAllowed VMK_TRUE if IO is allowed, VMK_FALSE if IO not allowed.
 */
void
ScsiNotifyIOAllowed(vmk_Device logicalDevice, vmk_Bool ioAllowed)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;
   vmk_ScsiAdapter *adapter;
   struct NvmeCtrlr *ctrlr;

   DPRINT_TEMP("entry, ioAllowed %d.", ioAllowed);

   vmkStatus = vmk_DeviceGetRegistrationData(logicalDevice, (vmk_AddrCookie *)&adapter);
   if (vmkStatus != VMK_OK || adapter == NULL) {
      EPRINT("failed to get logical device data, 0x%x.", vmkStatus);
      return;
   }
   ctrlr = (struct NvmeCtrlr *)adapter->clientData;

   if (ioAllowed) {
/* skip PSA completion queue creation if driver already created completion
 * worlds */
#if ( !defined(NVME_MUL_COMPL_WORLD) || (!NVME_MUL_COMPL_WORLD) )
      vmkStatus = vmk_ScsiStartCompletionQueues(adapter, ctrlr->numIoQueues);
      if (vmkStatus == VMK_OK) {
         IPRINT("started %d io queues.", ctrlr->numIoQueues);
      } else {
         EPRINT("failed to start %d io queues, 0x%x.", ctrlr->numIoQueues, vmkStatus);
      }
#endif

      NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_OPERATIONAL);
      vmk_NameCopy(&ctrlr->adapterName, &adapter->name);
      DPRINT_CTRLR("adpterName: %s", vmk_NameToString(&ctrlr->adapterName));

#if NVME_DEBUG_INJECT_STATE_DELAYS
      IPRINT("--STARTED to OPERATIONAL--");
      vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   } else {
#if ALLOW_IOS_IN_QUIESCED_STATE == 0
/*
 * When this workaround switch is active, do not disallow IOs prior to
 * QuiesceDevice being invoked.
 */
      NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_STARTED);
#endif

#if NVME_DEBUG_INJECT_STATE_DELAYS
      IPRINT("--OPERATIONAL to STARTED--");
      vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   }

   return;
}

inline VMK_ReturnStatus
OsLib_SetPathLostByDevice(struct NvmeCtrlr *ctrlr)
{
    return vmk_ScsiSetPathLostByDevice(&ctrlr->adapterName, 0, 0, -1);
}

VMK_ReturnStatus
OsLib_SetupExceptionHandler (struct NvmeCtrlr * ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   vmk_WorldProps worldProps;
   char lockName[VMK_MISC_NAME_MAX];
   char worldName[VMK_MISC_NAME_MAX];

   vmkStatus = VMK_OK;

   VPRINT("Starting exception handler...");

   vmk_StringFormat (lockName, VMK_MISC_NAME_MAX, NULL, "nvme_eh_lock--%s",
         Nvme_GetCtrlrName (ctrlr));

   vmkStatus = OsLib_LockCreate (&ctrlr->ctrlOsResources,
         NVME_LOCK_RANK_MEDIUM, lockName,
         &ctrlr->exceptionLock);
   if (vmkStatus != VMK_OK)
   {
      Nvme_LogWarning ("Can't create exception handler lock\n");
      goto err_out;
   }


   vmk_StringFormat (worldName, VMK_MISC_NAME_MAX, NULL, "nvme_eh--%s",
         Nvme_GetCtrlrName (ctrlr));



   worldProps.name = worldName;
   worldProps.moduleID = vmk_ModuleCurrentID;
   worldProps.startFunction =
      (vmk_WorldStartFunc) NvmeExc_ExceptionHandlerTask;
   worldProps.data = ctrlr;
   worldProps.schedClass = VMK_WORLD_SCHED_CLASS_QUICK;
#if VMKAPIDDK_VERSION >= 600
   worldProps.heapID = NVME_DRIVER_RES_HEAP_ID;
#endif
   vmkStatus = vmk_WorldCreate (&worldProps, &(ctrlr->exceptionHandlerTask));

   if (vmkStatus != VMK_OK)
   {
      Nvme_LogWarning ("Can't create exception handler world\n");
      goto destroy_lock;
   }
   ctrlr->exceptionThreadStarted = VMK_TRUE;
   return VMK_OK;

destroy_lock:
   vmkStatus = OsLib_LockDestroy (&ctrlr->exceptionLock);
err_out:
   return vmkStatus;
}

void
OsLib_ShutdownExceptionHandler (struct NvmeCtrlr *ctrlr)
{

   VMK_ReturnStatus vmkStatus;
   if (ctrlr->exceptionThreadStarted != VMK_TRUE)
      return;

   vmkStatus =
      NvmeExc_SignalExceptionAndWait (ctrlr, NVME_EXCEPTION_TASK_SHUTDOWN,
            15000);
   /*
    *  Shutdown should have gracefully terminated the exception handler task, but
    *  just to be sure....
    */
   VPRINT ("Killing exception handler task\n");
   vmk_WorldDestroy (ctrlr->exceptionHandlerTask);
   vmk_WorldWaitForDeath (ctrlr->exceptionHandlerTask);
   vmkStatus = OsLib_LockDestroy (&ctrlr->exceptionLock);
}

/* round robin for all of completion worlds */
vmk_uint32
OsLib_GetQueue(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd)
{
#if NVME_MUL_COMPL_WORLD
   static int qID=0;
#if VMKAPIDDK_VERSION >= 650
   vmk_uint16 affinityHint = vmk_ScsiCmdGetAffinityHint(vmkCmd);
   if (ctrlr->useQueueAffinityHint) {
      return affinityHint & ctrlr->affinityMask;

   } else if (affinityHint) {
      /*
       * Commands carry affinity hints. Do not use round robin in that case
       * and switch to the queue ID based on affinity hint starting with
       * IOs following this one.
       */
       ctrlr->useQueueAffinityHint = VMK_TRUE;
   }
#endif
   qID = (qID+1)%(ctrlr->numComplWorlds);
   return qID;
#else
   return vmk_ScsiCommandGetCompletionQueue(ctrlr->ctrlOsResources.scsiAdapter, vmkCmd);
#endif
}

vmk_uint32
OsLib_GetMaxNumQueues(void)
{
#if NVME_MUL_COMPL_WORLD
   return nvme_compl_worlds_num;
#else
   vmk_uint32 maxQ = vmk_ScsiGetMaxNumCompletionQueues();
   return (maxQ > NVME_MAX_COMPL_WORLDS) ? NVME_MAX_COMPL_WORLDS : maxQ;
#endif
}

static vmk_atomic64 NumPCPUs = 0;

VMK_ReturnStatus
NVMeStorConstructor(vmk_PCPUID pcpu, void *object, vmk_ByteCountSmall size,  \
      vmk_AddrCookie arg) {
   vmk_AtomicInc64(&NumPCPUs);
   return VMK_OK;
}

/* Get PCPU number by counting constuctor calling number */
vmk_uint32
Oslib_GetPCPUNum(void) {
   vmk_PCPUStorageProps props;
   vmk_PCPUStorageHandle handle = VMK_PCPU_STORAGE_HANDLE_INVALID;

   props.type = VMK_PCPU_STORAGE_TYPE_WRITE_LOCAL;
   props.moduleID = vmk_ModuleCurrentID;
   vmk_NameInitialize(&props.name, "NVMePerPCPUStor");
   props.constructor = NVMeStorConstructor;
   props.destructor = NULL;
   props.size = 4;
   props.align = 0;

   vmk_AtomicWrite64(&NumPCPUs, 0);
   vmk_PCPUStorageCreate(&props, &handle);

   if (VMK_UNLIKELY(handle == VMK_PCPU_STORAGE_HANDLE_INVALID)) {
      /* failed to get memory, so our CPU number can't be trusted. */
      VMK_ASSERT(VMK_FALSE);
      return -1;
   }

   vmk_PCPUStorageDestroy(handle);

   return NumPCPUs;
}

#if USE_TIMER
void OsLib_TimeoutHandler(vmk_TimerCookie cookie)
{
   struct NvmeCtrlr* ctrlr = (struct NvmeCtrlr*) cookie.ptr;
   NvmeExc_SignalException(ctrlr, NVME_EXCEPTION_TASK_TIMER);
}

VMK_ReturnStatus OsLib_TimerQueueDestroy(struct NvmeCtrlr *ctrlr)
{
   DPRINT_TEMP ("enter.");
   if (ctrlr->TimerQueue != VMK_INVALID_TIMER_QUEUE)
   {
      /* Destroy the timer queue. */
      vmk_TimerQueueDestroy(ctrlr->TimerQueue);
   }
   return VMK_OK;

}

VMK_ReturnStatus OsLib_TimerQueueCreate(struct NvmeCtrlr *ctrlr)
{
   vmk_TimerQueueProps timerQueueProps;
   VMK_ReturnStatus vmkStatus;
   vmk_Name timerQueueName;

   DPRINT_TEMP ("enter.");

   timerQueueProps.moduleID = vmk_ModuleCurrentID;
   timerQueueProps.heapID   = NVME_DRIVER_RES_HEAP_ID;
   timerQueueProps.attribs  = VMK_TIMER_QUEUE_ATTR_LOW_LATENCY;
   vmk_NameFormat(&timerQueueName, "nvme_timer_queue_%s", Nvme_GetCtrlrName(ctrlr));
   vmk_NameInitialize(&timerQueueProps.name, (const char *)&timerQueueName);

   vmkStatus = vmk_TimerQueueCreate(&timerQueueProps, &ctrlr->TimerQueue);
   if (vmkStatus != VMK_OK)
   {
      EPRINT("Cannot create timer queue, iostats and timeout checks will NOT be possible!");
      ctrlr->TimerQueue = VMK_INVALID_TIMER_QUEUE;
   }

   return vmkStatus;
}
void OsLib_StartIoTimeoutCheckTimer(struct NvmeCtrlr *ctrlr)
{
   // Initialize and start timer.
   ctrlr->TimerCookie.ptr  = ctrlr;
   ctrlr->TimerAttr        = VMK_TIMER_ATTR_PERIODIC;


   if (ctrlr->TimerQueue == VMK_INVALID_TIMER_QUEUE) {
      EPRINT("Timer Queue is invalid for %s", Nvme_GetCtrlrName(ctrlr));
      return;
   }

   if (vmk_TimerSchedule(ctrlr->TimerQueue, OsLib_TimeoutHandler, ctrlr->TimerCookie, NVME_TIMER_TIMEOUT_TICK, VMK_TIMER_DEFAULT_TOLERANCE,
            ctrlr->TimerAttr, ctrlr->ctrlOsResources.lockDomain, NVME_LOCK_RANK_LOW, &ctrlr->TimeoutTimerObj) != VMK_OK) {
      EPRINT("Timeout scanning timer failed to start for device %s!", Nvme_GetCtrlrName(ctrlr));
   }
}

void OsLib_StopIoTimeoutCheckTimer(struct NvmeCtrlr* ctrlr)
{
   if (ctrlr->TimeoutTimerObj != VMK_INVALID_TIMER) {
      // Wait for timer, if it is active.
      if (vmk_TimerCancel(ctrlr->TimeoutTimerObj, VMK_TRUE) == VMK_OK) {
         ctrlr->TimeoutTimerObj = VMK_INVALID_TIMER;
      }
   }
}
#endif

