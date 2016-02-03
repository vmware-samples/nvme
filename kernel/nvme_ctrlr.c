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
 * @file: nvme_ctrlr.c --
 *
 *    Nvme controller related stuff
 */

#include "nvme_private.h"
#include "../common/nvme_mgmt.h"
#include "nvme_debug.h"

static void
NvmeCtrlr_FlushAdminQueue(struct NvmeCtrlr *ctrlr);
static void
NvmeCtrlr_SuspendAdminQueue(struct NvmeCtrlr *ctrlr);
static void
NvmeCtrlr_ResumeAdminQueue(struct NvmeCtrlr *ctrlr);
static void
NvmeCtrlr_ResetAdminQueue(struct NvmeCtrlr *ctrlr);

/**
 * Initialize pci layer resources for a controller
 *
 * @param [in] ctrlr controller instance
 */
static VMK_ReturnStatus
PciInit(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   int bar;
   vmk_PCIResource pciRes[VMK_PCI_NUM_BARS];

   /* First, get pci device handle and id information for reference */
   vmkStatus = vmk_DeviceGetRegistrationData(ctrlr->device,
      (vmk_AddrCookie *)&ctrlr->pciDevice);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("invalid pci device, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   vmkStatus = vmk_PCIQueryDeviceID(ctrlr->pciDevice, &ctrlr->pciId);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("unable to get device id, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   vmkStatus = vmk_PCIQueryDeviceAddr(ctrlr->pciDevice, &ctrlr->sbdf);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("unable to get device address, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   /* Select and map pci bars */
   vmkStatus = vmk_PCIQueryIOResources(ctrlr->pciDevice, VMK_PCI_NUM_BARS, pciRes);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("unable to query io resource, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   for (bar = 0; bar < VMK_PCI_NUM_BARS; bar++) {
      if (((pciRes[bar].flags & VMK_PCI_BAR_FLAGS_IO) == 0) &&
         (pciRes[bar].size > 4096)) {
         Nvme_LogInfo("selected bar %d.", bar);
         ctrlr->bar = bar;
         ctrlr->barSize = pciRes[bar].size;
         break;
      }
   }
   if (bar == VMK_PCI_NUM_BARS) {
      Nvme_LogError("unable to find valid bar.");
      return VMK_NO_RESOURCES;
   }

   vmkStatus = vmk_PCIMapIOResource(vmk_ModuleCurrentID,
      ctrlr->pciDevice, ctrlr->bar, &ctrlr->pciResv, &ctrlr->regs);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("unable to map pci bar %d, 0x%x", ctrlr->bar, vmkStatus);
      return vmkStatus;
   }


   /* Generate a unique name for the controller */
   vmk_NameFormat(&ctrlr->name, "nvme%02d%02d%02d%02d",
      ctrlr->sbdf.seg, ctrlr->sbdf.bus, ctrlr->sbdf.dev, ctrlr->sbdf.fn);

   /* Everything at PCI layer should have been initialized. */
   return VMK_OK;
}


/**
 * Undo all resource allocations done by PciInit.
 *
 * @param [in] ctrlr controller instance
 */
static VMK_ReturnStatus
PciCleanup(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;

   vmkStatus = vmk_PCIUnmapIOResource(vmk_ModuleCurrentID, ctrlr->pciDevice,
      ctrlr->bar);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("unable to unmap pci io resource, 0x%x.", vmkStatus);
      /* Need to fall through */
   }

   ctrlr->regs = 0L;
   ctrlr->bar = VMK_PCI_NUM_BARS;  /* This should indicate an invalid bar */

   return VMK_OK;
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
static VMK_ReturnStatus
DmaInit(struct NvmeCtrlr *ctrlr)
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
   props.device = ctrlr->device;
   props.bounce = NULL;
   props.constraints = &constraints;
   vmk_NameInitialize(&props.name, NVME_DRIVER_PROPS_CTRLR_DMAENGINE_NAME);

   vmkStatus = vmk_DMAEngineCreate(&props, &ctrlr->dmaEngine);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("unable to create dma engine, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   /* Create SG handle */
   vmkStatus = vmk_SgCreateOpsHandle(NVME_DRIVER_RES_HEAP_ID,
      &ctrlr->sgHandle,
      NULL, /* custom ops */
      NULL /* private data */
      );
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("unable to create sg ops handle, 0x%x.", vmkStatus);
      goto destroy_dmaengine;
   }

   return VMK_OK;

destroy_dmaengine:
   vmk_DMAEngineDestroy(ctrlr->dmaEngine);
   ctrlr->dmaEngine = VMK_DMA_ENGINE_INVALID;

   return vmkStatus;
}


/**
 * Cleanup dma engine and SG handle
 *
 * @param [in] ctrlr controller instance
 */
static VMK_ReturnStatus
DmaCleanup(struct NvmeCtrlr *ctrlr)
{
   vmk_SgDestroyOpsHandle(ctrlr->sgHandle);
   ctrlr->sgHandle = NULL;

   vmk_DMAEngineDestroy(ctrlr->dmaEngine);
   ctrlr->dmaEngine = VMK_DMA_ENGINE_INVALID;

   return VMK_OK;
}


/**
 * interruptAcknowledge callback for INTx mode
 *
 * @param [in] handlerData controller instance of type struct NvmeCtrlr
 * @param [in] intrCookie interrupt cookie
 */
static VMK_ReturnStatus
IntxAck(void *handlerData, vmk_IntrCookie intrCookie)
{
   Nvme_LogDebug("intr acked for cookie: 0x%x.", intrCookie);
   return VMK_OK;
}


/**
 * intrHandler callback for INTx mode
 * @param [in] handlerData controller instance of type struct NvmeCtrlr
 * @param [in] intrCookie interrupt cookie
 */
static void
IntxHandler(void *handlerData, vmk_IntrCookie intrCookie)
{
   Nvme_LogDebug("intr handled for cookie: 0x%x.", intrCookie);
}


/**
 * Allocate and setup MSI-X interrupt handlers
 *
 * @param [in] ctrlr controller instance
 */
static VMK_ReturnStatus
MsixSetup(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   vmk_uint32 numQueues = 0;
   vmk_uint32 numAllocated;

   /* num io queues is determined by how many completion queues SCSI layer supports */
   /* plus 1 for admin queue */
   numQueues = vmk_ScsiGetMaxNumCompletionQueues() + 1;

   ctrlr->intrArray = Nvme_Alloc(sizeof(vmk_IntrCookie) * numQueues, 0, NVME_ALLOC_ZEROED);
   if (ctrlr->intrArray == NULL) {
      ctrlr->msixEnabled = 0;
      return VMK_NO_MEMORY;
   }

   vmkStatus = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
      ctrlr->pciDevice, VMK_PCI_INTERRUPT_TYPE_MSIX,
      numQueues, /* num desired */
      2, /* num required, 1 for admin and 1 for io */
      NULL, /* index array, not needed */
      ctrlr->intrArray,
      &numAllocated);

   if (vmkStatus == VMK_OK) {
      Nvme_LogVerb("Allocated %d msi-x vectors.", numAllocated);
      ctrlr->numIoQueues = numAllocated - 1; /* minus 1 admin q */
      ctrlr->numVectors = numAllocated;
      ctrlr->msixEnabled = 1;

      return VMK_OK;
   } else {
      ctrlr->msixEnabled = 0;
      return vmkStatus;
   }
}


/**
 * Setup INTx mode interrupt handler
 *
 * @param [in] ctrlr controller instance
 */
static VMK_ReturnStatus
IntxSetup(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   vmk_uint32 numAllocated;

   ctrlr->intrArray = Nvme_Alloc(sizeof(vmk_IntrCookie), 0, NVME_ALLOC_ZEROED);
   if (ctrlr->intrArray == NULL) {
      return VMK_NO_MEMORY;
   }

   vmkStatus = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
      ctrlr->pciDevice, VMK_PCI_INTERRUPT_TYPE_LEGACY,
      1, 1, NULL, ctrlr->intrArray, &numAllocated);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("unable to allocate intr cookie, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   /* should have just 1 intr cookie allocated for intx */
   VMK_ASSERT(numAllocated == 1);

   ctrlr->msixEnabled = 0;
   ctrlr->numIoQueues = 1;
   ctrlr->numVectors = 1; /* 1 intx for both admin and io */

   /* for intx mode, we should register intr handler here rather than
    * at individual queue creation time.
    */
   vmkStatus = OsLib_IntrRegister(ctrlr->device, ctrlr->intrArray[0],
      ctrlr, /* for intx handler, the data is the controller itself */
      0, /* use default id 0 */
      IntxAck, IntxHandler);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("unable to register intr handler, 0x%x.", vmkStatus);
      goto free_intr;
   }

   return VMK_OK;

free_intr:
   vmk_PCIFreeIntrCookie(vmk_ModuleCurrentID, ctrlr->pciDevice);
   ctrlr->numIoQueues = 0;
   ctrlr->numVectors = 0;

   return vmkStatus;
}


/**
 * Initialize interrupt handler.
 *
 * We will first try MSI-X, if MSI-X allocation is not successful, then
 * fallback to legacy intx.
 *
 * If MSI-X is used, the actual interrupt handler is NOT registered, until
 * qpair construct time.
 *
 * @param [in] ctrlr controller instance
 */
static VMK_ReturnStatus
IntrInit(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;

   /* try msi-x first, if nvme_force_intx is not set. */
   if (!nvme_force_intx) {
      vmkStatus = MsixSetup(ctrlr);
      if (vmkStatus == VMK_OK) {
         Nvme_LogInfo("using msi-x with %d vectors.", ctrlr->numVectors);
         return VMK_OK;
      }
   }

   /* msi-x setup failed, fallback to intx */
   vmkStatus = IntxSetup(ctrlr);
   if (vmkStatus == VMK_OK) {
      Nvme_LogInfo("using intx.");
      return VMK_OK;
   }

   Nvme_LogError("unable to initialize interrupt, 0x%x.", vmkStatus);
   return vmkStatus;
}


/**
 * Cleanup [in] interrupt resources
 */
static VMK_ReturnStatus
IntrCleanup(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;

   /* if using intx, needs to unregister intr handler first. */
   if (!ctrlr->msixEnabled) {
      vmkStatus = OsLib_IntrUnregister(*ctrlr->intrArray, ctrlr);
      Nvme_LogDebug("unregistered intr handler for intx, 0x%x.", vmkStatus);
   }

   vmkStatus = vmk_PCIFreeIntrCookie(vmk_ModuleCurrentID, ctrlr->pciDevice);
   Nvme_LogDebug("freed intr cookies, 0x%x.", vmkStatus);

   /* lastly, free the intr cookie array */
   Nvme_Free(ctrlr->intrArray);
   ctrlr->intrArray = NULL;
   ctrlr->msixEnabled = 0;
   ctrlr->numVectors = 0;

   return VMK_OK;
}


/**
 * Create lock domain.
 *
 * @param [in] ctrlr controller instance
 */
static VMK_ReturnStatus
LockDomainCreate(struct NvmeCtrlr *ctrlr)
{
   vmk_Name name;

   vmk_NameFormat(&name, "nvmeLockDom-%s", Nvme_GetCtrlrName(ctrlr));
   return vmk_LockDomainCreate(vmk_ModuleCurrentID, NVME_DRIVER_RES_HEAP_ID,
      &name, &ctrlr->lockDomain);
}


/**
 * Destroy lock domain
 *
 * @param [in] ctrlr controller instance
 */
static VMK_ReturnStatus
LockDomainDestroy(struct NvmeCtrlr *ctrlr)
{
   vmk_LockDomainDestroy(ctrlr->lockDomain);
   ctrlr->lockDomain = VMK_LOCKDOMAIN_INVALID;

   return VMK_OK;
}


/**
 * This function validate device parameters.
 *
 * Device parameters may be overwritten prior to driver
 * initialization. We need to validate these changes to make
 * sure they are within operational range of controller
 * capability and driver limitations.
 * Any parameters outside of its supported range are reported
 * and corrected accordingly.
 *
 * @param [in] ctrlr pointer to nvme device context
 *
 * @return VMK_OK if successful
 * @return VMK_BAD_PARAM if parameter is invalid
 */
static VMK_ReturnStatus
NvmeCtrlr_ValidateParams(struct NvmeCtrlr *ctrlr)
{
   vmk_uint64 minPage, maxPage, hwMaxQs, hwCap;

   hwCap = Nvme_Readq(ctrlr->regs + NVME_CAP);
   hwMaxQs = (hwCap & NVME_CAP_MQES_MSK64) + 1;

   Nvme_LogDebug("Controller Capability reg: %016lx", hwCap);

   /* Validate completion and submission queue size */
   if (hwMaxQs && ((io_cpl_queue_size > hwMaxQs) ||
      (io_sub_queue_size > hwMaxQs))) {
      Nvme_LogError("Parameter: maximum HW queue size %lu", hwMaxQs);
      Nvme_LogError("Adapting Hardware suggested queue size.");
      if (io_cpl_queue_size > hwMaxQs) {
         io_cpl_queue_size = hwMaxQs;
      }
      if (io_sub_queue_size > hwMaxQs) {
         io_sub_queue_size = hwMaxQs;
      }
   }

   /*
    * Validate number of command IDs to context size (16 bits).
    * Limit number of command issued to number of command IDs available.
    */
   if (io_command_id_size > 65535) {
      io_command_id_size = 65535;
       Nvme_LogError("Adjusting io_command_id_size to %d", io_command_id_size);
   }

   if (max_io_request > io_command_id_size) {
       max_io_request = io_command_id_size;
       Nvme_LogError("Adjusting max_io_request to %d", io_command_id_size);
   }

   minPage = (1 << (((hwCap & NVME_CAP_MPSMIN_MSK64) >>
            NVME_CAP_MPSMIN_LSB) + 12));
   maxPage = (1 << (((hwCap & NVME_CAP_MPSMAX_MSK64) >>
            NVME_CAP_MPSMAX_LSB) + 12));
   Nvme_LogDebug("hardware maximum page size %lu", maxPage);
   Nvme_LogDebug("hardware minimum page size %lu", minPage);

   if ((maxPage < VMK_PAGE_SIZE) || (minPage > VMK_PAGE_SIZE)) {
       Nvme_LogError("Controller does not support OS default Page size %u",
                     VMK_PAGE_SIZE);
       return VMK_BAD_PARAM;
   }

   max_prp_list = (transfer_size * 1024) / VMK_PAGE_SIZE;
   Nvme_LogDebug("Max xfer %d, Max PRP %d", transfer_size, max_prp_list);

   return VMK_OK;
}


/**
 * Setup admin queue.
 *
 * This only allocates resources for admin q, but doesn't set AQA, ASQ, and ACQ.
 *
 * @param [in] ctrlr controller instance
 */
static VMK_ReturnStatus
AdminQueueSetup(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeQueueInfo *qinfo;

   qinfo = &ctrlr->adminq;
   qinfo->ctrlr = ctrlr;

   vmkStatus = NvmeQueue_Construct(qinfo, admin_sub_queue_size,
      admin_cpl_queue_size, 0, VMK_TRUE, 0);
   if (vmkStatus != VMK_OK) {
      return vmkStatus;
   }

   /**
    * Since queue is initialized to SUSPEND state, resume the queue here to
    * make the queue up and running.
    */
   NvmeCore_ResumeQueue(qinfo);

   Nvme_LogDebug("Admin queue constructed, %p.", qinfo);

   return VMK_OK;
}


/**
 * Destroy/free resources for admin q.
 *
 * This should be called with admin queue deconfigured in the controller, i.e.
 * clear AQA, ASQ, and ACQ.
 *
 * @param [in] ctrlr controller instance
 */
static VMK_ReturnStatus
AdminQueueDestroy(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeQueueInfo *qinfo;

   qinfo = &ctrlr->adminq;

   NvmeCore_SuspendQueue(qinfo, 0);

   /* Flush and reset Admin Queue in case there are still cmds in situation of hot plug. */
   NvmeCtrlr_FlushAdminQueue(ctrlr);
   NvmeCtrlr_ResetAdminQueue(ctrlr);
   vmkStatus = NvmeQueue_Destroy(qinfo);

   return vmkStatus;
}


/**
 * @brief This function constructs all IO queues.
 *    This function allocates IO queue info, and assigns
 *    vector and queue ID to each queue sequentially.
 *    a. Construct queue memory and dma resources.
 *    b. Construct command information (done with queue construct).
 *    c. assign and attach IRQ vector (done with queue construct).
 *    d. register completion and submission queues with firmware.
 *
 * @param[in] ctrlr pointer to nvme device context
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
static VMK_ReturnStatus
NvmeCtrlr_CreateIoQueues(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   vmk_uint32 nrIoQueues = ctrlr->numIoQueues;
   struct NvmeQueueInfo *qinfo;
   struct NvmeSubQueueInfo *sqInfo;
   int shared, i, intrIndex, allocated;

   if (!nrIoQueues || (ctrlr->msixEnabled && nrIoQueues >= ctrlr->numVectors)) {
      Nvme_LogError("nrIoQueues: %d, numVectors: %d.", nrIoQueues, ctrlr->numVectors);
      VMK_ASSERT(0);
      return VMK_BAD_PARAM;
   }

   /* Note: always create shared IO queues for now */
   /* TODO: modify this behavior to allow non-shared IO queues */
   shared = 1;

   ctrlr->ioq = Nvme_Alloc(sizeof(struct NvmeQueueInfo) * nrIoQueues,
      0, NVME_ALLOC_ZEROED);
   if (!ctrlr->ioq) {
      return VMK_NO_MEMORY;
   }

   for (i = 1, allocated = 0; i <= nrIoQueues; i++, allocated ++) {
      intrIndex = ctrlr->msixEnabled ? i : 0;

      ctrlr->ioq[i-1].ctrlr = ctrlr;
      vmkStatus = NvmeQueue_Construct(&ctrlr->ioq[i - 1], /* IO queue starts from index 1 */
         io_sub_queue_size, io_cpl_queue_size, i, shared, intrIndex);
      if (vmkStatus != VMK_OK) {
         goto free_queues;
      }

      qinfo = &ctrlr->ioq[i - 1];
      sqInfo = qinfo->subQueue;

      Nvme_LogDebug("IO queue [%d] %p, Comp DB 0x%lx, Sub DB 0x%lx, vector: %ds",
         qinfo->id, qinfo,
         qinfo->doorbell, sqInfo->doorbell, qinfo->intrIndex);

      vmkStatus = NvmeCtrlrCmd_CreateCq(ctrlr, qinfo, i);
      if (vmkStatus != VMK_OK) {
         /* Need to destroy queue before bailing out */
         NvmeQueue_Destroy(qinfo);
         goto free_queues;
      }

      vmkStatus = NvmeCtrlrCmd_CreateSq(ctrlr, qinfo, i);
      if (vmkStatus != VMK_OK) {
         /* Need to destroy queue here befor bailing out */
         NvmeCtrlrCmd_DeleteCq(ctrlr, i);
         NvmeQueue_Destroy(qinfo);
         goto free_queues;
      }

      NvmeCore_ResumeQueue(qinfo);
   }

   return VMK_OK;

free_queues:
   /*
    * Queue [0, allocated) should have already been constructed if we reach here,
    * need to destroy those queues now.
    */
   while (--allocated >= 0) {
      NvmeCtrlrCmd_DeleteSq(ctrlr, allocated);
      NvmeCtrlrCmd_DeleteCq(ctrlr, allocated);
      NvmeCore_SuspendQueue(&ctrlr->ioq[allocated], 0);
      NvmeQueue_Destroy(&ctrlr->ioq[allocated]);
   }

   Nvme_Free(ctrlr->ioq);
   ctrlr->ioq = NULL;

   return vmkStatus;
}


/**
 * @brief This function deconstructs all IO queues.
 *    This function releases all IO queue info, and releases
 *    vector IDs and queue IDs.
 *    a. delete hardware completion and submission queues.
 *    b. Releas and detach IRQ vector (done with queue destruct).
 *    c. release command information (done with queue destruct).
 *    d. release queue memory and dma resources.
 *
 *
 * @param[in] ctrlr pointer to nvme device context
 *
 * @note ctrlr->lock must be held.
 *
 * @return VMK_OK.
 */
static VMK_ReturnStatus
NvmeCtrlr_DeleteIoQueues(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeQueueInfo *qinfo;
   struct NvmeSubQueueInfo *sqInfo;
   int i;

   for (i = 1; i <= ctrlr->numIoQueues; i++) {
      qinfo = &ctrlr->ioq[i - 1];
      sqInfo = qinfo->subQueue;

      if (!NvmeCore_IsQueueSuspended(qinfo)) {
         Nvme_LogError("trying to delete active queue %d.", qinfo->id);
         VMK_ASSERT(0);
         continue;
      }

      /*
       * We skip the destroy of hardware I/O queues if the controller is
       * already offline or failed.
       */
      if (NvmeState_GetCtrlrState(ctrlr, VMK_FALSE) != NVME_CTRLR_STATE_FAILED &&
          NvmeState_GetCtrlrState(ctrlr, VMK_FALSE) != NVME_CTRLR_STATE_QUIESCED &&
          NvmeState_GetCtrlrState(ctrlr, VMK_FALSE) != NVME_CTRLR_STATE_MISSING) {
         vmkStatus = NvmeCtrlrCmd_DeleteSq(ctrlr, sqInfo->id);
         Nvme_LogDebug("Destroyed sq %d, 0x%x.", sqInfo->id, vmkStatus);
         vmkStatus = NvmeCtrlrCmd_DeleteCq(ctrlr, qinfo->id);
         Nvme_LogDebug("Destroyed cq %d, 0x%x.", qinfo->id, vmkStatus);
      }

      NvmeCore_SuspendQueue(qinfo, 0);
      vmkStatus = NvmeQueue_Destroy(qinfo);
      Nvme_LogDebug("Destroyed queue %d, 0x%x.", qinfo->id, vmkStatus);
   }

   /* Finally free the queue pools we have created */
   Nvme_Free(ctrlr->ioq);
   ctrlr->ioq = NULL;
   ctrlr->numIoQueues = 0;

   return VMK_OK;
}


/**
 * Attach and bring up controller
 *
 * Allocate controller related resources
 *
 * @param [in] ctrlr controller instance
 */
VMK_ReturnStatus
NvmeCtrlr_Attach(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   char lockName[VMK_MISC_NAME_MAX];

   /**
    * Set initial state.
    *
    * Note: lock is not initialized by here, so do not use locking.
    */
   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_INIT, VMK_FALSE);

   /**
    * Initialize PCI resources first to access controller bars.
    *
    * Note: has to initialize PCI resource first, all the following operations
    *       require BARs to be mapped already.
    */
   vmkStatus = PciInit(ctrlr);
   if (vmkStatus != VMK_OK) {
      return vmkStatus;
   }

   vmkStatus = NvmeCtrlr_ValidateParams(ctrlr);
   if (vmkStatus != VMK_OK) {
      goto cleanup_pci;
   }

   /* Initialize DMA facilities (dma engine, sg handle, etc.) */
   vmkStatus = DmaInit(ctrlr);
   if (vmkStatus != VMK_OK) {
      goto cleanup_pci;
   }

   /* Initialize interrupt */
   vmkStatus = IntrInit(ctrlr);
   if (vmkStatus != VMK_OK) {
      goto cleanup_dma;
   }

   /* Initialize lock domain for locks within this controller */
   vmkStatus = LockDomainCreate(ctrlr);
   if (vmkStatus != VMK_OK) {
      goto cleanup_intr;
   }

   /* Initialize lock */
   vmk_StringFormat(lockName, sizeof(lockName), NULL, "%s-lock",
                    Nvme_GetCtrlrName(ctrlr));
   vmkStatus = OsLib_LockCreate(ctrlr->lockDomain, NVME_LOCK_RANK_LOW,
      lockName, &ctrlr->lock);
   if (vmkStatus != VMK_OK) {
      goto cleanup_lockdomain;
   }

   /* Initialize task management mutex */
   vmk_StringFormat(lockName, sizeof(lockName), NULL, "%s-mutex",
                    Nvme_GetCtrlrName(ctrlr));
   vmkStatus = OsLib_SemaphoreCreate(lockName, 1, &ctrlr->taskMgmtMutex);
   if (vmkStatus != VMK_OK) {
      goto cleanup_lock;
   }

   /*
    * TODO: Initialize and kick off timers and kernel thread
    */
#if 0
   vmkStatus = NvmeCtrlr_CreateLogWorld(ctrlr);
   if (vmkStatus != VMK_OK) {
      goto cleanup_intr;
   }
#endif

   vmkStatus = AdminQueueSetup(ctrlr);
   if (vmkStatus != VMK_OK) {
      goto cleanup_sema;
   }

   /**
    * Initialize all other essential members
    */
   vmk_ListInit(&ctrlr->nsList);

   return VMK_OK;

cleanup_sema:
   OsLib_SemaphoreDestroy(&ctrlr->taskMgmtMutex);

cleanup_lock:
   OsLib_LockDestroy(&ctrlr->lock);

cleanup_lockdomain:
   LockDomainDestroy(ctrlr);

cleanup_intr:
   IntrCleanup(ctrlr);

cleanup_dma:
   DmaCleanup(ctrlr);

cleanup_pci:
   PciCleanup(ctrlr);

   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_FAILED, VMK_FALSE);

   Nvme_LogDebug("failed to attach controller, 0x%x.", vmkStatus);

   return vmkStatus;
}


/**
 * NvmeCtrlr_Detach - tear down controller.
 *
 * @param [in] ctrlr controller instance
 */
VMK_ReturnStatus
NvmeCtrlr_Detach(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;

   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_DETACHED, VMK_TRUE);

   vmkStatus = AdminQueueDestroy(ctrlr);
   Nvme_LogDebug("cleaned admin queue, 0x%x.", vmkStatus);

   vmkStatus = OsLib_SemaphoreDestroy(&ctrlr->taskMgmtMutex);
   Nvme_LogDebug("cleaned task management mutex, 0x%x.", vmkStatus);

   vmkStatus = OsLib_LockDestroy(&ctrlr->lock);
   Nvme_LogDebug("cleaned up lock, 0x%x.", vmkStatus);

   vmkStatus = LockDomainDestroy(ctrlr);
   Nvme_LogDebug("cleaned up lock domain, 0x%x.", vmkStatus);

   vmkStatus = IntrCleanup(ctrlr);
   Nvme_LogDebug("cleaned up intr, 0x%x.", vmkStatus);

   vmkStatus = DmaCleanup(ctrlr);
   Nvme_LogDebug("cleaned up dma, 0x%x.", vmkStatus);

   vmkStatus = PciCleanup(ctrlr);
   Nvme_LogDebug("cleaned up pci, 0x%x.", vmkStatus);

   return VMK_OK;
}


/**
 * @brief This function sets up admin queue parameters and resets controller
 *    start controller operation:
 *    1 - Setup Admin queue parameters.
 *    2 - reset controller
 *    3 - Wait controller READY state.
 *
 * @param[in] ctrlr pointer to nvme device context
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 *
 */
static VMK_ReturnStatus
NvmeCtrlr_HwStart(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeQueueInfo *qinfo;
   struct NvmeSubQueueInfo *sqInfo;
   vmk_uint32 aqa, config;
   vmk_uint64 hwCap;
   vmk_IOA regs;

   qinfo = &ctrlr->adminq;
   sqInfo = qinfo->subQueue;
   regs = ctrlr->regs;

   hwCap = Nvme_Readq(regs + NVME_CAP);
   Nvme_LogDebug("Controller capability: 0x%016lx.", hwCap);
   ctrlr->hwTimeout = (hwCap & NVME_CAP_TO_MSK64) >> NVME_CAP_TO_LSB;
   ctrlr->hwTimeout = (ctrlr->hwTimeout + 1) >> 1;
   Nvme_LogDebug("Controller timeout %d.", ctrlr->hwTimeout);

   /* Clear controller Enable (EN) */
   if (Nvme_Readl(regs + NVME_CSTS) & NVME_CSTS_RDY) {
      Nvme_Writel(0, (regs + NVME_CC));
      Nvme_LogDebug("CC: 0x%x.", Nvme_Readl((regs + NVME_CC)));
      Nvme_WaitCond(ctrlr, ctrlr->hwTimeout,
         (!(Nvme_Readl(regs+NVME_CSTS)&NVME_CSTS_RDY)), vmkStatus);
      Nvme_LogDebug("Initial disable result: 0x%x.", vmkStatus);
      if (vmkStatus != VMK_OK) {
         Nvme_LogError("Controller reset clear enable failure status 0x%x.",
            Nvme_Readl(regs + NVME_CSTS));
         return vmkStatus;
      }
   }

   /*
    * Note: on the Qemu emulator, simply write NVME_CC_ENABLE (0x1) to
    * (regs + NVME_CC) is not enough to bring controller to RDY state.
    * IOSQES and IOCQES has to be set to bring the controller to RDY
    * state for the initial reset.
    */
   config = NVME_CC_ENABLE;
   config |= NVME_CC_CSS_NVM << NVME_CC_CSS_LSB;
   config |= (VMK_PAGE_SHIFT - 12) << NVME_CC_MPS_LSB;
   config |= (NVME_CC_ARB_RR << NVME_CC_AMS_LSB);
   config |= (NVME_CC_SHN_NONE << NVME_CC_SHN_LSB);
   config |= (6 << NVME_CC_IOSQES_LSB);
   config |= (4 << NVME_CC_IOCQES_LSB);
   Nvme_LogDebug("Writing CC: 0x%08x.", config);
   Nvme_Writel(config, (regs + NVME_CC));
   Nvme_Readl((regs + NVME_CC));
   Nvme_WaitCond(ctrlr, ctrlr->hwTimeout,
      (Nvme_Readl(regs+NVME_CSTS)&NVME_CSTS_RDY), vmkStatus);
   Nvme_LogDebug("Initial reset result: 0x%x.", Nvme_Readl(regs+NVME_CSTS));

   if (vmkStatus != VMK_OK) {
      Nvme_LogError("Controller reset enable failure status 0x%x.",
         Nvme_Readl(regs + NVME_CSTS));
      // return vmkStatus;
   }

   Nvme_Writel(0, (regs + NVME_CC));
   Nvme_Readl((regs + NVME_CC));
   Nvme_WaitCond(ctrlr, ctrlr->hwTimeout,
      (!(Nvme_Readl(regs+NVME_CSTS)&NVME_CSTS_RDY)), vmkStatus);
   Nvme_LogDebug("Controller disable status: 0x%x.", vmkStatus);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("Controller reset clear enable failure status 0x%x.",
         Nvme_Readl(regs + NVME_CSTS));
      return vmkStatus;
   }

   /* Set admin queue depth of completion and submission */
   aqa = (sqInfo->qsize - 1) << NVME_AQA_SQS_LSB;
   aqa |= (qinfo->qsize - 1) << NVME_AQA_CQS_LSB;

   /* Set admin queue attributes */
   Nvme_Writel(aqa, (regs + NVME_AQA));
   Nvme_Writeq(qinfo->compqPhy, (regs + NVME_ACQ));
   Nvme_Writeq(sqInfo->subqPhy, (regs + NVME_ASQ));

   /* Setup controller configuration and enable */
   config = NVME_CC_ENABLE;
   config |= NVME_CC_CSS_NVM << NVME_CC_CSS_LSB;
   config |= (VMK_PAGE_SHIFT - 12) << NVME_CC_MPS_LSB;
   config |= (NVME_CC_ARB_RR << NVME_CC_AMS_LSB);
   config |= (NVME_CC_SHN_NONE << NVME_CC_SHN_LSB);
   config |= (6 << NVME_CC_IOSQES_LSB);
   config |= (4 << NVME_CC_IOCQES_LSB);
   Nvme_Writel(config, (regs + NVME_CC));

   Nvme_WaitCond(ctrlr, ctrlr->hwTimeout,
      (Nvme_Readl(regs+NVME_CSTS)&NVME_CSTS_RDY), vmkStatus);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("Controller reset enable failure status: 0x%x.",
         Nvme_Readl(regs + NVME_CSTS));
      Nvme_LogError("Failed to start controller, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   ctrlr->version = Nvme_Readl(regs + NVME_VS);
   if (ctrlr->version == 0xffffffff) {
       return VMK_FAILURE;
   }
   Nvme_LogInfo("Controller version: 0x%04x", ctrlr->version);

   Nvme_LogDebug("Controller %s started.", Nvme_GetCtrlrName(ctrlr));

   return VMK_OK;
}


/**
 * @brief This function stops controller operation by clearing CC.EN.
 *
 *
 * @param[in] ctrlr pointer to nvme device context
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
static VMK_ReturnStatus
NvmeCtrlr_HwStop(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;

   /*
    * Skip controller stop when controller is missing.
    */
   if (NvmeState_GetCtrlrState(ctrlr, VMK_TRUE) == NVME_CTRLR_STATE_MISSING) {
      return VMK_OK;
   }

   /* Clear controller Enable */
   if (Nvme_Readl(ctrlr->regs + NVME_CSTS) & NVME_CSTS_RDY)
       Nvme_Writel(0, (ctrlr->regs + NVME_CC));

   Nvme_WaitCond(ctrlr, ctrlr->hwTimeout,
      (!Nvme_Readl(ctrlr->regs+NVME_CSTS)&NVME_CSTS_RDY), vmkStatus);

   Nvme_LogDebug("Status after controller stop: 0x%x.",
      Nvme_Readl(ctrlr->regs + NVME_CSTS));

   /*
    * Return VMK_OK when controller is missing.
    */
   if (NvmeCore_IsCtrlrRemoved(ctrlr)) {
      return VMK_OK;
   }

   return vmkStatus;
}


/**
 * @brief This function Sends an Admin Command to controller.
 *
 * @param[in] ctrlr pointer to nvme device context
 * @param[in] entry pointer to NVME command entry
 * @param[in] cqEntry pointer to NVME completion entry
 * @param[in] timeoutUs command timeout in micronseconds
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
static VMK_ReturnStatus
NvmeCtrlr_SendAdmin(struct NvmeCtrlr *ctrlr, struct nvme_cmd *entry,
   struct cq_entry *cqEntry, int timeoutUs)
{
   VMK_ReturnStatus      vmkStatus;
   Nvme_Status           nvmeStatus;
   struct nvme_cmd      *cmd;
   struct NvmeQueueInfo *qinfo;
   struct NvmeCmdInfo   *cmdInfo;

   if (cqEntry) {
      vmk_Memset(cqEntry, 0, sizeof(struct cq_entry));
   }

   qinfo = &ctrlr->adminq;
   qinfo->lockFunc(qinfo->lock);

   cmdInfo = NvmeCore_GetCmdInfo(qinfo);
   if (!cmdInfo) {
      qinfo->unlockFunc(qinfo->lock);
      return VMK_NO_MEMORY;
   }
   qinfo->unlockFunc(qinfo->lock);

   cmdInfo->type = ADMIN_CONTEXT;
   entry->header.cmdID = cmdInfo->cmdId;
   cmd = &cmdInfo->nvmeCmd;
   Nvme_Memcpy64(&cmdInfo->nvmeCmd, entry, sizeof(*entry)/sizeof(vmk_uint64));

   Nvme_LogDebug("Submitting admin command 0x%x, id:%d.", cmd->header.opCode,
                 cmdInfo->cmdId);
   NvmeDebug_DumpCmd(entry);

   nvmeStatus = NvmeCore_SubmitCommandWait(qinfo, cmdInfo, cqEntry,
                                           timeoutUs);
   if (!SUCCEEDED(nvmeStatus)) {
      Nvme_LogVerb("admin command %p [%d] failed, 0x%x, %s.",
                   cmdInfo, cmdInfo->cmdId,
                   nvmeStatus, NvmeCore_StatusToString(nvmeStatus));
      if (DELAYED_RETURN(nvmeStatus)) {
         vmkStatus = VMK_TIMEOUT;
      } else {
         vmkStatus = VMK_FAILURE;
      }
   } else {
      vmkStatus = VMK_OK;
   }

   Nvme_LogDebug("Completed admin command 0x%x, id:%d, status:0x%x",
                 entry->header.opCode, entry->header.cmdID, vmkStatus);

   if (cqEntry) {
      NvmeDebug_DumpCpl(cqEntry);
   }

   return vmkStatus;
}


/**
 * @brief This function Retrieves controller/Namespace identify data.
 *
 *
 * @param[in] ctrlr pointer to nvme device context
 * @param[in] nsId namespace ID
 * @param[in] dmaAddr dma address to copy Identify data
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
VMK_ReturnStatus
NvmeCtrlrCmd_Identify(struct NvmeCtrlr *ctrlr, int nsId, vmk_IOA dmaAddr)
{
   VMK_ReturnStatus vmkStatus;
   struct nvme_cmd entry;
   struct cq_entry cqEntry;

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.opCode = NVM_ADMIN_CMD_IDENTIFY;
   if (nsId < 0) {
      entry.cmd.identify.controllerStructure = IDENTIFY_CONTROLLER;
   } else {
      entry.cmd.identify.controllerStructure = IDENTIFY_NAMESPACE;
      entry.header.namespaceID = nsId;
   }
   entry.header.prp[0].addr = dmaAddr;
   entry.header.prp[1].addr = (dmaAddr+VMK_PAGE_SIZE) & ~(VMK_PAGE_SIZE -1);

   vmkStatus = NvmeCtrlr_SendAdmin(ctrlr, &entry, &cqEntry, ADMIN_TIMEOUT);
   Nvme_LogDebug("Identify [0x%04x] completion result 0x%x, Status 0x%x",
               nsId, vmkStatus, cqEntry.SC);

   return vmkStatus;
}


/**
 * @brief This function Deletes a submission queue
 *
 *
 * @param[in] ctrlr pointer to nvme device context
 * @param[in] id submisssion queue ID
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
VMK_ReturnStatus
NvmeCtrlrCmd_DeleteSq(struct NvmeCtrlr *ctrlr, vmk_uint16 id)
{
   struct nvme_cmd entry;

   Nvme_LogDebug("qid: %d.", id);

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.opCode             = NVM_ADMIN_CMD_DEL_SQ;
   entry.cmd.deleteSubQ.identifier = id;
   return (NvmeCtrlr_SendAdmin(ctrlr, &entry, NULL, ADMIN_TIMEOUT));
}


/**
 * @brief This function Deletes a completion queue
 *
 *
 * @param[in] ctrlr pointer to nvme device context
 * @param[in] id completion queue ID
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
VMK_ReturnStatus
NvmeCtrlrCmd_DeleteCq(struct NvmeCtrlr *ctrlr, vmk_uint16 id)
{
   struct nvme_cmd entry;

   Nvme_LogDebug("qid: %d.", id);

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.opCode             = NVM_ADMIN_CMD_DEL_CQ;
   entry.cmd.deleteCplQ.identifier = id;
   return (NvmeCtrlr_SendAdmin(ctrlr, &entry, NULL, ADMIN_TIMEOUT));
}


/**
 * @brief This function Creates a completion queue
 *
 *
 * @param[in] ctrlr pointer to nvme device context
 * @param[in] qinfo pointer to queue information block
 * @param[in] id queue ID
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
VMK_ReturnStatus
NvmeCtrlrCmd_CreateCq(struct NvmeCtrlr *ctrlr, struct NvmeQueueInfo *qinfo, vmk_uint16 qid)
{
   struct nvme_cmd entry;

   Nvme_LogDebug("qid: %d.", qid);

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.opCode                  = NVM_ADMIN_CMD_CREATE_CQ;
   entry.header.prp[0].addr             = qinfo->compqPhy;
   entry.cmd.createCplQ.identifier      = qid;
   entry.cmd.createCplQ.size            = qinfo->qsize - 1;
   entry.cmd.createCplQ.contiguous      = 1;
   entry.cmd.createCplQ.interruptEnable = 1;
   entry.cmd.createCplQ.interruptVector = qinfo->intrIndex;

   return(NvmeCtrlr_SendAdmin(ctrlr, &entry, NULL, ADMIN_TIMEOUT));
}


/**
 * @brief This function Creates a submission queue
 *
 *
 * @param[in] ctrlr pointer to nvme device context
 * @param[in] qinfo pointer to queue information block
 * @param[in] id queue ID
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
VMK_ReturnStatus
NvmeCtrlrCmd_CreateSq(struct NvmeCtrlr *ctrlr, struct NvmeQueueInfo *qinfo, vmk_uint16 qid)
{
   struct nvme_cmd entry;

   Nvme_LogDebug("qid: %d.", qid);

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.opCode                    = NVM_ADMIN_CMD_CREATE_SQ;
   entry.header.prp[0].addr               = qinfo->subQueue->subqPhy;
   entry.cmd.createSubQ.identifier        = qid;
   entry.cmd.createSubQ.size              = qinfo->subQueue->qsize - 1;
   entry.cmd.createSubQ.contiguous        = 1;
   entry.cmd.createSubQ.priority          = 0;  /** High */
   entry.cmd.createSubQ.completionQueueID = qinfo->id;

   return (NvmeCtrlr_SendAdmin(ctrlr, &entry, NULL, ADMIN_TIMEOUT));
}


/**
 * @brief This function Sends a set feature command
 *
 * @param[in] ctrlr pointer to nvme device context
 * @param[in] feature feature ID
 * @param[in] option feature option
 * @param[in] prp pointer to prp list of feature data
 * @param[in] cqEntry pointer to completion entry
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
VMK_ReturnStatus
NvmeCtrlrCmd_SetFeature(struct NvmeCtrlr *ctrlr, vmk_uint16 feature, vmk_uint32 option,
         struct nvme_prp *prp, struct cq_entry *cqEntry)
{
   struct nvme_cmd entry;

   DPRINT2("Feature ID 0x%0x, option 0x%08x", feature, option);
   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.opCode             = NVM_ADMIN_CMD_SET_FEATURES;
   if (prp) {
       entry.header.prp[0]         = *prp;
       entry.header.prp[1].addr    = (prp->addr + VMK_PAGE_SIZE) & ~(VMK_PAGE_SIZE -1);
   }
   entry.cmd.setFeatures.featureID = feature;
   entry.cmd.asUlong[1]            = option;
   return (NvmeCtrlr_SendAdmin(ctrlr, &entry, cqEntry, ADMIN_TIMEOUT));
}


/**
 * @brief This function retrieves a feature information
 *
 * @param[in] ctrlr pointer to nvme device context
 * @param[in] feature feature ID
 * @param[in] option feature option
 * @param[in] prp pointer to prp list of feature data
 * @param[in] cqEntry pointer to completion entry
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
VMK_ReturnStatus
NvmeCtrlrCmd_GetFeature(struct NvmeCtrlr *ctrlr, int nsId, vmk_uint16 feature, vmk_uint32 option,
         struct nvme_prp *prp, struct cq_entry *cqEntry)
{
   struct   nvme_cmd entry;

   DPRINT2("Feature ID 0x%0x", feature);
   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.opCode             = NVM_ADMIN_CMD_GET_FEATURES;
   entry.header.namespaceID        = nsId;
   if (prp) {
       entry.header.prp[0]         = *prp;
       entry.header.prp[1].addr    = (prp->addr + VMK_PAGE_SIZE) & ~(VMK_PAGE_SIZE -1);
   }
   entry.cmd.getFeatures.featureID = feature;
   entry.cmd.asUlong[1]            = option;
   return (NvmeCtrlr_SendAdmin(ctrlr, &entry, cqEntry, ADMIN_TIMEOUT));
}

#if 0
/**
 * @brief This function sends a request to retrieve a log page
 *
 * @param[in] struct NvmeCtrlr *ctrlr pointer to nvme device context
 * @param[in] vmk_IOA dmaAddr dma address to copy log page data
 *
 * @return This function returns vmk_OK if successful, otherwise Error Code
 *
 */
VMK_ReturnStatus
NvmeCtrlrCmd_GetLogPage(struct NvmeCtrlr *ctrlr, vmk_IOA dmaAddr)
{
   VMK_ReturnStatus vmkStatus;
   struct nvme_cmd entry;
   struct cq_entry cqEntry;

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry->cmd.getLogPage.LogPageID = GLP_ID_SMART_HEALTH;
   entry->cmd.getLogPage.numDW = sizeof(smart_log)/sizeof(vmk_uint64);

   entry.header.Opcode = NVM_ADMIN_CMD_GET_LOG_PAGE;
   entry->header.namespaceID = 0xFFFFFFFF;
   entry->header.prp[0].addr = dmaAddr;
   entry->header.prp[1].addr = (dmaEntry + (PAGE_SIZE)) & ~(PAGE_SIZE -1);

   vmkStatus = NvmeCtrlr_SendAdmin(ctrlr, &entry, &cqEntry, ADMIN_TIMEOUT);
   Nvme_LogDebug("GetLogPage [0x%04x] completion result 0x%x, status 0x%x",
                  entry->header.namespaceID, vmkStatus, cqEntry.SC);
   return vmkStatus;
}

#endif

/**
 * @brief This function sends a request to retrieve a log page
 *
 * @param[in] ctrlr pointer to nvme device context
 * @param[in] nsID: namespace ID
 * @param[in] smartLog pointer to memory used for copying log page data. It is
 *            used only when sending sync cmd, Asnyc cmd has nothing to do with it.
 * @param[in] isWaitCmd If VMK_TRUE, issue command via async cmd NvmeCtrlr_SendAdmin
 *            which waits the comand to complete. Otherwise use sync cmd NvmeQueue_SendCmd.
 *
 * @return This function returns vmk_OK if successful, otherwise Error Code
 *
 */

VMK_ReturnStatus
NvmeCtrlrCmd_GetLogPage(struct NvmeCtrlr *ctrlr, vmk_uint32 nsID, struct smart_log *smartLog,
                        vmk_Bool isSyncCmd)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;;
   struct nvme_cmd entry;
   struct NvmeDmaEntry *dmaEntry;

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.namespaceID = nsID;
   entry.cmd.getLogPage.LogPageID = GLP_ID_SMART_HEALTH & 0xFFFF;
   entry.cmd.getLogPage.numDW = (LOG_PG_SIZE/sizeof(vmk_uint32)-1);
   entry.header.opCode = NVM_ADMIN_CMD_GET_LOG_PAGE;

   dmaEntry = &ctrlr->smartDmaEntry;
   entry.header.prp[0].addr = dmaEntry->ioa;
   entry.header.prp[1].addr = (dmaEntry->ioa + (VMK_PAGE_SIZE)) &
                     ~(VMK_PAGE_SIZE -1);
   /* TODO: handle GLP_ID_ERR_INFO and GLP_ID_FIRMWARE_SLOT_INFO */

   if (isSyncCmd == VMK_TRUE) {
      /* Send sync command*/
      DPRINT11("admin cmd 0x%x ", entry.header.opCode);
      vmkStatus = NvmeCtrlr_SendAdmin(ctrlr,&entry, NULL, ADMIN_TIMEOUT);

      /* Copy log page info*/
      if(vmkStatus == VMK_OK) {
         Nvme_Memcpy64(smartLog, (struct smart_log*)dmaEntry->va, LOG_PG_SIZE/sizeof(vmk_uint64));
         ctrlr->smartLastUpdateTime = Nvme_GetTimeUS();
      }
      Nvme_LogVerb("GetLogPage [0x%04x],timeout %d s, completion result 0x%x",
         entry.header.namespaceID, ADMIN_TIMEOUT, vmkStatus);
   }
   /*TODO: handle async GetLopPage request if needed*/

   else {
#if 0
      /* Construct async command*/
      struct NvmeQueueInfo *qinfo;
      struct NvmeCmdInfo *cmdInfo;

      qinfo = &ctrlr->adminq;
      qinfo->lockFunc(qinfo->lock);
      DPRINT11("qinfo %p, va %ld, phy %ldx", qinfo,
              (unsigned long)dmaEntry->va, (unsigned long)dmaEntry->ioa);

      cmdInfo = NvmeCore_GetCmdInfo(qinfo);
      if (!cmdInfo) {
         EPRINT("Memory Allocation error to retrieve log with async command.");
         qinfo->unlockFunc(qinfo->lock);
         return VMK_NO_MEMORY;
      }

      cmdInfo->timeoutId = ctrlr->timeoutId;
      qinfo->timeout[cmdInfo->timeoutId]++;

      /*Save dmaEntry for future free use*/
      cmdInfo->cmdParam = (vmk_uint64)dmaEntry;
      cmdInfo->type = LOG_CONTEXT;

      entry.header.cmdID = cmdInfo->cmdId;
      Nvme_Memcpy64(&cmdInfo->nvmeCmd, &entry, sizeof(entry)/sizeof(vmk_uint64));

      qinfo->unlockFunc(qinfo->lock);

      /* Send sync command, bh is processed in NvmeQueue_ProcessCq().
       * Note: call NvmeCore_PutCmdInfo() in  NvmeQueue_ProcessCq()
       */
      vmkStatus = NvmeQueue_SendCmd(qinfo->subQueue, cmdInfo);
      Nvme_LogDebug("Queued get log page command.");
#endif
      vmkStatus = VMK_BAD_PARAM;
   }
   return vmkStatus;
}


/**
 * This function replaces chars after a nul terminator (including the nul
 * terminator) with spaces (' '), and insert a new nul terminator at the end of
 * the string.
 */
static void
ConvertNullToSpace(char *buffer, int bufferSize)
{
   int i;
   int nulFound = bufferSize;

   if (!buffer) {
      return;
   }

   for (i = 0; i < bufferSize; i++) {
      if (buffer[i] == '\0') {
         nulFound = i;
         break;
      }
   }

   Nvme_LogDebug("buffer: %s, nul: %d size: %d", buffer, nulFound, bufferSize);

   if (nulFound < bufferSize) {
      vmk_Memset(buffer + nulFound, ' ', bufferSize - nulFound);
   }

   buffer[bufferSize - 1] = '\0';
}

/**
 * This function replaces ':' with ' ' in str (model number or serial number)
 * since there is a conflict. The partition under /dev/disks will also use ':'.
 * See PR #1299256.
 */
static void
FindAndReplaceSplChar(vmk_uint8 *str, vmk_uint32 size)
{
   vmk_uint32 i = 0;

   for (i = 0; i < size; i++) {
       if (str[i] == ':') {
          str[i] = ' ';
       }
   }
   Nvme_LogDebug("str: %s, size: %d", str, size);
}

/**
 * Get IDNETIFY CONTROLLER data block
 *
 * This function issues IDENTIFY to the controller, and extracts certain
 * data from the IDENTIFY response and fills controller instance data block.
 *
 * @param [in] ctrlr controller instance
 */
static VMK_ReturnStatus
NvmeCtrlr_GetIdentify(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeDmaEntry dmaEntry;

   vmkStatus = OsLib_DmaAlloc(ctrlr, VMK_PAGE_SIZE, &dmaEntry);
   if (vmkStatus != VMK_OK) {
      return vmkStatus;
   }

   vmkStatus = NvmeCtrlrCmd_Identify(ctrlr, -1, dmaEntry.ioa);
   if (vmkStatus != VMK_OK) {
      goto free_dma;
   }

   Nvme_Memcpy64(&ctrlr->identify, (void *)dmaEntry.va, VMK_PAGE_SIZE/sizeof(vmk_uint64));
   vmkStatus = VMK_OK;

   /* Now we have completed IDENTIFY command, update controller
    * parameters based on IDENTIFY result.
    */
   ctrlr->admVendCmdCfg   = ctrlr->identify.admVendCmdCfg;
   ctrlr->nvmVendCmdCfg   = ctrlr->identify.nvmVendCmdCfg;
   ctrlr->nvmCacheSupport = ctrlr->identify.volWrCache;
   ctrlr->nvmCmdSupport   = ctrlr->identify.cmdSupt;
   ctrlr->logPageAttr     = ctrlr->identify.logPgAttrib;
   ctrlr->pcieVID         = ctrlr->identify.pcieVID;


   vmk_StringCopy(ctrlr->serial, ctrlr->identify.serialNum,
                  sizeof(ctrlr->serial));
   ConvertNullToSpace(ctrlr->serial, sizeof(ctrlr->serial));
   FindAndReplaceSplChar(ctrlr->serial, sizeof(ctrlr->serial));

   vmk_StringCopy(ctrlr->model, ctrlr->identify.modelNum,
                  sizeof(ctrlr->model));
   ConvertNullToSpace(ctrlr->model, sizeof(ctrlr->model));
   FindAndReplaceSplChar(ctrlr->model, sizeof(ctrlr->model));

   vmk_StringCopy(ctrlr->firmwareRev, ctrlr->identify.firmwareRev,
                  sizeof(ctrlr->firmwareRev));
   ConvertNullToSpace(ctrlr->firmwareRev, sizeof(ctrlr->firmwareRev));

   vmk_Memcpy(ctrlr->ieeeOui, ctrlr->identify.ieeeOui,
              sizeof(ctrlr->ieeeOui));

   ctrlr->maxAen = ctrlr->identify.asyncReqLmt + 1; /* Zero based value */
   if (ctrlr->maxAen > MAX_EVENTS) {
      ctrlr->maxAen = MAX_EVENTS;
   }

   ctrlr->nsCount = ctrlr->identify.numNmspc;

   Nvme_LogInfo("Controller: %s.", Nvme_GetCtrlrName(ctrlr));
   Nvme_LogInfo("Serial no: %s.", ctrlr->serial);
   Nvme_LogInfo("Model no: %s.", ctrlr->model);
   Nvme_LogInfo("Firmware revision: %s.", ctrlr->firmwareRev);

   Nvme_LogDebug("Admin Cmd Vendor Cfg: 0x%x.", ctrlr->admVendCmdCfg);
   Nvme_LogDebug("NVM Cmd Vendor Cfg: 0x%x.", ctrlr->nvmVendCmdCfg);
   Nvme_LogDebug("Number of namespaces: %d.", ctrlr->nsCount);

free_dma:
   OsLib_DmaFree(ctrlr, &dmaEntry);
   return vmkStatus;
}


/**
 * @brief This function sets controller features according to current driver
 *    selected interrupt coalescing parameters.
 *    This function is called once during driver probe time and also
 *    upon driver parameter update.
 *
 * @param[in] struct nvme_dev *dev pointer to nvme device context
 *
 * @return This function returns int 0 if successful, otherwise Error Code
 */
static VMK_ReturnStatus
NvmeCtrlr_IntrCoalescing(struct NvmeCtrlr *ctrlr)
{
#if 0
   int   result = 0;
   struct cq_entry cq;
   vmk_uint32   param;

   DPRINT("setting intr coalescing feature %d %d",
         intr_coalescing_time, intr_coalescing_threshold);

   if (intr_coalescing_threshold || intr_coalescing_time) {
       result |= nvme_set_feature(dev, FTR_ID_INT_COALESCING,
      (intr_coalescing_time << 8) | intr_coalescing_threshold,
                        NULL, NULL);
   }

   result = nvme_get_feature(dev, 0, FTR_ID_INT_COALESCING, 0, NULL, &cq);
   if (result) {
       EPRINT("Failed to validate feature status %d", result);
   }
   param = cq.param.cmdSpecific;
   DPRINT("returned param 0x%x", param);
   if (((param >> 8) != intr_coalescing_time) ||
         ((param & 0xFF) != intr_coalescing_threshold)) {
       EPRINT("Param validation error returned value 0x%x", param);
       result = -EINVAL;
   }
   return (result);
#endif
   Nvme_LogError("Not implemented.");
   return VMK_OK;
}


/**
 * @brief This function send a Request to retrieve no. available of IO queues
 *       Send a set feature requesting optimum number of
 *       queues. If hardware does not allow the requested
 *       number of IO queues, request for at least for number
 *       of NUMA nodes and if that fails, just request for
 *       a single IO queue.
 *
 * Note: For now, we always assume the number of completion and
 *    submission queues are same.
 *
 * @param[in] ctrlr pointer to nvme device context
 * @param[in] nrIoQueues pointer to variable returning number of queues
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
VMK_ReturnStatus
NvmeCtrlr_RequestIoQueues(struct NvmeCtrlr *ctrlr, vmk_uint32 *nrIoQueues)
{
   struct cq_entry cqEntry;
   VMK_ReturnStatus vmkStatus;

   DPRINT3("attempting to allocate [%d] IO queues", *nrIoQueues);

   do {
      vmkStatus = NvmeCtrlrCmd_SetFeature(ctrlr, FTR_ID_NUM_QUEUE,
         (*nrIoQueues << 16) | *nrIoQueues,
         NULL, &cqEntry);

      if (vmkStatus != VMK_OK) {
         EPRINT("Failed requesting nr_io_queues 0x%x", cqEntry.SC);
         if (*nrIoQueues == 1) {
            break;
         }

         *nrIoQueues = 1;
      }
   } while(vmkStatus != VMK_OK);

   if (vmkStatus != VMK_OK) {
      DPRINT3("maximum of [%d] IO queues", cqEntry.param.numCplQAlloc);
   }

   return vmkStatus;
}


/**
 * Free namespace data block
 *
 * Removes namespace data block from the controller, and frees resources
 * allocated for this namespace.
 *
 * @param [in] ctrlr controller instance
 * @param [in] ns namespace instance
 */
static void
NvmeCtrlr_FreeNs(struct NvmeCtrlr *ctrlr, struct NvmeNsInfo *ns)
{
   Nvme_LogDebug("Releasing Namespace [%d] %p", ns->id, ns);
   OsLib_LockDestroy(&ns->lock);
   vmk_ListRemove(&ns->list);
   Nvme_Free(ns);
}


/**
 * Allocate namespace data block for a given namespace id
 *
 * This function does namespace INDENTIFY to get namespace metadata from
 * the controller, and populate namespace data block for future reference
 * to this namespace.
 *
 * @param [in] ctrlr controller instance
 * @param [in] nsId namespace id to query
 */
static struct NvmeNsInfo *
NvmeCtrlr_AllocNs(struct NvmeCtrlr *ctrlr, int nsId)
{
   VMK_ReturnStatus vmkStatus;
   int   i;
   vmk_uint32   lba_format;
   struct NvmeNsInfo *ns;
   struct iden_namespace *ident;
   struct NvmeDmaEntry dmaEntry;
   char propName[VMK_MISC_NAME_MAX];

   ns = Nvme_Alloc(sizeof(*ns), 0, NVME_ALLOC_ZEROED);
   if (! ns) {
       EPRINT("Failed NS memory allocation.");
       return (NULL);
   }

   vmkStatus = OsLib_DmaAlloc(ctrlr, VMK_PAGE_SIZE, &dmaEntry);
   if (vmkStatus != VMK_OK) {
      goto free_ns;
   }


   vmkStatus = NvmeCtrlrCmd_Identify(ctrlr, nsId, dmaEntry.ioa);
   if (vmkStatus != VMK_OK) {
       EPRINT("Failed get NS Identify data.");
       goto free_dma;
   }

   ident = (struct iden_namespace *)dmaEntry.va;
   DPRINT2("NS [%d], size %lu, lba_fmt 0x%02x, Formats 0x%02x",
            nsId, ident->size,
            ident->fmtLbaSize, ident->numLbaFmt);
   DPRINT2("NS [%d], feature 0x%02x, Prot Cap 0x%02x, Prot Set 0x%02x",
            nsId, ident->feat,
            ident->dataProtCap, ident->dataProtSet);

   for (i = 0; i <= ident->numLbaFmt; i++) {
      DPRINT2("supported LBA format 0x%08x",
               *(vmk_uint32 *)&ident->lbaFmtSup[i]);
   }
   lba_format   = *(vmk_uint32 *)&ident->lbaFmtSup[ident->fmtLbaSize & 0x0F];
   DPRINT2("LBA format 0x%08x", lba_format);
   DPRINT2("Meta Data Capability 0x%02x", ident->metaDataCap);
   DPRINT2("LBA Data Prot Cap/Set 0x%02x/0x%02x",
            ident->dataProtCap, ident->dataProtSet);

   vmk_StringFormat(propName, VMK_MISC_NAME_MAX, NULL, "nvmeNs-%s-%d",
                    Nvme_GetCtrlrName(ctrlr), nsId);
   vmkStatus = OsLib_LockCreate(ctrlr->lockDomain, NVME_LOCK_RANK_MEDIUM,
                                propName, &ns->lock);
   if (vmkStatus != VMK_OK) {
       EPRINT("Failed NS lock creation.");
       goto free_dma;
   }

   vmk_ListInit(&ns->list);

   ns->id         = nsId;
   ns->blockCount = ident->size;
   ns->lbaShift   = (lba_format >> 16) & 0x0F;
   ns->feature    = ident->feat;

   /* Bit 4 of fmtLbaSize indicates type MetaData buffer.
    * Bit 4 Set, indicates 8 bytes meta data at end of buffer
    * Bit 4 Clear, indicated a seperate contiguous buffer.
    */
   ns->metasize    = lba_format & 0x0FFFF;
   ns->fmtLbaSize  = ident->fmtLbaSize;
   ns->dataProtCap = ident->dataProtCap;
   ns->dataProtSet = ident->dataProtSet;
   ns->ctrlr       = ctrlr;

   ns->eui64       = ident->eui64;

   DPRINT2("NS [%d] %p, adding to dev list %p, lba size %u",
         ns->id, ns, &ctrlr->nsList, (1 << ns->lbaShift));
   vmk_ListInsert(&ns->list, vmk_ListAtRear(&ctrlr->nsList));

   /* Need to free the DMA buffer used here */
   OsLib_DmaFree(ctrlr, &dmaEntry);

   /* Mark ns as ONLINE by default */
   ns->flags |= NS_ONLINE;

   /* Initially set ref count to 0 */
   vmk_AtomicWrite64(&ns->refCount, 0);

   return (ns);

free_dma:
   OsLib_DmaFree(ctrlr, &dmaEntry);

free_ns:
   Nvme_Free(ns);

   return NULL;
}


vmk_uint64
NvmeCtrlr_GetNs(struct NvmeNsInfo *ns)
{
   vmk_uint64 rc;
   rc = vmk_AtomicReadInc64(&ns->refCount);
#if NVME_DEBUG
   Nvme_LogDebug("ns %d refCount increased to %ld.",
      ns->id, vmk_AtomicRead64(&ns->refCount));
#endif
   return rc;
}

vmk_uint64
NvmeCtrlr_PutNs(struct NvmeNsInfo *ns)
{
   vmk_uint64 rc;

   rc = vmk_AtomicReadDec64(&ns->refCount);

#if NVME_DEBUG
   Nvme_LogDebug("ns %d refCount decreased to %ld.",
      ns->id, vmk_AtomicRead64(&ns->refCount));
#endif

   /**
    * Free the namespace data structure if reference count reaches 0.
    *
    * Note: We should never hit this condition when the device is operational.
    */
   if (rc == 1) {
      VMK_ASSERT(NvmeState_GetCtrlrState(ns->ctrlr, VMK_FALSE) != NVME_CTRLR_STATE_OPERATIONAL);
      NvmeCtrlr_FreeNs(ns->ctrlr, ns);
   }

   return rc;
}


/**
 * Allocate namespace data blocks for the controller
 *
 * The number of available namespaces are discovered during controller
 * IDENTIFY.
 *
 * @param [in] ctrlr controller instance
 */
VMK_ReturnStatus
NvmeCtrlr_AllocDisks(struct NvmeCtrlr *ctrlr)
{
   int nsId;
   struct NvmeNsInfo *ns;

   /**
    * For number of Namespaces discovered:
    *
    * a. get Namespace identify data
    * b. Create a block device queue
    * c. create a disk device.
    * d. add Namespace to list of devices
    */
   for (nsId = 1; nsId <= ctrlr->nsCount; nsId++) {

      DPRINT2("allocating Namespace %d", nsId);
      ns = NvmeCtrlr_AllocNs(ctrlr, nsId);
      if (! ns) {
         EPRINT("Failed to allocate NS information structure.");
         continue;
      }

      /**
       * Grab a reference to the namespace. The reference will be released
       * at device cleanup.
       */
      NvmeCtrlr_GetNs(ns);

#if 0
#if USE_NS_ATTR
       /**
        * Get Namespace attributes/
        */
       if (nvme_ns_attr(dev, nsId, ns)) {
         EPRINT("Failed get NS attributes, ignoring namespace.");
         continue;
       }
#endif
#endif
   }

   return VMK_OK;
}


/**
 * Free namespace data blocks for the adatper.
 *
 * @param [in] ctrlr controller instance
 */
VMK_ReturnStatus
NvmeCtrlr_FreeDisks(struct NvmeCtrlr *ctrlr)
{
   struct NvmeNsInfo *ns;
   vmk_ListLinks *itemPtr, *nextPtr;

   /*
    * First, offline all namespace by marking all LUNs as PDL.
    */
   if (ctrlr->scsiAdapter) {
      vmk_ScsiSetPathLostByDevice(&ctrlr->scsiAdapter->name,
            0,    /* channel */
            0,    /* target */
            -1);  /* all luns */
   }

   VMK_LIST_FORALL_SAFE(&ctrlr->nsList, itemPtr, nextPtr) {
      ns = VMK_LIST_ENTRY(itemPtr, struct NvmeNsInfo, list);

      /*
       * Try to delete the path. This is a best-effort operation, if there
       * are open handles to the SCSI device and path, the clean up would
       * fail.
       */
      if (ctrlr->scsiAdapter) {
         vmk_ScsiScanDeleteAdapterPath(&ctrlr->scsiAdapter->name,
               0,
               0,
               ns->id - 1);
      }

      DPRINT2("NS [%d], releasing resource %p", ns->id, ns);
      NvmeCtrlr_PutNs(ns);
   }

   return VMK_OK;
}


/**
 * Check if the IO function is ready by issuing a READ command.
 *
 * return value: NVME_STATUS_SUCCESS if success, otherwise not.
 */
Nvme_Status
NvmeCtrlr_CheckIOFunction(struct NvmeNsInfo *ns, struct NvmeQueueInfo *qinfo)
{
   Nvme_Status		nvmeStatus;
   struct nvme_cmd	*cmd;
   struct NvmeCmdInfo	*cmdInfo;
   vmk_uint64		timeout;
   struct NvmeCtrlr	*ctrlr;

   ctrlr = ns->ctrlr;
   qinfo->lockFunc(qinfo->lock);
   cmdInfo = NvmeCore_GetCmdInfo(qinfo);
   if (!cmdInfo) {
      qinfo->unlockFunc(qinfo->lock);
      return VMK_NO_MEMORY;
   }
   qinfo->unlockFunc(qinfo->lock);

   cmdInfo->vmkCmd = NULL;
   cmdInfo->cmdCount = 0;
   cmdInfo->ns = ns;
   cmd = &cmdInfo->nvmeCmd;

   cmd->header.opCode = NVM_CMD_READ;
   cmd->header.prp[0].addr = cmdInfo->prpPhy;
   cmd->header.prp[1].addr = 0;
   cmd->header.namespaceID = ns->id;
   cmd->header.cmdID = cmdInfo->cmdId;
   cmdInfo->timeoutId = ctrlr->timeoutId;
   cmdInfo->doneData = NULL;
   cmd->cmd.read.numLBA = 1;

   qinfo->timeout[cmdInfo->timeoutId] ++;

   cmdInfo->type = BIO_CONTEXT;
   cmdInfo->status = NVME_CMD_STATUS_ACTIVE;

   timeout = 1 * 1000 * 1000; /* 1 second in microseconds */
   Nvme_LogDebug("issue read to fw");
   nvmeStatus = NvmeCore_SubmitCommandWait(qinfo, cmdInfo, NULL, timeout);

   /*(1) Theoretically, the command will returns immediately with "NS not ready" status.
    *So sleep 1 second before issuing next command to save efforts.
    *(2) There is a minor possibility that the command times out due to fw problem, in this case,
    * the command will be marked with ABORT_CONTEXT and handled in processCq routine. Since at most
    * 60 commands will be issued, the submission queue will not get overwhelmed given that its size is 1024*/
   if(cmdInfo->status == NVME_CMD_STATUS_DONE) {
      /*read command returns with "NS not ready" tag*/
      Nvme_LogDebug("read returns by fw due to ns not ready, sleep 1s");
      vmk_WorldSleep(timeout);
      Nvme_LogDebug("sleep finished");
   }
   return nvmeStatus;
}

/**
 * Wait until IO is ready to function for this controller.
 *
 * @param [in] ctrlr controller instance
 *
 * return value: NVME_STATUS_SUCCESS if success, otherwise NVME_STATUS_FAILURE
 */
Nvme_Status
NvmeCtrlr_WaitDeviceReady(struct NvmeCtrlr *ctrlr)
{
   Nvme_Status                  nvmeStatus;
   struct NvmeNsInfo            *ns;
   struct NvmeQueueInfo         *qinfo;
   struct vmk_ListLinks         *itemPtr, *nextPtr;
   vmk_uint64			timeout;
   vmk_uint64			waitDuration;
   vmk_Bool			validNs;

   if (VMK_UNLIKELY(ctrlr->numIoQueues < 1)) {
      Nvme_LogError("IOqueue not ready: %d", ctrlr->numIoQueues);
      return  NVME_STATUS_FAILURE;
   }

   /*Use the 1st IO queue*/
   qinfo = &ctrlr->ioq[0];

   validNs = VMK_FALSE;
   /*Use the 1st namespace whose size > 0*/
   if(ctrlr->nsCount > 0) {
      /*issue cmd to the first useable namespace */
      VMK_LIST_FORALL_SAFE(&ctrlr->nsList, itemPtr, nextPtr) {
         ns = VMK_LIST_ENTRY(itemPtr, struct NvmeNsInfo, list);
         if (NvmeCore_ValidateNs(ns) == VMK_OK) {
            Nvme_LogDebug("check device status with namespace %d", ns->id);
            validNs = VMK_TRUE;
            break;
         }
      }
   }
   else {
      Nvme_LogVerb("nsCount = 0, no need to check IO, return success");
      return  NVME_STATUS_SUCCESS;
   }

   if(validNs == VMK_FALSE) {
      Nvme_LogVerb("All namespaces are invalid, no need to check IO, return success");
      return  NVME_STATUS_SUCCESS;
   }

   /*keep probing the device until it is ready. If being not ready for more than 60 seconds, quit.*/
   waitDuration = 60 * 1000 * 1000; /*60s in ms*/
   timeout = OsLib_GetTimerUs() + waitDuration;
   do {
      nvmeStatus = NvmeCtrlr_CheckIOFunction(ns, qinfo);
      Nvme_LogDebug("check IO function status 0x%x, %s", nvmeStatus, NvmeCore_StatusToString(nvmeStatus));
      if(!OsLib_TimeAfter(OsLib_GetTimerUs(), timeout)) {
         Nvme_LogVerb("device not ready after 60 seconds, quit");
         nvmeStatus = NVME_STATUS_FAILURE;
         break;
      }
   } while(!SUCCEEDED(nvmeStatus));

   Nvme_LogDebug("need %ld ms to bring up the device.", (OsLib_GetTimerUs() - timeout + waitDuration));
   return nvmeStatus;
}



/**
 * Start a controller
 *
 * @param [in] ctrlr controller instance
 */
VMK_ReturnStatus
NvmeCtrlr_Start(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   int              nrIoQueues;

   vmkStatus = NvmeCtrlr_HwStart(ctrlr);
   if (vmkStatus != VMK_OK) {
      return vmkStatus;
   }

   /* Initialize Completion and submission queues info */
   NvmeCtrlr_SuspendAdminQueue(ctrlr);
   NvmeCtrlr_ResetAdminQueue(ctrlr);
   NvmeCtrlr_ResumeAdminQueue(ctrlr);

   /* Asynchronous events */
   ctrlr->curAen = 0;

   vmkStatus = NvmeCtrlr_GetIdentify(ctrlr);
   if (vmkStatus != VMK_OK) {
      goto stop_hw;
   }

   /*
    * We should be able to allocate an IO queue with a unique
    * IRQ vector per SCSI completion queue. The number of SCSI
    * completion queues available is provided by PSA.
    */
   nrIoQueues = vmk_ScsiGetMaxNumCompletionQueues();
   Nvme_LogDebug("Requesting %d IO queues.", nrIoQueues);

   /*
    * We should have allocated enough MSI-x vectors already, if
    * not, fallback to use just one IO queue
    */
   if (!ctrlr->msixEnabled || ctrlr->numVectors < (nrIoQueues + 1)) {
      Nvme_LogVerb("Insufficient resources, using single IO queue.");
      nrIoQueues = 1;
   }

   /*
    * Determine number of queues required for optimum performance.
    */
   vmkStatus = NvmeCtrlr_RequestIoQueues(ctrlr, &nrIoQueues);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("Failed to allocate hardware IO queues.");
      goto stop_hw;
   }
   Nvme_LogDebug("Got %d HW IO queues.", nrIoQueues);
   ctrlr->numIoQueues = nrIoQueues;

   /*
    * Allocate IO queue information blocks, required DMA resources
    * and register IO queues with controller.
    */
   vmkStatus = NvmeCtrlr_CreateIoQueues(ctrlr);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("Failed to allocate IO queues, 0x%x.", vmkStatus);
      goto stop_hw;
   }

   /*
    * Setup controller features according to current device parameters.
    */
   vmkStatus = NvmeCtrlr_IntrCoalescing(ctrlr);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("Failed to set features, 0x%x.", vmkStatus);
   }

   /**
    * Allocate Namespace control blocks, create disk devices
    * and register block device interface.
    */
   vmk_ListInit(&ctrlr->nsList);
   NvmeCtrlr_AllocDisks(ctrlr);

   /*check if IO is ready to function for this controller*/
   if (NvmeCtrlr_WaitDeviceReady(ctrlr) != NVME_STATUS_SUCCESS) {
      Nvme_LogError("The device can not be operational.");
      goto stop_hw;
   }

   /*
    * Device is now operational.
    */
   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_STARTED, VMK_TRUE);

   return VMK_OK;


stop_hw:
   NvmeCtrlr_HwStop(ctrlr);

   return vmkStatus;
}


/**
 * Set the controller as missing, or hot removed.
 *
 * @param [in] ctrlr controller instance
 */
void
NvmeCtrlr_SetMissing(struct NvmeCtrlr *ctrlr)
{
   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_MISSING, VMK_TRUE);
}


static void
NvmeCtrlr_SuspendAdminQueue(struct NvmeCtrlr *ctrlr)
{
   /**
    * TODO: pick a correct timeoutId when doing suspend
    */
   NvmeCore_SuspendQueue(&ctrlr->adminq, 0);
}

static void
NvmeCtrlr_ResumeAdminQueue(struct NvmeCtrlr *ctrlr)
{
   NvmeCore_ResumeQueue(&ctrlr->adminq);
}

static void
NvmeCtrlr_ResetAdminQueue(struct NvmeCtrlr *ctrlr)
{
   NvmeCore_ResetQueue(&ctrlr->adminq);
}

/**
 * @brief This function suspends all IO queues.
 *       This function is called during error recovery to
 *       suspend IO queue processing.
 *
 * @param[in] struct nvme_dev *dev pointer to nvme device context
 * @param[in] u32 new_id Timer slot of queue timeout slot.
 *
 * @return void
 *    None.
 *
 * @note It is assumed that dev lock is held by caller.
 */
static void
NvmeCtrlr_SuspendIoQueues(struct NvmeCtrlr *ctrlr)
{
   struct NvmeQueueInfo *qinfo;
   int   i;
   /**
    * TODO: pick a correct new timeoutId
    */
   int   newId = 0;

   DPRINT4("device %p [%s], suspending %d queues",
            ctrlr, Nvme_GetCtrlrName(ctrlr), ctrlr->numIoQueues);

   for( i = 1; i <= ctrlr->numIoQueues; i++ ) {
      qinfo = &ctrlr->ioq[i - 1];
      NvmeCore_SuspendQueue(qinfo, newId);
   }
}


/**
 * @brief This function resumes all suspended BIO requests for all IO queues.
 *       This function is called during error recovery to
 *       resume normal IO queue processing.
 *
 * @param [in] ctrlr pointer to nvme device context
 *
 * @return void
 *    None.
 */
static void
NvmeCtrlr_ResumeIoQueues(struct NvmeCtrlr *ctrlr)
{
   struct NvmeQueueInfo *qinfo;
   int   i;

   DPRINT4("device %p [%s], resuming %d queues",
            ctrlr, Nvme_GetCtrlrName(ctrlr), ctrlr->numIoQueues);

   for(i = 1; i <= ctrlr->numIoQueues  ; i++ ) {
      qinfo = &ctrlr->ioq[i - 1];
      NvmeCore_ResumeQueue(qinfo);
   }
}

static void
NvmeCtrlr_ResetIoQueues(struct NvmeCtrlr *ctrlr)
{
   struct NvmeQueueInfo *qinfo;
   int    i;

   DPRINT4("device %p [%s], resetting %d queues",
            ctrlr, Nvme_GetCtrlrName(ctrlr), ctrlr->numIoQueues);
   for(i = 1; i <= ctrlr->numIoQueues  ; i++ ) {
      qinfo = &ctrlr->ioq[i - 1];
      NvmeCore_ResetQueue(qinfo);
   }
}


/**
 * This function Flushes all outstanding admin requests for the admin queue.
 *       This function is called during error recovery to
 *       either terminate all pending admin requests.
 *
 * @param [in] ctrlr pointer to nvme device context
 *
 * @return void
 *    None.
 */
static void
NvmeCtrlr_FlushAdminQueue(struct NvmeCtrlr *ctrlr)
{
   struct NvmeQueueInfo *qinfo;
   int id;

   qinfo = &ctrlr->adminq;
   qinfo->lockFunc(qinfo->lock);
   NvmeCore_FlushQueue(qinfo, NVME_STATUS_IN_RESET);
   for (id = 0; id < TIMEOUT_LIST; id ++) {
      qinfo->timeout[id] = 0;
   }
   qinfo->unlockFunc(qinfo->lock);
}


/**
 * This function Flushes all outstanding BIO requests for all IO queues.
 *       This function is called during error recovery to
 *       either terminate all pending BIO request or to
 *       insert into congestion queue.
 *
 * @param[in] ctrlr pointer to nvme device context
 * @param[in] ns optional pointer to namespace information
 * @param[in] status parameter specifies bio completion status
 *
 * @return void
 *    None.
 * @note It is assumed that ctrlr lock is held by caller.
 */
static void
NvmeCtrlr_FlushIoQueues(struct NvmeCtrlr *ctrlr, int status)
{
   struct NvmeQueueInfo *qinfo;
   int   i, id;

   DPRINT4("device %p [%s], flushing %d queues",
            ctrlr, Nvme_GetCtrlrName(ctrlr), ctrlr->numIoQueues);

   for(i = 1; i <= ctrlr->numIoQueues; i++ ) {
      qinfo = &ctrlr->ioq[i - 1];

      DPRINT4("qinfo %p [%d], nr_req %d, nr_act %d", qinfo, qinfo->id,
               qinfo->nrReq, qinfo->nrAct);

      qinfo->lockFunc(qinfo->lock);
      NvmeCore_FlushQueue(qinfo, status);
      /**
       * Clear timeout table.
       */
      for (id=0; id<TIMEOUT_LIST; id++) {
         qinfo->timeout[id] = 0;
      }
      qinfo->nrAct = 0;     /* reset active requests */
      qinfo->unlockFunc(qinfo->lock);
   }
}

/**
 * @brief This function Resets an IO queue.
 *       This function is called during error recovery to
 *       reset an IO queue. The only way to reset an IO queue,
 *       is to remove and recreate it.
 *
 * @param[in] qinfo pointer to queue information block
 * @param[in] restart flag inidicating controller is restarted
 *
 * @return This function returns int 0 if successful, otherwise Error Code
 *
 * @note It is assumed that queue lock is held by caller.
 */
static int
NvmeQueue_ResetIoQueue(struct NvmeQueueInfo *qinfo, int restart)
{
   int    result = 0;
   struct NvmeSubQueueInfo *sqinfo = qinfo->subQueue;
   struct NvmeCtrlr *ctrlr = qinfo->ctrlr;

   if (!restart) {
       /**
        * unregister submission and completion queue from hardware.
        */
      sqinfo = qinfo->subQueue;
      if (NvmeCtrlrCmd_DeleteSq(ctrlr, sqinfo->id)) {
         Nvme_LogError("Failed to destroy hardware IO submission queue %d",
                        sqinfo->id);
      }
      if (NvmeCtrlrCmd_DeleteCq(ctrlr, qinfo->id)) {
         Nvme_LogError("Failed to destroy hardware IO completion queue %d",
                        qinfo->id);
      }
   }

   /**
    * Reset the soft state of the queue
    */
   NvmeCore_ResetQueue(qinfo);

   /**
    * Need to re-create IO CQ and SQ in the firmware.
    */
   result = NvmeCtrlrCmd_CreateCq(ctrlr, qinfo, qinfo->id);
   if (result) {
       Nvme_LogError("Failed to create hardware IO completion queue %d",
                        qinfo->id);
       goto err_out;
   }

   result = NvmeCtrlrCmd_CreateSq(ctrlr, qinfo, sqinfo->id);
   if (result) {
       Nvme_LogError("Failed to create hardware IO submission queue %d",
                        sqinfo->id);
       NvmeCtrlrCmd_DeleteCq(ctrlr, qinfo->id);
       goto err_out;
   }

   result = 0;

err_out:
   return (result);
}


/**
 * @brief This function Restarts an IO queue.
 *       This function is called during error recovery to
 *       restart an IO queue.
 *
 *       a. Abort all outstanding BIO requests.
 *       b. Destroy hardware submission and completion queues.
 *       c. Create hardware submission and completion queues.
 *       d. Recreate command information free list.
 *       e. Restart IO queue.
 *
 * @param[in] qinfo pointer to queue information block
 * @param[in] restart flag inidicating controller is restarted
 *
 * @return This function returns int 0 if successful, otherwise Error Code
 *
 * @note It is assumed that dev lock is held by caller.
 *       TODO: ctrlr lock is NOT actually held here.
 */
static int
NvmeQueue_RestartIoQueue(struct NvmeQueueInfo *qinfo, int restart)
{
   int   result = 0;

   Nvme_LogInfo("Restarting io queue %p[%d].", qinfo, qinfo->id);
   /* TODO: Do we need to grab the queue lock here? */
   // qinfo->lockFunc(qinfo->lock);
   result = NvmeQueue_ResetIoQueue(qinfo, restart);
   if (result) {
      // qinfo->unlockFunc(qinfo->lock);
      Nvme_LogError("Failed IO queue reset qid %d", qinfo->id);
      return result;
   }
   // qinfo->unlockFunc(qinfo->lock);
   return result;
}


/**
 * @brief This function Restarts all IO queues.
 *       This function is called during error recovery to
 *       reset controller.
 *
 *       For all IO queue:
 *       a. Create hardware submission and completion queues.
 *       b. Recreate command information free list.
 *       c. Restart IO queue.
 *
 * @param[in] struct nvme_dev *dev pointer to nvme device context
 * @param[in] int restart flag inidicating controller is restarted
 *
 * @return This function returns int 0 if successful, otherwise Error Code
 *
 * @note It is assumed that dev lock is held by caller.
 *       TODO: ctrlr lock is NOT actually held here.
 */
static int
NvmeCtrlr_RestartIoQueues(struct NvmeCtrlr *ctrlr, int restart)
{
   struct  NvmeQueueInfo *qinfo;
   int   i, result = 0;

   for (i = 1; i <= ctrlr->numIoQueues; i++) {

      qinfo = &ctrlr->ioq[i - 1];
      result = NvmeQueue_RestartIoQueue(qinfo, restart);
      if (result) {
         Nvme_LogError("Failed IO queue reset, terminating restart");
         break;
      }
   }
   return result;
}


/**
 * This function Restarts Controller
 *       This function is called during error recovery to
 *       restart controller. All controller activities are
 *       halted and pending IO requests placed on congestion
 *       list. the controler is reset and all hwardware
 *       resources reinitialized.
 *
 *       a. Abort all outstanding BIO requests.
 *       b. Destroy all submission and completion queues.
 *       c. Initialize Admin Queue.
 *       c. Reset controller.
 *       c. Create all submission and completion queues.
 *       d. Recreate command information free list.
 *       e. Restart IO queues.
 *
 * @param [in] ctrlr    pointer to nvme device context
 * @param [in] status   status code for flushed outstanding cmds
 *
 * @return VMK_OK if successful
 * @return Error Code if failed
 */
VMK_ReturnStatus
NvmeCtrlr_HwReset(struct NvmeCtrlr *ctrlr, Nvme_Status status)
{
   VMK_ReturnStatus vmkStatus;
   Nvme_CtrlrState state;
   int nrIoQueues;

   Nvme_LogInfo("Restarting Controller %s.", Nvme_GetCtrlrName(ctrlr));
   state = NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_INRESET, VMK_TRUE);
   if (state == NVME_CTRLR_STATE_INRESET) {
      /**
       * We are already in reset
       */
       return VMK_BUSY;
   }


   /**
    * Inorder to reset an IO queue, we need to delete and
    * recreate the IO queue. This is required to quiesce
    * IO completions in progress bebfore we can reset hardware.
    */

   NvmeCtrlr_SuspendAdminQueue(ctrlr);
   NvmeCtrlr_SuspendIoQueues(ctrlr);

   /**
    * Stop the controller first.
    */
   NvmeCtrlr_HwStop(ctrlr);

   /**
    * Reset Queues
    */
   vmk_SpinlockLock(ctrlr->lock);

   /* Reset admin queue */
   NvmeCtrlr_FlushAdminQueue(ctrlr);
   NvmeCtrlr_ResetAdminQueue(ctrlr);

   /* Reset IO queue */
   NvmeCtrlr_FlushIoQueues(ctrlr, status);
   NvmeCtrlr_ResetIoQueues(ctrlr);

   /* Asynchronous events */
   ctrlr->curAen = 0;
   vmk_SpinlockUnlock(ctrlr->lock);

   /**
    * Now it is safe to restart the controller.
    */
   vmkStatus = NvmeCtrlr_HwStart(ctrlr);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("Controller Reset Failure.");
      Nvme_LogError("Offlining Controller.");
      goto err_out;
   }

   /**
    * Resume admin queue now.
    */
   NvmeCtrlr_ResumeAdminQueue(ctrlr);

   /**
    * As part of reset, we need to verify controller configuration
    * is still valid with existing driver configuration parameters.
    */
   vmkStatus = NvmeCtrlr_GetIdentify(ctrlr);
   if (vmkStatus != VMK_OK) {
       Nvme_LogError("Controller Identify Failure.");
       Nvme_LogError("Offlining Controller.");
       goto err_out;
   }

   /* Double check number of queues to be same as nrIoQueues */
   nrIoQueues = ctrlr->numIoQueues;
   vmkStatus = NvmeCtrlr_RequestIoQueues(ctrlr, &nrIoQueues);
   if (vmkStatus != VMK_OK) {
       Nvme_LogError("Failed to allocate hardware IO Queue error.");
       goto err_out;
   }
   if (nrIoQueues != ctrlr->numIoQueues) {
       Nvme_LogError("IO queue configuration changed!!!");
       Nvme_LogError("Unsupported configuration, failing controller.");
       goto err_out;
   }
   Nvme_LogDebug("Got %d hw IO queues", nrIoQueues);

   vmkStatus = NvmeCtrlr_RestartIoQueues(ctrlr, VMK_TRUE);
   if (vmkStatus) {
       Nvme_LogError("Failed to restart IO queue %0x.", vmkStatus);
       goto err_out;
   }

   /**
    * Lastly, resume IO queues.
    */
   NvmeCtrlr_ResumeIoQueues(ctrlr);

   /**
    * reinitiate AEN requests.
    */
#if SEND_AEN
   nvme_send_aen(ctrlr);
#endif


#if 0
   /**
    * Validate existing Namespace and if additional Namespaces
    * discovered, create a ns information block and create disk device.
    */
   Nvme_LogDebug("number of Namespaces %d", ctrlr->nsCount);
   if (dev->nsCount != ctrlr->identify->numNmspc) {
       Nvme_LogError("Number Namespaces changed since last discovery %d",
                  dev->identify->numNmspc);
       dev->nsCount = dev->identify->numNmspc;
   }
#endif

   /**
    * Device is operational, restart timer and kick restart
    * IO queue processing.
    */
   vmk_SpinlockLock(ctrlr->lock);
   NvmeCtrlr_ResumeIoQueues(ctrlr);

   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_OPERATIONAL, VMK_FALSE);
   vmk_SpinlockUnlock(ctrlr->lock);

#if 0 /* @todo - to validate namespaces. */
   /**
    * Validate current Namespaces.
    *
    * @todo - Need to handle removed namespace.
    */
   for (ns_id = 1; ns_id <= dev->ns_count; ns_id++) {
       nvme_validate_ns(dev, ns_id);
   }
#endif

   DPRINT1("Exit %d", vmkStatus);
   return vmkStatus;

err_out:
   vmk_SpinlockLock(ctrlr->lock);
   NvmeCtrlr_FlushIoQueues(ctrlr, 0 /* TODO: Some Error Code */);
   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_FAILED, VMK_FALSE);
   vmk_SpinlockUnlock(ctrlr->lock);

   return VMK_FAILURE;
}


/**
 * Stop a controller
 *
 * @param [in] ctrlr controller instance
 */
VMK_ReturnStatus
NvmeCtrlr_Stop(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;

   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_QUIESCED, VMK_TRUE);

   vmkStatus = NvmeCtrlr_HwStop(ctrlr);

   /*
    * Flush all I/O queues. Since the hardware queues should have been
    * destroyed during HwStop (CC.EN to 0), we only need to go through
    * the active command list and return all pending commands now.
    */
   vmk_SpinlockLock(ctrlr->lock);

   NvmeCtrlr_SuspendAdminQueue(ctrlr);
   NvmeCtrlr_SuspendIoQueues(ctrlr);

   NvmeCtrlr_FlushAdminQueue(ctrlr);
   NvmeCtrlr_ResetAdminQueue(ctrlr);

   NvmeCtrlr_FlushIoQueues(ctrlr, 0);

   /* Asynchronous events */
   ctrlr->curAen = 0;

   vmk_SpinlockUnlock(ctrlr->lock);

   /* Free queue resources and namespace resources */
   NvmeCtrlr_DeleteIoQueues(ctrlr);

   NvmeCtrlr_FreeDisks(ctrlr);

   return vmkStatus;
}


/**
 * Issue task management reset to the controller
 *
 * @param [in] ctrlr controller instance
 * @param [in] resetType type of the reset
 * @param [in] nsId namespace ID if applicable
 *
 * @return VMK_OK if task completes successfully
 */
VMK_ReturnStatus
NvmeCtrlr_DoTaskMgmtReset(struct NvmeCtrlr *ctrlr, Nvme_ResetType resetType,
                          int nsId)
{
   VMK_ReturnStatus vmkStatus;

   Nvme_LogDebug("Reset ctrlr %s: %s", Nvme_GetCtrlrName(ctrlr),
                 Nvme_GetResetTypeName(resetType));

   /**
    * Task managements should be serialized.
    */
   vmk_SemaLock(&ctrlr->taskMgmtMutex);

   switch(resetType) {
      case NVME_TASK_MGMT_BUS_RESET:
         /**
          * I_T Nexus Reset - Shall be supported by returning FUNCTION
          *                   SUCCEEDED if there are outstanding commands in
          *                   the submission queue, otherwise by returning
          *                   FUNCTION COMPLETE.
          */
         vmkStatus = NvmeCtrlr_HwReset(ctrlr, NVME_STATUS_RESET);
         break;
      case NVME_TASK_MGMT_LUN_RESET:
      case NVME_TASK_MGMT_DEVICE_RESET:
         /**
          * LOGICAL UNIT RESET - Shall be supported by writing a 0 to Enable
          *                      (EN) field of Controller Configuration
          *                      register
          */
         vmkStatus = NvmeCtrlr_HwReset(ctrlr, NVME_STATUS_RESET);
         break;
      default:
         vmkStatus = VMK_BAD_PARAM;
         VMK_ASSERT(0);
         break;
   }

   vmk_SemaUnlock(&ctrlr->taskMgmtMutex);

   return vmkStatus;
}


/**
 * Microseconds to delay before doing actual abort scan and NVM reset
 *
 * TODO: figure out the proper delay US for this. so far set it to 100ms.
 */
#define NVME_ABORT_DELAY_US (1000 * 100)

/**
 * Issue task management abort to the controller
 *
 * @param [in] ctrlr controller instance
 * @param [in] taskMgmt pointer to the task management request
 * @param [in] ns pointer to the namespace
 *
 * @return VMK_OK if request completes successfully
 */
VMK_ReturnStatus
NvmeCtrlr_DoTaskMgmtAbort(struct NvmeCtrlr *ctrlr, vmk_ScsiTaskMgmt *taskMgmt, struct NvmeNsInfo *ns)
{
   int                   i;
   int                   cmdsFound = 0, qf = 0;
   int                   cmdsImpacted = 0, qi = 0;
   struct NvmeQueueInfo *qinfo;
   struct NvmeCmdInfo   *cmdInfo;
   vmk_ScsiCommand      *vmkCmd;
   vmk_ListLinks        *itemPtr;
   Nvme_CtrlrState       ctrlrState;


   vmk_SemaLock(&ctrlr->taskMgmtMutex);

   ctrlrState = NvmeState_GetCtrlrState(ctrlr, VMK_TRUE);
   if (ctrlrState != NVME_CTRLR_STATE_OPERATIONAL) {
      Nvme_LogWarning("task management abort received while controller is in"
                      " %s state.", NvmeState_GetCtrlrStateString(ctrlrState));
      return VMK_BUSY;
   }


   /**
    * To give oustanding commands a chance to complete without being aborted,
    * wait a short period before we actually do abort scan.
    */
   vmk_WorldSleep(NVME_ABORT_DELAY_US);

   /**
    * First, block controlelr
    */
   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_SUSPEND, VMK_TRUE);

   NvmeCtrlr_SuspendIoQueues(ctrlr);

   /**
    * Stop the controller, then give outstanding commands a chance to complete.
    */
   for (i = 0; i < ctrlr->numIoQueues; i++) {
      qinfo = &ctrlr->ioq[i];

      qf = 0;
      qi = 0;

      Nvme_LogDebug("scan %s I:%p SN:0x%lx in queue %d, req:%d act:%d.",
         vmk_ScsiGetTaskMgmtTypeName(taskMgmt->type),
         taskMgmt->cmdId.initiator, taskMgmt->cmdId.serialNumber, qinfo->id,
         qinfo->nrReq, qinfo->nrAct);

      qinfo->lockFunc(qinfo->lock);

      /**
       * Flush completed items, make sure that completed items are preserved.
       */
      NvmeCore_ProcessQueueCompletions(qinfo);

      /**
       * Now search for still active cmds, if there are any, we need to do an
       * NVM reset to clear them
       */
      VMK_LIST_FORALL(&qinfo->cmdActive, itemPtr) {
         cmdInfo = VMK_LIST_ENTRY(itemPtr, struct NvmeCmdInfo, list);
         vmkCmd  = NvmeCore_CmdInfoToScsiCmd(cmdInfo);

         if (VMK_UNLIKELY(vmkCmd == NULL)) {
            /**
             * We really shouldn't see an command carrying no vmkCmd here.
             */
            VMK_ASSERT(0);
            continue;
         }

         /**
          * Check if the command should be aborted. A command should be aborted
          * if:
          *    1. taskMgmt is ABORT, and the initiator/serialNumber of vmkCmd
          *       matches the one in taskMgmt;
          *    2. taskMgmt is VIRT_RESET, and the initiator of vmkCmd matches
          *       the one in taskMgmt.
          *
          * vmk_ScsiQueryTaskMmgt should do this check for us.
          */
         if (vmk_ScsiQueryTaskMgmt(taskMgmt, vmkCmd) ==
               VMK_SCSI_TASKMGMT_ACTION_ABORT) {
            cmdsFound ++;
            qf ++;
            Nvme_LogDebug("vmkCmd %p [%Xh] I:%p SN:0x%lx found to be aborted.",
               vmkCmd, vmkCmd->cdb[0], vmkCmd->cmdId.initiator,
               vmkCmd->cmdId.serialNumber);
         } else {
            /**
             * TODO: next, we are going to do an NVM reset to clear stuck
             * commands, this could impact other oustanding commands that
             * are not requested to be aborted. Should move those commands to
             * congestion queue for re-issue.
             */
            cmdsImpacted ++;
            qi ++;
         }
      }

      Nvme_LogDebug("scan %s in queue %d completed, %d found, %d impacted.",
                    vmk_ScsiGetTaskMgmtTypeName(taskMgmt->type),
                    qinfo->id, qf, qi);

      qinfo->unlockFunc(qinfo->lock);
   }

   /**
    * Finally, if we found commands to be aborted, issue NVM reset to clear them
    */
   if (cmdsFound) {
      Nvme_LogDebug("scan %s completed, %d found, %d impacted.",
         vmk_ScsiGetTaskMgmtTypeName(taskMgmt->type), cmdsFound, cmdsImpacted);

      NvmeCtrlr_HwReset(ctrlr, NVME_STATUS_ABORTED);

      /**
       * After reset, the controller state should transition to OPERATIONAL.
       */
   } else {
      /**
       * No command is found matching the requested task management request.
       * the command should have been completed by hardware already.
       */

      NvmeCtrlr_ResumeIoQueues(ctrlr);

      NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_OPERATIONAL, VMK_TRUE);
   }

   vmk_SemaUnlock(&ctrlr->taskMgmtMutex);

   return VMK_OK;
}



/* Controller wide helper functions */


#if 0
VMK_ReturnStatus
NvmeCtrl_ProcessLogPage(struct NvmeQueueInfo *qinfo, struct NvmeCmdInfo *cmdInfo)
{
   vmk_uint32 pageId;
   vmk_uint32 pageSize;
   vmk_VA *logPage;
   if (cmdInfo->cqEntry.SC) {
      Nvme_LogDebug("Failed Get Log Page comand 0x%x, param %d, result 0x%x",
                     cmdInfo->nvmeCmd.header.opCode,
                     cmdInfo->cqEntry.param.cmdSpecific,
                     cmdInfo->cqEntry.SC);
      return VMK_FAILURE;
   }

   logPage = &cmdInfo->dmaEntry.va;

   /* Detern log size based on its page_id*/
   pageId = cmdInfo->cqEntry.param.cmdSpecific & 0xffff;
   Nvme_LogDebug("cmdInfo %p, pageId %d", cmdInfo, pageId);
   switch(pageId) {
   case GLP_ID_ERR_INFO:		/* 64 Bytes*/
      pageSize = 64;
      break;
   case GLP_ID_SMART_HEALTH:		/* 512 Bytes*/
      pageSize = 512;
      return NvmeCtrlr_ReportSmart(logPage);
   case GLP_ID_FIRMWARE_SLOT_INFO:	/* 512 Bytes*/
      pageSize = 512;
      break;
   default:
      Nvme_LogError("Unknown Log page ID %d", pageId);
      return VMK_FAILURE;
   }
   return VMK_OK;
}


struct NvmeCmdInfo*
NvmeCtrl_CompleteLog(struct NvmeQueueInfo *qinfo, vmk_uint32 type)
{
   struct NvmeCmdInfo *cmdInfo;
   vmk_ListLinks *itemPtr;
   VMK_LIST_FORALL(&qinfo->cmdActive, itemPtr) {
      cmdInfo = VMK_LIST_ENTRY(itemPtr, struct NvmeCmdInfo, list);
      if((cmdInfo->type == type) && (cmdInfo->status != -1)) {
         Nvme_LogDebug("Handled one LOG_CONTEXT cmd, cmdInfo %p", cmdInfo);
         return (cmdInfo);
      }
   }
   return NULL;

}

VMK_ReturnStatus
NvmeCtrlr_CheckLog(void *data)
{
   struct NvmeCtrlr *ctrlr = (struct NvmeCtrlr *)data;
   struct NvmeCmdInfo *cmdInfo;
   struct NvmeQueueInfo *qinfo = &ctrlr->adminq;

   qinfo->lockFunc(qinfo->lock);
   do {
      cmdInfo = NvmeCtrl_CompleteLog(qinfo, LOG_CONTEXT);
      if (!cmdInfo)
         break;
      Nvme_LogDebug("cmdInfo %p", cmdInfo);
      NvmeCtrl_ProcessLogPage(qinfo, cmdInfo);
      qinfo->unlockFunc(qinfo->lock);
      OsLib_DmaFree(ctrlr, &cmdInfo->dmaEntry);
/*   dma_free_coherent(dev->dma_dev, LOG_PG_SIZE, cmd_info->req, (dma_addr_t)cmd_info->cmd_param);*/
      qinfo->lockFunc(qinfo->lock);
      qinfo->timeout[cmdInfo->timeoutId]--;
      NvmeCore_PutCmdInfo(qinfo, cmdInfo);
   } while(cmdInfo);

   qinfo->unlockFunc(qinfo->lock);
  return VMK_OK;
}

VMK_ReturnStatus
NvmeCtrlr_CheckLogEvent(void *data)
{
   struct NvmeCtrlr *ctrlr = (struct NvmeCtrlr *)data;
   VMK_ReturnStatus status = VMK_OK;

   while (status != VMK_DEATH_PENDING) {
      status = vmk_WorldWait(ctrlr->nvmeWorldID, VMK_LOCK_INVALID,
                             (5 * VMK_MSEC_PER_SEC),
                             "Waiting for nvme log world to wakeup");
   }
   return VMK_OK;
}
#endif


/**
 * @brief This function Validates user uio data structure.
 *     This function is called by IOCTL function to validate
 *     pass-through uio header and its content.
 *
 * @param[in] ctrlr Pointer NVME device context
 * @param[in] uio kernel address space uio header pointer
 * @param[in] usr_io flag to identify user IO request.
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
static VMK_ReturnStatus
ValidateUio(struct NvmeCtrlr *ctrlr, struct usr_io *uio, vmk_Bool usr_io)
{
   DPRINT8("Validating uio %p.", uio);
   NvmeDebug_DumpUio(uio);

   if (VMK_UNLIKELY(usr_io &&
      ((uio->cmd.header.opCode >= NVME_VNDR_CMD_IO_CODE_START) &&
         (uio->cmd.header.opCode <= NVME_VNDR_CMD_IO_CODE_END)))) {
      if (!ctrlr->nvmVendCmdCfg) {
         Nvme_LogVerb("Firmware does not support Vendor Specific.");
         return VMK_NOT_SUPPORTED;
      }
      if ((uio->length < uio->cmd.cmd.vendorSpecific.buffNumDW >> 2) ||
         (uio->meta_length < uio->cmd.cmd.vendorSpecific.metaNumDW >> 2)) {
         Nvme_LogVerb("length mismatch data %d, meta %d",
            uio->cmd.cmd.vendorSpecific.buffNumDW,
            uio->cmd.cmd.vendorSpecific.metaNumDW);
         return VMK_BAD_PARAM;
      }
   }

#if 0
   /**
    * Validate command buffer access.
    */
   if (!access_ok(VERIFY_READ, uio->addr, uio->length)) {
       DPRINT8("Access violation command Ptr 0x%llx\n", uio->addr);
       return -EFAULT;
   }
   DPRINT8("uio %p, Access OK\n", uio);
#endif

   /**
    * Validate data access.
    */
   if (uio->length) {
#if 0
       if (!access_ok((uio->direction == XFER_TO_DEV) ?
               VERIFY_WRITE : VERIFY_READ,
               uio->addr, uio->length)) {
      DPRINT8("Access violation data Ptr 0x%llx, length %d\n",
                  uio->addr, uio->length);
      return (-EFAULT);
       }
#endif
      if (uio->length > (transfer_size * 1024)) {
         EPRINT("Request transfer length exceeds maximum allowed %d",
            uio->length);
         return VMK_BAD_PARAM;
      }
   }
   DPRINT8("uio %p, addr %lx, len %d Access OK", uio, uio->addr, uio->length);

   /**
    * Validate status buffer access.
    */
   if (uio->meta_length) {
#if 0
       if (!access_ok((uio->direction == XFER_TO_DEV) ?
               VERIFY_WRITE : VERIFY_READ,
               uio->meta_addr, uio->meta_length)) {
      DPRINT8("Access violation meta data Ptr 0x%llx, length %d\n",
               uio->meta_addr, uio->meta_length);
      return (-EFAULT);
       }
#endif
      if (uio->meta_length > VMK_PAGE_SIZE) {
         EPRINT("Request meta data length exceeds maxmimum allowed %d",
            uio->meta_length);
         return VMK_BAD_PARAM;
      }
   }

   DPRINT8("uio %p, Meta addr 0x%lx, len %d Access OK",
      uio, uio->meta_addr, uio->meta_length);

   return VMK_OK;
}


/**
 * @brief This function checkd for list of disallowed user Admin requests.
 *    This function is called by IOCTL function to perform
 *    a check and valifdate that command opcode does not iterfear with
 *    driver operation.
 *
 * @param[in] ctrlr Pointer NVME device context
 * @param[in] uio Pointer to uio structure in kernel space.
 *
 * @return This function returns VMK_OK if allowed, otherwise Error Code
 *
 * @note: ECN-23 requires us to check for vendor unique request and to
 *    validate data length if supported.
 */
static VMK_ReturnStatus
AllowedAdminCmd(struct NvmeCtrlr *ctrlr, struct usr_io *uio)
{
   struct NvmeNsInfo *ns;
   vmk_ListLinks *itemPtr;

   switch (uio->cmd.header.opCode) {

      case NVM_ADMIN_CMD_DEL_SQ:
      case NVM_ADMIN_CMD_CREATE_SQ:
      case NVM_ADMIN_CMD_DEL_CQ:
      case NVM_ADMIN_CMD_CREATE_CQ:
      case NVM_ADMIN_CMD_ABORT:
      case NVM_ADMIN_CMD_ASYNC_EVENT_REQ:
      {
         DPRINT9("Disallowed Admin command 0x%x.",
               uio->cmd.header.opCode);
         return VMK_NOT_SUPPORTED;
      }
      case NVM_ADMIN_CMD_FORMAT_NVM:
      {
         vmk_SpinlockLock(ctrlr->lock);
         VMK_LIST_FORALL(&ctrlr->nsList, itemPtr) {
            ns = VMK_LIST_ENTRY(itemPtr, struct NvmeNsInfo, list);
            DPRINT9("ns id %d command nsID %d flags %x",
               ns->id, uio->namespace, ns->flags);
            if (ns->id == uio->cmd.header.namespaceID ||
               NVME_FULL_NAMESPACE == uio->cmd.header.namespaceID) {
               if (NvmeCore_IsNsOnline(ns)) {
                  vmk_SpinlockUnlock(ctrlr->lock);
                  DPRINT9("Disallowed Admin command Format 0x%x\n",
                     uio->cmd.header.opCode);
                  return VMK_NOT_SUPPORTED;
               }
               DPRINT9("allowing Admin command Format 0x%x flags %x\n",
                  uio->cmd.header.opCode, ns->flags);
               break;
            }
         }
         vmk_SpinlockUnlock(ctrlr->lock);
         return VMK_OK;
      }
      default:
      {
         if ((uio->cmd.header.opCode & NVME_VNDR_CMD_ADM_CODE_START) ==
            NVME_VNDR_CMD_ADM_CODE_START)
         {
            if (!ctrlr->admVendCmdCfg) {
               DPRINT9("Vendor Specific command not supported.\n");
               return VMK_NOT_SUPPORTED;
            }
            if ((uio->length <
               uio->cmd.cmd.vendorSpecific.buffNumDW >> 2) ||
               (uio->meta_length < uio->cmd.cmd.vendorSpecific.metaNumDW >> 2)) {
               EPRINT("Vendor Specific data length mismatch.\n");
               return VMK_BAD_PARAM;
            }
         }
         return VMK_OK;
      }
   }
}


/**
 * @brief This function maps user pages into kernel memory
 *
 * @param[in] ctrlr Pointer NVME device context
 * @param[in] uio Pointer to user request
 * @param[in] cmdInfo Pointer to Command Information Block
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 *
 * @note: We have blocked meta data and fail any request with non-zero length.
 * @note: We currently do not actually map user pages into the kernel address
 *        space. instead, we do bounce buffers for the user io and copy from/to
 *        user space address at the entry and exit of IOCTL command.
 */
static VMK_ReturnStatus
MapUserPages(struct NvmeCtrlr *ctrlr, struct usr_io *uio,
             struct NvmeDmaEntry *dmaEntry)
{
   VMK_ReturnStatus vmkStatus;

   vmkStatus = OsLib_DmaAlloc(ctrlr, uio->length, dmaEntry);
   if (vmkStatus) {
      DPRINT8("Failed to allocate dma buffer for uio, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   if(uio->direction == XFER_TO_DEV) {
      vmkStatus = vmk_CopyFromUser(dmaEntry->va, uio->addr, uio->length);
      if (vmkStatus) {
         DPRINT8("Failed to copy from user buffer, 0x%x.", vmkStatus);
         goto err_free_dmabuffer;
      }
   }

   return VMK_OK;


err_free_dmabuffer:
   OsLib_DmaFree(ctrlr, dmaEntry);

   return vmkStatus;
}


/**
 * @brief This function unmaps user data from kernel address space.
 *
 * @param[in] ctrlr Pointer NVME device context
 * @param[in] uio Pointer to user request
 * @param[in] cmdInfo Pointer to Command Information Block
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 *
 */
static VMK_ReturnStatus
UnmapUserPages(struct NvmeCtrlr *ctrlr, struct usr_io *uio,
               struct NvmeDmaEntry *dmaEntry)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;

   /**
    * Since we are doing bounce buffer, copy the data back to user buffer if
    * requesting data from device
    */
   if(uio->direction == XFER_FROM_DEV) {
      vmkStatus = vmk_CopyToUser(uio->addr, dmaEntry->va, uio->length);
      if (vmkStatus) {
         DPRINT8("Failed to copy to user buffer, 0x%x.", vmkStatus);
      }
   }

   OsLib_DmaFree(ctrlr, dmaEntry);

   return vmkStatus;
}


/**
 * @brief This function updates user uio data structure.
 *    This function is called by pass-through function to update
 *    user uio data structure, updating data length, meta data
 *    length, status and completion entry.
 *
 * @param[in] uio Pointer to uio structure in kernel address
 * @param[in] cmdInfo Pointer to command information block.
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 *
 * @note: We only copy the cq_entry back to uio here.
 *
 */
static VMK_ReturnStatus
PutUio(struct usr_io *uio)
{
   return VMK_OK;
}


/**
 * Free DMA buffer allocated for an Admin Passthru command, in ABORT context.
 *
 * If an admin passthru command failed (due to TIMEOUT or other reasons), the
 * DMA buffer cannot be freed inline since the command may still be outstanding
 * in the hardware and freeing the DMA buffer inline may introduce problems
 * when hardware tries to access the DMA buffer which has alrady been freed.
 *
 * In that case, this function is called during command completion time, to
 * free the DMA buffer when we are guaranteed that the command is leaving
 * hardware.
 */
static void
AdminPassthruFreeDma(struct NvmeQueueInfo *qinfo, struct NvmeCmdInfo *cmdInfo)
{
   struct NvmeDmaEntry *dmaEntry = cmdInfo->cleanupData;

   if (cmdInfo->type == ABORT_CONTEXT) {
      Nvme_LogVerb("Freeing DMA buffer from cmd %p.", cmdInfo);
      OsLib_DmaFree(qinfo->ctrlr, dmaEntry);
      Nvme_Free(dmaEntry);
   }
}


/**
 * @brief This function process user Admin request.
 *    This function is called by IOCTL function to perform
 *    a user requested Admin command.
 *
 * @param[in] ctrlr Pointer to NVME device control block
 * @param[in] uio Pointer to uio structure in user address space.
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 *
 * @note: ECN-23 requires us to check for vendor unique request and to
 *    validate data length if supported.
 */
static VMK_ReturnStatus
AdminPassthru(struct NvmeCtrlr *ctrlr, struct usr_io *uio)
{
   Nvme_Status           nvmeStatus;
   VMK_ReturnStatus      vmkStatus;
   struct NvmeCmdInfo   *cmdInfo;
   struct NvmeQueueInfo *qinfo;
   struct NvmeDmaEntry  *uioDmaEntry = NULL;
   vmk_ByteCount         length;
   Nvme_CtrlrState       state;

   /**
    * Block admin commands if the controller is not in STARTED or OPERATIONAL
    * state.
    */
   state = NvmeState_GetCtrlrState(ctrlr, VMK_TRUE);
   if (state != NVME_CTRLR_STATE_STARTED &&
       state != NVME_CTRLR_STATE_OPERATIONAL) {
      return VMK_FAILURE;
   }

   if (ValidateUio(ctrlr, uio, VMK_FALSE)) {
      DPRINT9("Failed validation %p.", uio);
      return VMK_FAILURE;
   }

   if ((vmkStatus = AllowedAdminCmd(ctrlr, uio))) {
      return vmkStatus;
   }

   qinfo = &ctrlr->adminq;
   qinfo->lockFunc(qinfo->lock);
   cmdInfo = NvmeCore_GetCmdInfo(qinfo);
   qinfo->unlockFunc(qinfo->lock);
   if (!cmdInfo) {
      DPRINT9("Out of Cmd_Info data %p", qinfo);
      return VMK_NO_MEMORY;
   }
   Nvme_Memcpy64(&cmdInfo->nvmeCmd, &uio->cmd,
      sizeof(struct nvme_cmd)/sizeof(vmk_uint64));

#if NVME_DEBUG
   if (nvme_dbg & NVME_DEBUG_DUMP) {
      int          i;
      vmk_uint32   *ptr;
      ptr = (vmk_uint32 *)&cmdInfo->nvmeCmd;
      for (i=0; i<sizeof(struct nvme_cmd)/sizeof(vmk_uint32); i += sizeof(vmk_uint32)) {
         DPRINT("%02x: %08x %08x %08x %08x\n", i,
         ptr[i], ptr[i+1], ptr[i+2], ptr[i+3]);
      }
   }
#endif

   cmdInfo->uio = uio;
   cmdInfo->type = ADMIN_CONTEXT;
   cmdInfo->count = uio->length;
   cmdInfo->nvmeCmd.header.cmdID = cmdInfo->cmdId;
   DPRINT9("command ID %d", cmdInfo->cmdId);

   /**
    * Map user space and Create an scatter gather list of user data.
    */
   if (uio->length) {

      uioDmaEntry = Nvme_Alloc(sizeof(*uioDmaEntry), 0, NVME_ALLOC_ZEROED);
      if (!uioDmaEntry) {
         /**
          * Need to free the cmdInfo allocated before exit.
          */
         qinfo->lockFunc(qinfo->lock);
         NvmeCore_PutCmdInfo(qinfo, cmdInfo);
         qinfo->unlockFunc(qinfo->lock);

         goto out;
      }

      vmkStatus = MapUserPages(ctrlr, uio, uioDmaEntry);
      if (vmkStatus) {
         Nvme_Free(uioDmaEntry);

         /**
          * Need to free the cmdInfo allocated before exit.
          */
         qinfo->lockFunc(qinfo->lock);
         NvmeCore_PutCmdInfo(qinfo, cmdInfo);
         qinfo->unlockFunc(qinfo->lock);

         goto out;
      }

      cmdInfo->cleanup     = AdminPassthruFreeDma;
      cmdInfo->cleanupData = uioDmaEntry;

      /**
       * Initialzie sgPosition so that we can process SG to PRPs later
       */
      vmkStatus = vmk_SgFindPosition(uioDmaEntry->sgOut, 0,
                                     &cmdInfo->sgPosition);
      VMK_ASSERT(vmkStatus == VMK_OK);

      cmdInfo->cmdBase = cmdInfo;
      cmdInfo->requiredLength= uio->length;
      cmdInfo->requestedLength = 0;
      length = NvmeIo_ProcessPrps(qinfo, cmdInfo);

      /**
       * We allocate physically contiguous buffer for uio, so we should
       * not need to split the command for this IO.
       */
      VMK_ASSERT(length == uio->length);
   }

   /**
    * Now submit the command to HW and wait for completion
    */
   nvmeStatus = NvmeCore_SubmitCommandWait(qinfo, cmdInfo,
                                           &uio->comp, uio->timeoutUs);
   vmkStatus = SUCCEEDED(nvmeStatus) ? VMK_OK : VMK_FAILURE;
   uio->status = vmkStatus;
   DPRINT9("Command completion result 0x%x.", vmkStatus);

   if (cmdInfo->status == NVME_CMD_STATUS_DONE &&
       uioDmaEntry != NULL) {
      /**
       * We only free DMA buffers inline when the command is successful.
       */
      UnmapUserPages(ctrlr, uio, uioDmaEntry);
      Nvme_Free(uioDmaEntry);
   }

   vmkStatus = PutUio(uio);

out:
   DPRINT9("Result 0x%x, uio status 0x%x.", vmkStatus, uio->status);

   return vmkStatus;
}


/**
 * Dump registers
 *
 * @param [in] ctrlr controller instance
 * @param [in] uio pointer to the uio structure
 *
 * @return VMK_OK if completes successfully; otherwise return error code
 */
static VMK_ReturnStatus
DumpRegs(struct NvmeCtrlr *ctrlr, struct usr_io *uio)
{
   int length;

   length = min_t(int, ctrlr->barSize, uio->length);
   uio->meta_length = length;

   return vmk_CopyToUser(uio->addr, ctrlr->regs, length);
}


static VMK_ReturnStatus
nvmeMgmtSetCtrlrOnline(struct NvmeCtrlr *ctrlr, struct usr_io *uio,
                       vmk_Bool isOnline)
{
   Nvme_Status nvmeStatus;

   nvmeStatus = NvmeCore_SetCtrlrOnline(ctrlr, isOnline);
   uio->status = nvmeStatus;

   if (SUCCEEDED(nvmeStatus)) {
      return VMK_OK;
   } else {
      return VMK_FAILURE;
   }
}


/**
 * Process ioctl commands
 *
 * @param [in] ctrlr controller instance
 * @param [in] cmd ioctl command
 * @param [in] uio pointer to pass-through command
 *
 * @return VMK_OK if completes successfully; otherwise return error code
 */
VMK_ReturnStatus
NvmeCtrlr_IoctlCommon(struct NvmeCtrlr *ctrlr, vmk_uint32 cmd,
                      struct usr_io *uio)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;

   switch(cmd) {
      case NVME_IOCTL_ADMIN_CMD:
         vmkStatus = AdminPassthru(ctrlr, uio);
         break;
      case NVME_IOCTL_IO_CMD:
         vmkStatus = VMK_NOT_SUPPORTED;
         break;
      case NVME_IOCTL_RESTART:
         vmkStatus = VMK_NOT_SUPPORTED;
         break;
      case NVME_IOCTL_HOTREMOVE:
         vmkStatus = VMK_NOT_SUPPORTED;
         break;
      case NVME_IOCTL_HOTADD:
         vmkStatus = VMK_NOT_SUPPORTED;
         break;
      case NVME_IOCTL_EVENT:
         vmkStatus = VMK_NOT_SUPPORTED;
         break;
      case NVME_IOCTL_SET_CACHE:
         vmkStatus = VMK_NOT_SUPPORTED;
         break;
      case NVME_IOCTL_DUMP_REGS:
         vmkStatus = DumpRegs(ctrlr, uio);
         break;
      case NVME_IOCTL_SET_CTRLR_ONLINE:
         vmkStatus = nvmeMgmtSetCtrlrOnline(ctrlr, uio, VMK_TRUE);
         break;
      case NVME_IOCTL_SET_CTRLR_OFFLINE:
         vmkStatus = nvmeMgmtSetCtrlrOnline(ctrlr, uio, VMK_FALSE);
         break;
      default:
         Nvme_LogError("unknown ioctl command %d.", cmd);
         vmkStatus = VMK_BAD_PARAM;
         break;
   }

   uio->status = vmkStatus;
   return vmkStatus;
}
