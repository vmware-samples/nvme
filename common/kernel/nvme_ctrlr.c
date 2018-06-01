/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (c) 2012-2013  Integrated Device Technology, Inc.
 * All rights reserved.
 *******************************************************************************/

/*
 * @file: nvme_ctrlr.c --
 *
 *    Nvme controller related stuff
 */
#include "oslib.h"
#include "nvme_private.h"
#include "nvme_exc.h"
#include "nvme_debug.h"

const char *NvmeCtrlr_GetErrorStatusString (int errorStatus);
const char *NvmeCtrlr_GetAsyncEventHealthStatusString (int critWarning);
#if ASYNC_EVENTS_ENABLED
static void AsyncEventReportComplete (struct NvmeQueueInfo *qinfo,
      struct NvmeCmdInfo *cmdInfo);
#endif



static void
NvmeCtrlr_FlushAdminQueue(struct NvmeCtrlr *ctrlr);
static void
NvmeCtrlr_SuspendAdminQueue(struct NvmeCtrlr *ctrlr);
static void
NvmeCtrlr_ResumeAdminQueue(struct NvmeCtrlr *ctrlr);
static void
NvmeCtrlr_ResetAdminQueue(struct NvmeCtrlr *ctrlr);


const char* Nvme_AsyncEventHealthStatusString [] = { "Device Reliability Degrated", "Temperature above threshold", "Spare below Threshold" };

const char* NvmeCtrlr_GetAsyncEventHealthStatusString (int healthStatus)
{
  if ((healthStatus < AER_INFO_SH_DEV_RELIABILITY) || (healthStatus >= AER_INFO_SH_SPARE_BELOW_THRESHOLD)) {
     return "";
  }
  return Nvme_AsyncEventHealthStatusString[healthStatus];
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
VMK_ReturnStatus
NvmeCtrlr_ValidateParams(struct NvmeCtrlr *ctrlr)
{
   vmk_uint64 hwCap;
   vmk_uint32 minPage, maxPage, hwMaxQs;

   hwCap = Nvme_Readq(ctrlr->regs + NVME_CAP);
   hwMaxQs = (hwCap & NVME_CAP_MQES_MSK64) + 1;

   DPRINT_CTRLR("Controller Capability reg: %016lx", hwCap);

   ctrlr->ioCompQueueSize = io_cpl_queue_size;
   ctrlr->ioSubQueueSize = io_sub_queue_size;

   /* Validate completion and submission queue size */
   if (hwMaxQs == 1) {
      EPRINT("Invalid CAP.MQES: 0");
   } else {
      if (io_cpl_queue_size > hwMaxQs) {
         WPRINT("Parameter io_cpl_queue_size %d exceeds max HW queue size %d,"
                " use HW max queue size.", io_cpl_queue_size, hwMaxQs);
         ctrlr->ioCompQueueSize = hwMaxQs;
      }
      if (io_sub_queue_size > hwMaxQs) {
         WPRINT("Parameter io_sub_queue_size %d exceeds max HW queue size %d,"
                " use HW max queue size.", io_sub_queue_size, hwMaxQs);
         ctrlr->ioSubQueueSize = hwMaxQs;
      }
   }

   minPage = (1 << (((hwCap & NVME_CAP_MPSMIN_MSK64) >> NVME_CAP_MPSMIN_LSB) + 12));
   maxPage = (1 << (((hwCap & NVME_CAP_MPSMAX_MSK64) >> NVME_CAP_MPSMAX_LSB) + 12));
   DPRINT_CTRLR("hardware max page size %d, min page size %d", maxPage, minPage);

   if ((maxPage < VMK_PAGE_SIZE) || (minPage > VMK_PAGE_SIZE)) {
       EPRINT("Controller does not support OS default Page size %u", VMK_PAGE_SIZE);
       return VMK_BAD_PARAM;
   }

   max_prp_list = (transfer_size * 1024) / VMK_PAGE_SIZE;
   DPRINT_CTRLR("Max xfer %d, Max PRP %d", transfer_size, max_prp_list);

   return VMK_OK;
}


/**
 * Setup admin queue.
 *
 * This only allocates resources for admin q, but doesn't set AQA, ASQ, and ACQ.
 *
 * @param [in] ctrlr controller instance
 */
VMK_ReturnStatus
NvmeCtrlr_AdminQueueSetup(struct NvmeCtrlr *ctrlr)
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

   DPRINT_Q("Admin queue constructed, %p.", qinfo);

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
VMK_ReturnStatus
NvmeCtrlr_AdminQueueDestroy(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeQueueInfo *qinfo;

   qinfo = &ctrlr->adminq;

   NvmeCore_SuspendQueue(qinfo);

   /* Flush and reset Admin Queue in case there are still cmds in situation of hot plug. */
   NvmeCtrlr_FlushAdminQueue(ctrlr);
   NvmeCtrlr_ResetAdminQueue(ctrlr);
   vmkStatus = NvmeQueue_Destroy(qinfo);

   DPRINT_Q("Destroyed admin queue, 0x%x.", vmkStatus);

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

   if (!nrIoQueues || (ctrlr->ctrlOsResources.msixEnabled && nrIoQueues >= ctrlr->ctrlOsResources.numVectors)) {
      EPRINT("nrIoQueues: %d, numVectors: %d.", nrIoQueues, ctrlr->ctrlOsResources.numVectors);
      VMK_ASSERT(0);
      return VMK_BAD_PARAM;
   }

   /* Note: always create shared IO queues for now */
   /* TODO: modify this behavior to allow non-shared IO queues */
   shared = 1;

   ctrlr->ioq = Nvme_Alloc(sizeof(struct NvmeQueueInfo) * nrIoQueues,
      VMK_L1_CACHELINE_SIZE, NVME_ALLOC_ZEROED);
   if (!ctrlr->ioq) {
      return VMK_NO_MEMORY;
   }

   for (i = 1, allocated = 0; i <= nrIoQueues; i++, allocated ++) {
      intrIndex = ctrlr->ctrlOsResources.msixEnabled ? i : 0;

      ctrlr->ioq[i-1].ctrlr = ctrlr;
      vmkStatus = NvmeQueue_Construct(&ctrlr->ioq[i - 1], /* IO queue starts from index 1 */
         ctrlr->ioSubQueueSize, ctrlr->ioCompQueueSize, i, shared, intrIndex);
      if (vmkStatus != VMK_OK) {
         goto free_queues;
      }

      qinfo = &ctrlr->ioq[i - 1];
      sqInfo = qinfo->subQueue;

      DPRINT_Q("IO queue [%d] %p, Comp DB 0x%lx, Sub DB 0x%lx, vector: %ds",
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
      NvmeCore_SuspendQueue(&ctrlr->ioq[allocated]);
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
         EPRINT("trying to delete active queue %d.", qinfo->id);
         VMK_ASSERT(0);
         continue;
      }

      /*
       * We skip the destroy of hardware I/O queues if the controller is
       * already offline or failed.
       */
      if (NvmeState_GetCtrlrState(ctrlr) != NVME_CTRLR_STATE_FAILED &&
          NvmeState_GetCtrlrState(ctrlr) != NVME_CTRLR_STATE_QUIESCED &&
          NvmeState_GetCtrlrState(ctrlr) != NVME_CTRLR_STATE_MISSING) {
         vmkStatus = NvmeCtrlrCmd_DeleteSq(ctrlr, sqInfo->id);
         DPRINT_Q("Destroyed sq %d, 0x%x.", sqInfo->id, vmkStatus);
         vmkStatus = NvmeCtrlrCmd_DeleteCq(ctrlr, qinfo->id);
         DPRINT_Q("Destroyed cq %d, 0x%x.", qinfo->id, vmkStatus);
      }

      NvmeCore_SuspendQueue(qinfo);
      vmkStatus = NvmeQueue_Destroy(qinfo);
      DPRINT_Q("Destroyed queue %d, 0x%x.", qinfo->id, vmkStatus);
   }

   /* Finally free the queue pools we have created */
   Nvme_Free(ctrlr->ioq);
   ctrlr->ioq = NULL;
   ctrlr->numIoQueues = 0;

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

   if (NvmeCore_IsCtrlrRemoved(ctrlr)) {
      EPRINT("Device is missing.");
      NvmeCtrlr_SetMissing(ctrlr);
      return VMK_PERM_DEV_LOSS;
   }

   hwCap = Nvme_Readq(regs + NVME_CAP);
   DPRINT_CTRLR("Controller capability: 0x%016lx.", hwCap);
   ctrlr->hwTimeout = (hwCap & NVME_CAP_TO_MSK64) >> NVME_CAP_TO_LSB;
   ctrlr->hwTimeout = (ctrlr->hwTimeout + 1) >> 1;
   DPRINT_CTRLR("Controller timeout %d.", ctrlr->hwTimeout);

   /* Clear controller Enable (EN) */
   if (Nvme_Readl(regs + NVME_CSTS) & NVME_CSTS_RDY) {
      Nvme_Writel(0, (regs + NVME_CC));
      DPRINT_CTRLR("CC: 0x%x.", Nvme_Readl((regs + NVME_CC)));
      Nvme_WaitCond(ctrlr, ctrlr->hwTimeout,
         (!(Nvme_Readl(regs+NVME_CSTS)&NVME_CSTS_RDY)), vmkStatus);
      if (vmkStatus == VMK_PERM_DEV_LOSS) {
         EPRINT("Device is missing. Controller reset clear enable fails.");
         NvmeCtrlr_SetMissing(ctrlr);
         return vmkStatus;
      }
      DPRINT_CTRLR("Initial disable status: 0x%x.", Nvme_Readl(regs + NVME_CSTS));
      if (vmkStatus != VMK_OK) {
         EPRINT("Controller reset clear enable failure status 0x%x.",
                Nvme_Readl(regs + NVME_CSTS));
         return vmkStatus;
      }
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
      if (vmkStatus != VMK_PERM_DEV_LOSS) {
         EPRINT("Controller reset enable failure status: 0x%x.",
                Nvme_Readl(regs + NVME_CSTS));
      } else {
         EPRINT("Device is missing. Controller reset enable fails.");
         NvmeCtrlr_SetMissing(ctrlr);
      }
      EPRINT("Failed to start controller, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   ctrlr->version = Nvme_Readl(regs + NVME_VS);
   if (ctrlr->version == 0xffffffff) {
       return VMK_FAILURE;
   }
   DPRINT_CTRLR("Controller version: 0x%04x", ctrlr->version);

   DPRINT_CTRLR("Controller %s started.", Nvme_GetCtrlrName(ctrlr));

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
   if (NvmeCore_IsCtrlrRemoved(ctrlr)) {
      NvmeCtrlr_SetMissing(ctrlr);
      return VMK_OK;
   }

   /* Clear controller Enable */
   if (Nvme_Readl(ctrlr->regs + NVME_CSTS) & NVME_CSTS_RDY)
       Nvme_Writel(0, (ctrlr->regs + NVME_CC));

   Nvme_WaitCond(ctrlr, ctrlr->hwTimeout,
      (!Nvme_Readl(ctrlr->regs+NVME_CSTS)&NVME_CSTS_RDY), vmkStatus);

   /*
    * Return VMK_OK when controller is missing.
    */
   if (NvmeCore_IsCtrlrRemoved(ctrlr)) {
      NvmeCtrlr_SetMissing(ctrlr);
      return VMK_OK;
   }

   DPRINT_CTRLR("Status after controller stop: 0x%x.",
      Nvme_Readl(ctrlr->regs + NVME_CSTS));

   return vmkStatus;
}

static inline int
GetAdminCmdDirection(struct nvme_cmd *entry)
{
   nvm_admin_opcodes_e opCode = entry->header.opCode;
   switch(opCode) {
      case NVM_ADMIN_CMD_FIRMWARE_DOWNLOAD:
      case NVM_ADMIN_CMD_NS_ATTACH:
         return XFER_TO_DEV;
      case NVM_ADMIN_CMD_NS_MGMT:
         if (entry->cmd.nsMgmt.sel == 0) {
            return XFER_TO_DEV;
         } else {
            return XFER_FROM_DEV;
         }

      default:
         return XFER_FROM_DEV;
   }
}

/**
 * Free DMA buffer allocated for an Admin command, in ABORT context.
 *
 * If an admin command failed (due to TIMEOUT or other reasons), the
 * DMA buffer cannot be freed inline since the command may still be outstanding
 * in the hardware and freeing the DMA buffer inline may introduce problems
 * when hardware tries to access the DMA buffer which has alrady been freed.
 *
 * In that case, this function is called during command completion time, to
 * free the DMA buffer when we are guaranteed that the command is leaving
 * hardware.
 */
static void
SendAdminCleanup(struct NvmeQueueInfo *qinfo, struct NvmeCmdInfo *cmdInfo)
{
   struct NvmeDmaEntry *dmaEntry = cmdInfo->cleanupData;

   OsLib_DmaFree(&qinfo->ctrlr->ctrlOsResources, dmaEntry);
   Nvme_Free(dmaEntry);
}

static inline void *
PopulateAdminCmdInfo(struct NvmeCtrlr *ctrlr,
                     struct NvmeCmdInfo *cmdInfo,
                     struct nvme_cmd *entry,
                     vmk_uint8 *buf,
                     vmk_uint32 length)
{
   VMK_ReturnStatus     vmkStatus = VMK_OK;
   struct NvmeDmaEntry *dmaEntry = NULL;
   void                *dmaBuf;
   vmk_uint32           processLen = 0;

   cmdInfo->type = ADMIN_CONTEXT;
   entry->header.cmdID = cmdInfo->cmdId;

   if (length == 0) {
      VMK_ASSERT(buf == NULL);
      Nvme_Memcpy64(&cmdInfo->nvmeCmd, entry, sizeof(*entry)/sizeof(vmk_uint64));
      return cmdInfo->prps;
   }

   Nvme_Memcpy64(&cmdInfo->nvmeCmd, entry, sizeof(*entry)/sizeof(vmk_uint64));

   if (VMK_LIKELY(length <= VMK_PAGE_SIZE)) {
      cmdInfo->nvmeCmd.header.prp[0].addr = cmdInfo->prpPhy;
      dmaBuf = cmdInfo->prps;
   } else {
      dmaEntry = Nvme_Alloc(sizeof(*dmaEntry), 0, NVME_ALLOC_ZEROED);
      if (dmaEntry == NULL) {
         EPRINT("Failed to allocate memory!");
         return NULL;
      }

      vmkStatus = OsLib_DmaAlloc(&ctrlr->ctrlOsResources, length, dmaEntry,
                                 VMK_TIMEOUT_UNLIMITED_MS);
      if (vmkStatus != VMK_OK) {
         EPRINT("Failed to allocate memory!");
         Nvme_Free(dmaEntry);
         return NULL;
      }

      dmaBuf = (void *)dmaEntry->va;

      vmkStatus = vmk_SgFindPosition(dmaEntry->sgOut, 0, &cmdInfo->sgPosition);
      VMK_ASSERT(vmkStatus == VMK_OK);

      cmdInfo->cmdBase = cmdInfo;
      cmdInfo->requiredLength= length;
      cmdInfo->requestedLength = 0;
      processLen = NvmeIo_ProcessPrps(&ctrlr->adminq, cmdInfo);
      VMK_ASSERT(length == processLen);
      cmdInfo->cmdBase = NULL;

      cmdInfo->cleanup = SendAdminCleanup;
      cmdInfo->cleanupData = dmaEntry;
   }

   if (VMK_UNLIKELY(GetAdminCmdDirection(entry) == XFER_TO_DEV)) {
      VMK_ASSERT(buf != NULL);
      vmk_Memcpy(dmaBuf, buf, length);
   }

   return dmaBuf;
}

/**
 * @brief This function Sends a sync Admin Command to controller.
 *
 * @param[in] ctrlr     pointer to nvme device context
 * @param[in] entry     pointer to NVME command entry
 * @param[in] buf       pointer to data buf transferred
 * @param[in] length    data transferred size
 * @param[in] cqEntry   pointer to NVME completion entry
 * @param[in] timeoutUs command timeout in micronseconds
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
VMK_ReturnStatus
NvmeCtrlrCmd_SendAdmin(struct NvmeCtrlr *ctrlr,
                       struct nvme_cmd *entry,
                       vmk_uint8 *buf,
                       vmk_uint32 length,
                       struct cq_entry *cqEntry,
                       int timeoutUs)
{
   VMK_ReturnStatus      vmkStatus = VMK_OK;
   Nvme_Status           nvmeStatus;
   struct NvmeQueueInfo *qinfo;
   struct NvmeCmdInfo   *cmdInfo;
   void                 *dmaBuf;

   if (length > ctrlr->maxXferLen && ctrlr->maxXferLen != 0) {
      EPRINT("Data size: %d exceeds the limitation: %d.", length, ctrlr->maxXferLen);
      return VMK_LIMIT_EXCEEDED;
   }

   qinfo = &ctrlr->adminq;

   LOCK_FUNC(qinfo);
   cmdInfo = NvmeCore_GetCmdInfo(qinfo);
   if (!cmdInfo) {
      UNLOCK_FUNC(qinfo);
      EPRINT("Failed to get cmdInfo, opc 0x%x", entry->header.opCode);
      return VMK_NO_RESOURCES;
   }
   UNLOCK_FUNC(qinfo);

   dmaBuf = PopulateAdminCmdInfo(ctrlr, cmdInfo, entry, buf, length);
   if (dmaBuf == NULL) {
      NvmeCore_PutCmdInfo(qinfo, cmdInfo);
      return VMK_NO_MEMORY;
   }

   if (cqEntry) {
      vmk_Memset(cqEntry, 0, sizeof(struct cq_entry));
   }

   DPRINT_ADMIN("Submitting admin cmd %p [%d], opc: 0x%x.",
                cmdInfo, cmdInfo->cmdId, entry->header.opCode);
#if NVME_DEBUG
   if (nvme_dbg & NVME_DEBUG_ADMIN) {
      NvmeDebug_DumpCmd(&cmdInfo->nvmeCmd);
   }
#endif

   nvmeStatus = NvmeCore_SubmitCommandWait(qinfo, cmdInfo, cqEntry, timeoutUs);

   DPRINT_ADMIN("Completed admin command %p [%d], opc: 0x%x, status:0x%x",
                cmdInfo, cmdInfo->cmdId, entry->header.opCode, nvmeStatus);

   if (nvmeStatus == NVME_STATUS_ABORTED) {
      // Command has been submitted to hardware, but timeout
      return VMK_TIMEOUT;
   }
   if (nvmeStatus != NVME_STATUS_SUCCESS) {
      // Command failed to be submitted to hardware
       vmkStatus = VMK_BUSY;
   } else {
      // Command has been submitted to hardware
      if (cmdInfo->cmdStatus == NVME_STATUS_SUCCESS) {
         // Command executed successfully
         if (GetAdminCmdDirection(entry) == XFER_FROM_DEV && length && buf) {
            vmk_Memcpy(buf, dmaBuf, length);
         }
         vmkStatus = VMK_OK;
      } else {
         // Command executed with error
         vmkStatus = VMK_FAILURE;
      }
#if NVME_DEBUG
      if (cqEntry && (nvme_dbg & NVME_DEBUG_ADMIN)) {
         NvmeDebug_DumpCpl(cqEntry);
      }
#endif
   }

   if (cmdInfo->cleanup) {
      cmdInfo->cleanup(qinfo, cmdInfo);
   }
   NvmeCore_PutCmdInfo(qinfo, cmdInfo);

   return vmkStatus;

}

/**
 * @brief This function Sends an async Admin Command to controller.
 *
 * @param[in] ctrlr    pointer to nvme device context
 * @param[in] entry    pointer to NVME command entry
 * @param[in] buf      pointer to data buf transferred
 * @param[in] length   data transferred size
 * @param[in] done     completion callback
 * @param[in] doneData data to completion callback
 *
 * @return returns VMK_OK if successfully submitting to hardware, otherwise Error Code
 */
VMK_ReturnStatus
NvmeCtrlrCmd_SendAdminAsync(struct NvmeCtrlr *ctrlr,
                            struct nvme_cmd *entry,
                            vmk_uint8 *buf,
                            vmk_uint32 length,
                            NvmeCore_CompleteCommandCb done,
                            void *doneData)
{
   VMK_ReturnStatus      vmkStatus;
   Nvme_Status           nvmeStatus;
   struct NvmeQueueInfo *qinfo;
   struct NvmeCmdInfo   *cmdInfo;
   void                 *dmaBuf;

   if (length > ctrlr->maxXferLen && ctrlr->maxXferLen != 0) {
      EPRINT("Data size: %d exceeds the limitation: %d.", length, ctrlr->maxXferLen);
      return VMK_LIMIT_EXCEEDED;
   }

   qinfo = &ctrlr->adminq;

   LOCK_FUNC(qinfo);
   cmdInfo = NvmeCore_GetCmdInfo(qinfo);
   if (!cmdInfo) {
      UNLOCK_FUNC(qinfo);
      EPRINT("Failed to get cmdInfo, opc 0x%x", entry->header.opCode);
      return VMK_NO_RESOURCES;
   }
   UNLOCK_FUNC(qinfo);

   dmaBuf = PopulateAdminCmdInfo(ctrlr, cmdInfo, entry, buf, length);
   if (dmaBuf == NULL) {
      NvmeCore_PutCmdInfo(qinfo, cmdInfo);
      return VMK_NO_MEMORY;
   }

   cmdInfo->doneData = doneData;

   DPRINT_ADMIN("Submitting async admin cmd %p [%d], opc: 0x%x.",
                cmdInfo, cmdInfo->cmdId, entry->header.opCode);
#if NVME_DEBUG
   if (nvme_dbg & NVME_DEBUG_ADMIN) {
      NvmeDebug_DumpCmd(&cmdInfo->nvmeCmd);
   }
#endif

   LOCK_FUNC(qinfo);
   nvmeStatus = NvmeCore_SubmitCommandAsync(qinfo, cmdInfo, done);
   UNLOCK_FUNC(qinfo);
   if (!SUCCEEDED(nvmeStatus)) {
      vmkStatus = VMK_FAILURE;
   } else {
      DPRINT_ADMIN("Submitted async admin cmd [%d], opc: 0x%x.",
                   entry->header.cmdID, entry->header.opCode);
      vmkStatus = VMK_OK;
   }

   return vmkStatus;
}

/**
 * @brief This function Retrieves controller/Namespace identify data.
 *
 *
 * @param[in] ctrlr         pointer to nvme device context
 * @param[in] nsId          namespace ID
 * @param[in] ctrlrId       controller ID
 * @param[in] identifyData  identify data buffer
 *
 * @return returns VMK_OK if identify command executed successfully, otherwise Error Code
 */
VMK_ReturnStatus
NvmeCtrlrCmd_Identify(struct NvmeCtrlr *ctrlr,
                      int cns,
                      int ctrlrId,
                      int nsId,
                      vmk_uint8 *identifyData)
{
   struct nvme_cmd entry;
   struct cq_entry cqEntry;

   DPRINT_ADMIN("Identify cns: %d, ctrlrId: %d, nsId：%d", cns, ctrlrId, nsId);

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.opCode = NVM_ADMIN_CMD_IDENTIFY;
   entry.cmd.identify.controllerStructure = cns;
   entry.cmd.identify.cntId = ctrlrId;
   entry.header.namespaceID = nsId;

   return NvmeCtrlrCmd_SendAdmin(ctrlr, &entry, identifyData, VMK_PAGE_SIZE, &cqEntry,
                                 ADMIN_TIMEOUT);
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

   DPRINT_ADMIN("Delete submission qid: %d.", id);

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.opCode             = NVM_ADMIN_CMD_DEL_SQ;
   entry.cmd.deleteSubQ.identifier = id;
   return NvmeCtrlrCmd_SendAdmin(ctrlr, &entry, NULL, 0, NULL, ADMIN_TIMEOUT);
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

   DPRINT_ADMIN("Delete completion qid: %d.", id);

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.opCode             = NVM_ADMIN_CMD_DEL_CQ;
   entry.cmd.deleteCplQ.identifier = id;
   return NvmeCtrlrCmd_SendAdmin(ctrlr, &entry, NULL, 0, NULL, ADMIN_TIMEOUT);
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
NvmeCtrlrCmd_CreateCq(struct NvmeCtrlr *ctrlr,
                      struct NvmeQueueInfo *qinfo,
                      vmk_uint16 qid)
{
   struct nvme_cmd entry;

   DPRINT_ADMIN("Create completion qid: %d.", qid);

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.opCode                  = NVM_ADMIN_CMD_CREATE_CQ;
   entry.header.prp[0].addr             = qinfo->compqPhy;
   entry.cmd.createCplQ.identifier      = qid;
   entry.cmd.createCplQ.size            = qinfo->qsize - 1;
   entry.cmd.createCplQ.contiguous      = 1;
   entry.cmd.createCplQ.interruptEnable = 1;
   entry.cmd.createCplQ.interruptVector = qinfo->intrIndex;

   return NvmeCtrlrCmd_SendAdmin(ctrlr, &entry, NULL, 0, NULL, ADMIN_TIMEOUT);
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
NvmeCtrlrCmd_CreateSq(struct NvmeCtrlr *ctrlr,
                      struct NvmeQueueInfo *qinfo,
                      vmk_uint16 qid)
{
   struct nvme_cmd entry;

   DPRINT_ADMIN("Create submission qid: %d.", qid);

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.opCode                    = NVM_ADMIN_CMD_CREATE_SQ;
   entry.header.prp[0].addr               = qinfo->subQueue->subqPhy;
   entry.cmd.createSubQ.identifier        = qid;
   entry.cmd.createSubQ.size              = qinfo->subQueue->qsize - 1;
   entry.cmd.createSubQ.contiguous        = 1;
   entry.cmd.createSubQ.priority          = 0;  /** High */
   entry.cmd.createSubQ.completionQueueID = qinfo->id;

   return NvmeCtrlrCmd_SendAdmin(ctrlr, &entry, NULL, 0, NULL, ADMIN_TIMEOUT);
}

/**
 * @brief This function sends a sync set feature command
 * 
 * @param[in]  ctrlr   pointer to nvme device context
 * @param[in]  nsId    namespace ID
 * @param[in]  feature feature ID
 * @param[in]  option  feature option
 * @param[out] buf     pointer to data buf transferred
 * @param[in]  length  data transferred size
 * @param[in]  cqEntry pointer to completion entry
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
VMK_ReturnStatus
NvmeCtrlrCmd_SetFeature(struct NvmeCtrlr *ctrlr,
                        int nsId,
                        vmk_uint16 featureId,
                        vmk_uint32 option,
                        vmk_uint8 *buf,
                        vmk_uint32 length,
                        struct cq_entry *cqEntry)
{
   struct nvme_cmd entry;

   DPRINT_ADMIN("Set feature sync: Fid: 0x%x, nsid: %d, option 0x%08x",
                featureId, nsId, option);

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.opCode             = NVM_ADMIN_CMD_SET_FEATURES;
   entry.header.namespaceID        = nsId;
   entry.cmd.setFeatures.featureID = featureId;
   entry.cmd.asUlong[1]            = option;
   return NvmeCtrlrCmd_SendAdmin(ctrlr, &entry, buf, length, cqEntry, ADMIN_TIMEOUT);
}

/**
 * @brief This function sends an async set feature command
 * 
 * @param[in] ctrlr    pointer to nvme device context
 * @param[in] nsId     namespace ID
 * @param[in] feature  feature ID
 * @param[in] option   feature option
 * @param[in] done     completion callback
 * @param[in] doneData data to completion callback
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
VMK_ReturnStatus
NvmeCtrlrCmd_SetFeatureAsync(struct NvmeCtrlr *ctrlr,
                            int nsId,
                            vmk_uint16 featureId,
                            vmk_uint32 option,
                            NvmeCore_CompleteCommandCb done,
                            void *doneData)
{
   struct nvme_cmd entry;

   DPRINT_ADMIN("Set feature async: Fid: 0x%x, nsid: %d, option 0x%08x",
                featureId, nsId, option);

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.opCode             = NVM_ADMIN_CMD_SET_FEATURES;
   entry.header.namespaceID        = nsId;
   entry.cmd.setFeatures.featureID = featureId;
   entry.cmd.asUlong[1]            = option;

   return NvmeCtrlrCmd_SendAdminAsync(ctrlr, &entry, NULL, 0, done, doneData);
}

/**
 * @brief This function sends a sync get feature command
 *
 * @param[in]  ctrlr   pointer to nvme device context
 * @param[in]  nsId    namespace ID
 * @param[in]  feature feature ID
 * @param[in]  option  feature option
 * @param[out] buf     pointer to data buf transferred
 * @param[in]  length  data transferred size
 * @param[in]  cqEntry pointer to completion entry
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
VMK_ReturnStatus
NvmeCtrlrCmd_GetFeature(struct NvmeCtrlr *ctrlr,
                        int nsId,
                        vmk_uint16 featureId,
                        vmk_uint32 option,
                        vmk_uint8 *buf,
                        vmk_uint32 length,
                        struct cq_entry *cqEntry)
{
   struct nvme_cmd entry;

   DPRINT_ADMIN("Get feature sync: Fid: 0x%x, nsid: %d, option 0x%08x",
                featureId, nsId, option);

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.opCode             = NVM_ADMIN_CMD_GET_FEATURES;
   entry.header.namespaceID        = nsId;
   entry.cmd.getFeatures.featureID = featureId;
   entry.cmd.asUlong[1]            = option;
   return NvmeCtrlrCmd_SendAdmin(ctrlr, &entry, buf, length, cqEntry, ADMIN_TIMEOUT);
}


/**
 * @brief This function sends an async get feature command
 * 
 * @param[in] ctrlr    pointer to nvme device context
 * @param[in] nsId     namespace ID
 * @param[in] feature  feature ID
 * @param[in] option   feature option
 * @param[in] done     completion callback
 * @param[in] doneData data to completion callback
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */

VMK_ReturnStatus
NvmeCtrlrCmd_GetFeatureAsync(struct NvmeCtrlr *ctrlr,
                             int nsId,
                             vmk_uint16 featureId,
                             vmk_uint32 option,
                             NvmeCore_CompleteCommandCb done,
                             void *doneData)
{
   struct nvme_cmd entry;

   DPRINT_ADMIN("Get feature async: Fid: 0x%x, nsid: %d, option 0x%08x",
                featureId, nsId, option);

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.opCode             = NVM_ADMIN_CMD_GET_FEATURES;
   entry.header.namespaceID        = nsId;
   entry.cmd.getFeatures.featureID = featureId;
   entry.cmd.asUlong[1]            = option;

   return NvmeCtrlrCmd_SendAdminAsync(ctrlr, &entry, NULL, 0, done, doneData);
}

/**
 * @brief This function sends a sync request to retrieve a log page
 *
 * @param[in]  ctrlr     pointer to nvme device context
 * @param[in]  nsID      namespace ID
 * @param[in]  logPageID Log Page Identifier
 * @param[out] logPage   pointer to memory used for copying log page data
 * @param[in]  length    data transferred size
 *
 * @return This function returns vmk_OK if successful, otherwise Error Code
 *
 */
VMK_ReturnStatus
NvmeCtrlrCmd_GetLogPage(struct NvmeCtrlr *ctrlr,
                        vmk_uint32 nsID,
                        vmk_uint16 logPageID,
                        void* logPage,
                        vmk_uint32 length)
{
   struct nvme_cmd	entry;

   DPRINT_ADMIN("Get log page sync: nsId: %d, pageId: 0x%x", nsID, logPageID);

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.opCode = NVM_ADMIN_CMD_GET_LOG_PAGE;
   entry.header.namespaceID = nsID;
   entry.cmd.getLogPage.LogPageID = logPageID & 0xFFFF;
   entry.cmd.getLogPage.numDW = (length/sizeof(vmk_uint32)-1);

   return NvmeCtrlrCmd_SendAdmin(ctrlr, &entry, logPage, length, NULL, ADMIN_TIMEOUT);
}

/**
 * @brief This function sends an sync request to retrieve a log page
 *
 * @param[in] ctrlr     pointer to nvme device context
 * @param[in] nsID      namespace ID
 * @param[in] logPageID Log Page Identifier
 * @param[in] length    data transferred size
 * @param[in] done      completion callback
 * @param[in] doneData  data to completion callback
 *
 * @return This function returns vmk_OK if successful, otherwise Error Code
 */

VMK_ReturnStatus
NvmeCtrlrCmd_GetLogPageAsync(struct NvmeCtrlr *ctrlr,
                             vmk_uint32 nsID,
                             vmk_uint16 logPageID,
                             vmk_uint32 length,
                             NvmeCore_CompleteCommandCb done,
                             void *doneData)
{
   struct nvme_cmd entry;

   DPRINT_ADMIN("Get log page async: nsId: %d, pageId: 0x%x", nsID, logPageID);

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.opCode = NVM_ADMIN_CMD_GET_LOG_PAGE;
   entry.header.namespaceID = nsID;
   entry.cmd.getLogPage.LogPageID = logPageID & 0xFFFF;
   entry.cmd.getLogPage.numDW = (length/sizeof(vmk_uint32)-1);

   return NvmeCtrlrCmd_SendAdminAsync(ctrlr, &entry, NULL, length, done, doneData);
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

   DPRINT_CTRLR("buffer: %s, nul: %d size: %d", buffer, nulFound, bufferSize);

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

   vmkStatus = NvmeCtrlrCmd_Identify(ctrlr, IDENTIFY_CONTROLLER, 0, 0,
                                     (vmk_uint8 *)&ctrlr->identify);
   if (vmkStatus != VMK_OK) {
      EPRINT("Failed to get controller identify data.");
      return vmkStatus;
   }

   /* Now we have completed IDENTIFY command, update controller
    * parameters based on IDENTIFY result.
    */
   ctrlr->admVendCmdCfg   = ctrlr->identify.admVendCmdCfg;
   ctrlr->nvmVendCmdCfg   = ctrlr->identify.nvmVendCmdCfg;
   ctrlr->nvmCacheSupport = ctrlr->identify.volWrCache;
   ctrlr->nvmCmdSupport   = ctrlr->identify.cmdSupt;
   ctrlr->logPageAttr     = ctrlr->identify.logPgAttrib;
   ctrlr->pcieVID         = ctrlr->identify.pcieVID;
   ctrlr->ctrlrId         = ctrlr->identify.cntlId;

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

   if (ctrlr->identify.numNmspc > NVME_MAX_NAMESPACE_PER_CONTROLLER) {
      WPRINT("identify.nn %d exceeds to driver's capability %d, "
             "only the first %d namespaces supported",
             ctrlr->identify.numNmspc,
             NVME_MAX_NAMESPACE_PER_CONTROLLER,
             NVME_MAX_NAMESPACE_PER_CONTROLLER);
      ctrlr->nn = NVME_MAX_NAMESPACE_PER_CONTROLLER;
   } else {
      ctrlr->nn = ctrlr->identify.numNmspc;
   }

   DPRINT_CTRLR("Controller: %s.", Nvme_GetCtrlrName(ctrlr));
   DPRINT_CTRLR("Serial no: %s.", ctrlr->serial);
   DPRINT_CTRLR("Model no: %s.", ctrlr->model);
   DPRINT_CTRLR("Firmware revision: %s.", ctrlr->firmwareRev);

   DPRINT_CTRLR("Admin Cmd Vendor Cfg: 0x%x.", ctrlr->admVendCmdCfg);
   DPRINT_CTRLR("NVM Cmd Vendor Cfg: 0x%x.", ctrlr->nvmVendCmdCfg);
   DPRINT_CTRLR("Max Number of Namespaces: %d.", ctrlr->nn);

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

   DPRINT_CTRLR("setting intr coalescing feature %d %d",
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
   DPRINT_CTRLR("returned param 0x%x", param);
   if (((param >> 8) != intr_coalescing_time) ||
         ((param & 0xFF) != intr_coalescing_threshold)) {
       EPRINT("Param validation error returned value 0x%x", param);
       result = -EINVAL;
   }
   return (result);
#endif
   EPRINT("Not implemented.");
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
   vmk_uint16 allocNum;

   DPRINT_Q("attempting to allocate [%d] IO queues", *nrIoQueues);

   do {
      vmkStatus = NvmeCtrlrCmd_SetFeature(ctrlr, 0, FTR_ID_NUM_QUEUE,
                                          ((*nrIoQueues - 1) << 16) | (*nrIoQueues - 1),
                                          NULL, 0, &cqEntry);

      if (vmkStatus != VMK_OK) {
         EPRINT("Failed requesting nr_io_queues 0x%x", cqEntry.SC);
         if (*nrIoQueues == 0) {
            break;
         }

         *nrIoQueues = 0;
      }
   } while(vmkStatus != VMK_OK);

   if (vmkStatus == VMK_OK) {
      allocNum = cqEntry.param.numCplQAlloc + 1;
      DPRINT_Q("Allocated [%d] IO queues", allocNum);
      if (allocNum < *nrIoQueues) {
         WPRINT("Number of IO queues allocated [%d] is less than requested [%d],"
                " set IO queue number as %d.", allocNum, *nrIoQueues, allocNum);
         *nrIoQueues = allocNum;
      }
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
   VMK_ASSERT(vmk_AtomicRead64(&ns->refCount) == 0);
   DPRINT_NS("Releasing Namespace [%d] %p", ns->id, ns);
   OsLib_LockDestroy(&ns->lock);
   vmk_ListRemove(&ns->list);
   vmk_AtomicDec32(&ctrlr->nsCount);
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
   char propName[VMK_MISC_NAME_MAX];

   ns = Nvme_Alloc(sizeof(*ns), 0, NVME_ALLOC_ZEROED);
   if (!ns) {
       EPRINT("Failed NS memory allocation.");
       return (NULL);
   }

   ident = Nvme_Alloc(sizeof(*ident), 0, NVME_ALLOC_ZEROED);
   if (!ident) {
       EPRINT("Failed ident memory allocation.");
       goto free_ns;
   }

   vmkStatus = NvmeCtrlrCmd_Identify(ctrlr, IDENTIFY_NAMESPACE, 0, nsId,
                                     (vmk_uint8 *)ident);
   if (vmkStatus != VMK_OK) {
       EPRINT("Failed get NS Identify data.");
       goto free_ident;
   }

   DPRINT_NS("NS [%d], size %lu, lba_fmt 0x%02x, Formats 0x%02x",
            nsId, ident->size, ident->fmtLbaSize, ident->numLbaFmt);
   DPRINT_NS("NS [%d], feature 0x%02x, Prot Cap 0x%02x, Prot Set 0x%02x",
            nsId, ident->feat, ident->dataProtCap, ident->dataProtSet);

   for (i = 0; i <= ident->numLbaFmt; i++) {
      DPRINT_NS("supported LBA format 0x%08x",
               *(vmk_uint32 *)&ident->lbaFmtSup[i]);
   }
   lba_format = *(vmk_uint32 *)&ident->lbaFmtSup[ident->fmtLbaSize & 0x0F];
   DPRINT_NS("LBA format 0x%08x", lba_format);
   DPRINT_NS("Meta Data Capability 0x%02x", ident->metaDataCap);
   DPRINT_NS("LBA Data Prot Cap/Set 0x%02x/0x%02x",
            ident->dataProtCap, ident->dataProtSet);

   vmk_StringFormat(propName, VMK_MISC_NAME_MAX, NULL, "nvmeNs-%s-%d",
                    Nvme_GetCtrlrName(ctrlr), nsId);
   vmkStatus = OsLib_LockCreate(&ctrlr->ctrlOsResources, NVME_LOCK_RANK_MEDIUM,
                                propName, &ns->lock);
   if (vmkStatus != VMK_OK) {
       EPRINT("Failed NS lock creation.");
       goto free_ident;
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
   ns->metaDataCap = ident->metaDataCap;
   ns->ctrlr       = ctrlr;

   ns->eui64       = ident->eui64;
   vmk_Memcpy(ns->nguid, &ident->nguid, 16);

   DPRINT_NS("NS [%d] %p, adding to dev list %p, lba size %u",
             ns->id, ns, &ctrlr->nsList, (1 << ns->lbaShift));
   vmk_SpinlockLock(ctrlr->lock);
   vmk_ListInsert(&ns->list, vmk_ListAtRear(&ctrlr->nsList));
   vmk_SpinlockUnlock(ctrlr->lock);

   /* Mark ns as ONLINE by default */
   ns->flags |= NS_ONLINE;

   /* Initially set ref count to 0 */
   vmk_AtomicWrite64(&ns->refCount, 0);

   Nvme_Free(ident);

   vmk_AtomicInc32(&ctrlr->nsCount);
   return (ns);

free_ident:
   Nvme_Free(ident);

free_ns:
   Nvme_Free(ns);

   return NULL;
}


vmk_uint64
NvmeCtrlr_GetNs(struct NvmeNsInfo *ns)
{
   vmk_uint64 rc;
   rc = vmk_AtomicReadInc64(&ns->refCount) + 1;
#if NVME_DEBUG
   DPRINT_NS("ns %d refCount increased to %ld.", ns->id, rc);
#endif
   return rc;
}

vmk_uint64
NvmeCtrlr_PutNs(struct NvmeNsInfo *ns)
{
   vmk_uint64 rc;

   rc = vmk_AtomicReadDec64(&ns->refCount) - 1;

#if NVME_DEBUG
   DPRINT_NS("ns %d refCount decreased to %ld.", ns->id, rc);
#endif

   return rc;
}

static inline vmk_Bool
SupportNsMgmtAttach(struct NvmeCtrlr *ctrlr)
{
   return (ctrlr->identify.adminCmdSup & 0x8);
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
   int i, n = 0;
   VMK_ReturnStatus vmkStatus = VMK_OK;
   struct NvmeNsInfo *ns;
   struct ns_list *nsList = NULL;

   /**
    * For number of Namespaces discovered:
    *
    * a. get Namespace identify data
    * b. Create a block device queue
    * c. create a disk device.
    * d. add Namespace to list of devices
    */
   if (SupportNsMgmtAttach(ctrlr)) {
      nsList = Nvme_Alloc(sizeof(*nsList), 0, NVME_ALLOC_ZEROED);
      if (nsList == NULL) {
         EPRINT("Failed to allocate memory.");
         return VMK_NO_MEMORY;
      }

      vmkStatus = NvmeCtrlrCmd_Identify(ctrlr, ACTIVE_NAMESPACE_LIST, 0, 0,
                                        (vmk_uint8 *)nsList);
      if (vmkStatus != VMK_OK) {
         EPRINT("Failed to get attached namespace list, status: %x", vmkStatus);
         goto out;
      }

      for (i = 0; i < ctrlr->nn; i++) {
         if (nsList->nsId[i] == 0) {
            break;
         }

         DPRINT_NS("allocating Namespace %d", nsList->nsId[i]);
         ns = NvmeCtrlr_AllocNs(ctrlr, nsList->nsId[i]);
         if (!ns) {
            EPRINT("Failed to allocate NS [%d] information structure.", nsList->nsId[i]);
            continue;
         }
         n ++;
      }

      DPRINT_NS("Attached Namespace number: %d", n);
   } else {
      for (i = 1; i <= ctrlr->nn; i++) {
         DPRINT_NS("allocating Namespace %d", i);
         ns = NvmeCtrlr_AllocNs(ctrlr, i);
         if (!ns) {
            EPRINT("Failed to allocate NS [%d] information structure.", i);
            continue;
         }
         n ++;
      }
   }

out:
   if (nsList != NULL) {
      Nvme_Free(nsList);
   }
   return vmkStatus;
}

VMK_ReturnStatus
NvmeCtrlr_UpdateNsList(struct NvmeCtrlr *ctrlr, int sel, vmk_uint32 nsId)
{
   VMK_ReturnStatus   vmkStatus = VMK_OK;
   Nvme_Status        nvmeStatus = NVME_STATUS_SUCCESS;
   struct ns_list    *nsList;
   struct NvmeNsInfo *ns = NULL;
   vmk_ListLinks     *itemPtr, *nextPtr;
   int                i, num;
   vmk_Bool           found = VMK_FALSE;

   nsList = Nvme_Alloc(sizeof(*nsList), 0, NVME_ALLOC_ZEROED);
   if (nsList == NULL) {
      EPRINT("Failed to allocate memory.");
      return VMK_NO_MEMORY;
   }

   vmkStatus = NvmeCtrlrCmd_Identify(ctrlr, ACTIVE_NAMESPACE_LIST, 0, 0,
                                     (vmk_uint8 *)nsList);
   if (vmkStatus != VMK_OK) {
      EPRINT("Failed to get attached namespace list, status: %x", vmkStatus);
      goto out;
   }

   for (i = 0; i < ctrlr->nn; i++) {
      if (nsList->nsId[i] == 0) {
         break;
      }
      if (nsList->nsId[i] == nsId) {
         found = VMK_TRUE;
      }
   }

   num = i;
   DPRINT_NS("num: %d, nsCount: %d, found: %d\n",
             num, vmk_AtomicRead32(&ctrlr->nsCount), (int)found);

   if (sel == NS_ATTACH) {
      VMK_ASSERT(num == vmk_AtomicRead32(&ctrlr->nsCount) + 1 && found);
      ns = NvmeCtrlr_AllocNs(ctrlr, nsId);
      if (!ns) {
         EPRINT("Failed to allocate NS information structure.");
         vmkStatus =  VMK_NO_MEMORY;
         goto out;
      }
      nvmeStatus = NvmeCore_SetNamespaceOnline(ctrlr, VMK_TRUE, nsId);
      if (nvmeStatus != NVME_STATUS_SUCCESS) {
         EPRINT("Failed to set the namespace %d online.", nsId);
         vmkStatus = VMK_FAILURE;
         goto out;
      }
   } else {
      VMK_ASSERT(num == vmk_AtomicRead32(&ctrlr->nsCount) - 1 && !found);

      found = VMK_FALSE;
      vmk_SpinlockLock(ctrlr->lock);
      VMK_LIST_FORALL_SAFE(&ctrlr->nsList, itemPtr, nextPtr) {
         ns = VMK_LIST_ENTRY(itemPtr, struct NvmeNsInfo, list);
         if (ns->id == nsId) {
            found = VMK_TRUE;
            break;
         }
      }
      /*
       * Before this function is called, this path should be unclaimed, and the
       * namespace is set as offline by user world tool, which means, the path
       * has already been destroied.
       */
      VMK_ASSERT(found);
      if (found) {
         NvmeCtrlr_FreeNs(ctrlr, ns);
      }
      vmk_SpinlockUnlock(ctrlr->lock);
   }

out:
   Nvme_Free(nsList);
   return vmkStatus;
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

   VMK_LIST_FORALL_SAFE(&ctrlr->nsList, itemPtr, nextPtr) {
      ns = VMK_LIST_ENTRY(itemPtr, struct NvmeNsInfo, list);
      NvmeCtrlr_FreeNs(ctrlr, ns);
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
   LOCK_FUNC(qinfo);
   cmdInfo = NvmeCore_GetCmdInfo(qinfo);
   if (!cmdInfo) {
      UNLOCK_FUNC(qinfo);
      return VMK_NO_RESOURCES;
   }
   UNLOCK_FUNC(qinfo);

   cmdInfo->cmdPtr = NULL;
   cmdInfo->cmdCount = 0;
   cmdInfo->ns = ns;
   cmd = &cmdInfo->nvmeCmd;

   cmd->header.opCode = NVM_CMD_READ;
   cmd->header.prp[0].addr = cmdInfo->prpPhy;
   cmd->header.prp[1].addr = 0;
   cmd->header.namespaceID = ns->id;
   cmd->header.cmdID = cmdInfo->cmdId;
#if USE_TIMER
   cmdInfo->timeoutId = ctrlr->timeoutId;
#endif
   cmdInfo->doneData = NULL;
   cmd->cmd.read.numLBA = 0;
   cmd->cmd.read.startLBA = 0;
   if (END2END_DPS_TYPE(ns->dataProtSet)) {
      cmd->cmd.read.protInfo = 0x8;
   }

   cmdInfo->type = BIO_CONTEXT;

   timeout = 1 * 1000 * 1000; /* 1 second in microseconds */
   DPRINT_CMD("issue read to fw");
   nvmeStatus = NvmeCore_SubmitCommandWait(qinfo, cmdInfo, NULL, timeout);

   /*(1) Theoretically, nvmeStatus should reflect whether the command is truly completed.
    * If not, sleep 1 second before issuing next command to avoid high CPU utilization.
    *(2) There is a minor possibility that the command times out due to fw problem, in this case,
    * the command will be marked with ABORT_CONTEXT and handled in processCq routine. Since at most
    * 60 commands will be issued, the submission queue will not get overwhelmed given that its size is 1024*/
   if(!SUCCEEDED(nvmeStatus)) {
      if (nvmeStatus != NVME_STATUS_ABORTED) {
         // Command failed to be submitted to hardwware
         NvmeCore_PutCmdInfo(qinfo, cmdInfo);
      }
      DPRINT_CMD("read fails, sleep 1s");
      vmk_WorldSleep(timeout);
      DPRINT_CMD("sleep finished");
   } else {
      NvmeCore_PutCmdInfo(qinfo, cmdInfo);
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
      EPRINT("IOqueue not ready: %d", ctrlr->numIoQueues);
      return  NVME_STATUS_FAILURE;
   }

   /*Use the 1st IO queue*/
   qinfo = &ctrlr->ioq[0];

   validNs = VMK_FALSE;
   /*Use the 1st namespace whose size > 0*/
   if(ctrlr->nn > 0) {
      /*issue cmd to the first useable namespace */
      VMK_LIST_FORALL_SAFE(&ctrlr->nsList, itemPtr, nextPtr) {
         ns = VMK_LIST_ENTRY(itemPtr, struct NvmeNsInfo, list);
         if (NvmeCore_ValidateNs(ns) == VMK_OK) {
            DPRINT_NS("check device status with namespace %d", ns->id);
            validNs = VMK_TRUE;
            break;
         }
      }
   }
   else {
      VPRINT("nn = 0, no need to check IO, return success");
      return  NVME_STATUS_SUCCESS;
   }

   if(validNs == VMK_FALSE) {
      VPRINT("All namespaces are invalid, no need to check IO, return success");
      return  NVME_STATUS_SUCCESS;
   }

   /*keep probing the device until it is ready. If being not ready for more than 60 seconds, quit.*/
   waitDuration = 60 * 1000 * 1000; /*60s in ms*/
   timeout = OsLib_GetTimerUs() + waitDuration;
   do {
      nvmeStatus = NvmeCtrlr_CheckIOFunction(ns, qinfo);
      DPRINT_CTRLR("check IO function status 0x%x, %s", nvmeStatus, NvmeCore_StatusToString(nvmeStatus));

      /**
       * Check device whether device is physically removed.
       * If yes, we should return immediately. See PR 1568844.
       **/
      if (NvmeCore_IsCtrlrRemoved(ctrlr) == VMK_TRUE) {
          WPRINT("device is missing.");
          NvmeCtrlr_SetMissing(ctrlr);
          return NVME_STATUS_FAILURE;
      }

      if(!OsLib_TimeAfter(OsLib_GetTimerUs(), timeout)) {
         EPRINT("device not ready after 60 seconds, quit");
         nvmeStatus = NVME_STATUS_FAILURE;
         break;
      }
   } while(!SUCCEEDED(nvmeStatus));

   DPRINT_CTRLR("need %ld ms to bring up the device.", (OsLib_GetTimerUs() - timeout + waitDuration));
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
   vmk_uint64       hwCap = 0;
   vmk_uint32       minPage = 0;
   vmk_uint32       maxSize = VMK_UINT32_MAX;

   DPRINT_CTRLR("NvmeCtrlr_Start");


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

   /* Initialize max transfer length of controller */
   DPRINT_CTRLR("mdts: %d\n", ctrlr->identify.mdts);
   if (ctrlr->identify.mdts != 0) {
      hwCap = Nvme_Readq(ctrlr->regs + NVME_CAP);
      minPage = (1 << (((hwCap & NVME_CAP_MPSMIN_MSK64) >> NVME_CAP_MPSMIN_LSB) + 12));
      maxSize = (1 << ctrlr->identify.mdts) * minPage;
   }
   ctrlr->maxXferLen = min_t(vmk_uint32, transfer_size * 1024, maxSize);
   DPRINT_CTRLR("maxXferLen: %d\n", ctrlr->maxXferLen);

   if (NvmeCtrlr_IsIntelStripeLimit(ctrlr)) {
      /**
       * According to Intel, the stripe size could be retrieved from byte 3075
       * in controller identify page.
       */
      ctrlr->stripeSize = (1 << ctrlr->identify.reservedF[3]) * minPage;
      DPRINT_CTRLR("stripeSize: %d\n", ctrlr->stripeSize);
   }

#if ASYNC_EVENTS_ENABLED
   NvmeExc_RegisterForEvents (ctrlr);
#endif
   /*
    * We should be able to allocate an IO queue with a unique
    * IRQ vector per SCSI completion queue. The number of SCSI
    * completion queues available is provided by PSA.
    */
   DPRINT_Q("Requesting %d IO queues.", ctrlr->numIoQueues);
   nrIoQueues = ctrlr->numIoQueues;

   /*
    * We should have allocated enough MSI-x vectors already, if
    * not, fallback to use just one IO queue
    */
   if (!ctrlr->ctrlOsResources.msixEnabled || ctrlr->ctrlOsResources.numVectors < (nrIoQueues + 1)) {
      VPRINT("Insufficient resources, using single IO queue.");
      nrIoQueues = 1;
   }

   /*
    * Determine number of queues required for optimum performance.
    */
   vmkStatus = NvmeCtrlr_RequestIoQueues(ctrlr, &nrIoQueues);
   if (vmkStatus != VMK_OK) {
      EPRINT("Failed to allocate hardware IO queues.");
      goto stop_hw;
   }
   DPRINT_Q("Got %d HW IO queues.", nrIoQueues);
   ctrlr->numIoQueues = nrIoQueues;

#if NVME_MUL_COMPL_WORLD
   vmkStatus = OsLib_StartCompletionWorlds(ctrlr);
   if (vmkStatus != VMK_OK) {
      EPRINT("Failed to create completion worlds. vmkStatus: 0x%x.",  \
            vmkStatus);
      goto stop_hw;

   }
#endif

   /*
    * Allocate IO queue information blocks, required DMA resources
    * and register IO queues with controller.
    */
   vmkStatus = NvmeCtrlr_CreateIoQueues(ctrlr);
   if (vmkStatus != VMK_OK) {
      EPRINT("Failed to allocate IO queues, 0x%x.", vmkStatus);
      goto cleanup_compl_worlds;
   }

   /*
    * Setup controller features according to current device parameters.
    */
   vmkStatus = NvmeCtrlr_IntrCoalescing(ctrlr);
   if (vmkStatus != VMK_OK) {
      EPRINT("Failed to set features, 0x%x.", vmkStatus);
   }

   /**
    * Allocate Namespace control blocks, create disk devices
    * and register block device interface.
    */
   vmk_ListInit(&ctrlr->nsList);
   NvmeCtrlr_AllocDisks(ctrlr);

   /*check if IO is ready to function for this controller*/
   if (NvmeCtrlr_WaitDeviceReady(ctrlr) != NVME_STATUS_SUCCESS) {
      EPRINT("The device can not be operational.");
      NvmeCtrlr_Stop(ctrlr);
      return VMK_NOT_READY;
   }

   /*
    * Device is now operational.
    */
   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_STARTED);

   return VMK_OK;

cleanup_compl_worlds:
#if NVME_MUL_COMPL_WORLD
   OsLib_EndCompletionWorlds(ctrlr);
#endif

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
   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_MISSING);
}


static void
NvmeCtrlr_SuspendAdminQueue(struct NvmeCtrlr *ctrlr)
{
   /**
    * TODO: pick a correct timeoutId when doing suspend
    */
   NvmeCore_SuspendQueue(&ctrlr->adminq);
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

   DPRINT_CMD("device %p [%s], suspending %d queues",
            ctrlr, Nvme_GetCtrlrName(ctrlr), ctrlr->numIoQueues);

   for( i = 1; i <= ctrlr->numIoQueues; i++ ) {
      qinfo = &ctrlr->ioq[i - 1];
      NvmeCore_SuspendQueue(qinfo);
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

   DPRINT_CMD("device %p [%s], resuming %d queues",
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

   DPRINT_CMD("device %p [%s], resetting %d queues",
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
#if ENABLE_REISSUE == 0
   int id;
#endif

   qinfo = &ctrlr->adminq;
   NvmeCore_FlushQueue(qinfo, NULL, INVALID_TIMESLOT, NVME_STATUS_IN_RESET , VMK_FALSE);
#if ((ENABLE_REISSUE == 0) && (USE_TIMER))
   /**
    * Reinitialize the timeout values. This is done outside the lock since
    * the queue is disabled and no IOs should affect these fields
    */
   for (id = 0; id < NVME_IO_TIMEOUT; id ++) {
      qinfo->timeoutCount[id] = 0;
      vmk_AtomicWrite32(&qinfo->timeoutComplCount[id], 0);
   }
#endif
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
NvmeCtrlr_FlushIoQueues(struct NvmeCtrlr *ctrlr, struct NvmeNsInfo* ns, int status, vmk_Bool doReissue)
{
   struct NvmeQueueInfo *qinfo;
   int   i;
#if ENABLE_REISSUE == 0
   int   id;
#endif

   DPRINT_Q("device %p [%s], flushing %d queues",
            ctrlr, Nvme_GetCtrlrName(ctrlr), ctrlr->numIoQueues);

   for(i = 1; i <= ctrlr->numIoQueues; i++ ) {
      qinfo = &ctrlr->ioq[i - 1];

      DPRINT_Q("qinfo %p [%d], nr_act %d", qinfo, qinfo->id,
               qinfo->nrAct - qinfo->pendingCmdFree.freeListLength);
#if USE_TIMER
      NvmeCore_FlushQueue(qinfo, ns, ctrlr->timeoutId, status, doReissue);
#else
      NvmeCore_FlushQueue(qinfo, ns, 0, status, doReissue);
#endif
#if ((ENABLE_REISSUE == 0) && (USE_TIMER))
      /**
       * Reinitialize the timeout values. This is done outside the lock since
       * the queue is disabled and no IOs should affect these fields
       */
      for (id = 0; id < NVME_IO_TIMEOUT; id ++) {
         qinfo->timeoutCount[id] = 0;
         vmk_AtomicWrite32(&qinfo->timeoutComplCount[id], 0);
      }
#endif
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
         EPRINT("Failed to destroy hardware IO submission queue %d",
                        sqinfo->id);
      }
      if (NvmeCtrlrCmd_DeleteCq(ctrlr, qinfo->id)) {
         EPRINT("Failed to destroy hardware IO completion queue %d",
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
       EPRINT("Failed to create hardware IO completion queue %d",
                        qinfo->id);
       goto err_out;
   }

   result = NvmeCtrlrCmd_CreateSq(ctrlr, qinfo, sqinfo->id);
   if (result) {
       EPRINT("Failed to create hardware IO submission queue %d",
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

   DPRINT_Q("Restarting io queue %p[%d].", qinfo, qinfo->id);
   /* TODO: Do we need to grab the queue lock here? */
   // qinfo->lockFunc(qinfo->lock);
   result = NvmeQueue_ResetIoQueue(qinfo, restart);
   if (result) {
      // qinfo->unlockFunc(qinfo->lock);
      EPRINT("Failed IO queue reset qid %d", qinfo->id);
      return result;
   }
#if ENABLE_REISSUE
   // qinfo->unlockFunc(qinfo->lock);
   {
      struct NvmeCmdInfo *cmdInfo;
      vmk_ListLinks *itemPtr, *nextPtr;
      Nvme_Status nvmeStatus;
      vmk_ScsiCommand      *vmkCmd;
      vmk_ListLinks cmdActive;
      /**
        * We need to hold the lock queue because the reset enables the
        * interrupts and the active list might be modified
        */
      LOCK_FUNC(qinfo);
      NvmeCore_QueryActiveCommands(qinfo, &cmdActive);
      VMK_LIST_FORALL_SAFE(&cmdActive, itemPtr, nextPtr) {
         cmdInfo = VMK_LIST_ENTRY(itemPtr, struct NvmeCmdInfo, list);
         vmkCmd  = NvmeCore_CmdInfoToScsiCmd(cmdInfo);

         DPRINT_CMD("qinfo %p [%d], cmd_info %p, base %p [%d] vmkCmd %p", qinfo,
                 qinfo->id, cmdInfo,
                 cmdInfo->cmdBase, cmdInfo->cmdCount, vmkCmd);

         /**
          * Don't reissue base command which has been completed.
          * When one scsi command is split to several NVMe commands, the base NVMe
          * command would stay in active list until all other split commands completed.
          * In the situation that base command is completed prior to split command, we
          * don't need to reissue it because it has been completed.
          * See PR #1473498.
          * */
         if (!(cmdInfo->cmdBase == cmdInfo &&
            vmk_AtomicRead32(&cmdInfo->atomicStatus) == NVME_CMD_STATUS_DONE)) {
            nvmeStatus = NvmeCore_ReissueCommand(qinfo, cmdInfo);
            VMK_ASSERT(nvmeStatus == NVME_STATUS_SUCCESS);
         }

      }
      UNLOCK_FUNC(qinfo);
   }
#endif

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
         EPRINT("Failed IO queue reset, terminating restart");
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
NvmeCtrlr_HwReset(struct NvmeCtrlr *ctrlr,
                  struct NvmeNsInfo* ns,
                  Nvme_Status status,
                  vmk_Bool doReissue)
{
   VMK_ReturnStatus vmkStatus;
   Nvme_CtrlrState state;
   int nrIoQueues;

   IPRINT("Restarting Controller %s.", Nvme_GetCtrlrName(ctrlr));
   state = NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_INRESET);
   if (state == NVME_CTRLR_STATE_INRESET) {
      /**
       * We are already in reset
       */
       return VMK_BUSY;
   }
   if (state == NVME_CTRLR_STATE_FAILED) {
      /**
       * State transition from FAILED to OPERATIONAL is not allowed
       */
       return VMK_NOT_SUPPORTED;
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
   NvmeCtrlr_FlushIoQueues(ctrlr, ns, status, doReissue);
   NvmeCtrlr_ResetIoQueues(ctrlr);

   /* Asynchronous events */
   ctrlr->curAen = 0;
   vmk_SpinlockUnlock(ctrlr->lock);

   /**
    * Now it is safe to restart the controller.
    */
   vmkStatus = NvmeCtrlr_HwStart(ctrlr);
   if (vmkStatus != VMK_OK) {
      EPRINT("Controller Reset Failure. Offlining Controller.");
      goto err_out;
   }

   /**
    *  Transit ctrlr state from INRESET to STARTED to make sure completion
    *  callback:nvmeCoreProcessCq would handle the admin commands later correctly.
    */
    NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_STARTED);

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
      EPRINT("Controller Identify Failure. Offlining Controller.");
      goto err_out;
   }

   /* Double check number of queues to be same as nrIoQueues */
   nrIoQueues = ctrlr->numIoQueues;
   vmkStatus = NvmeCtrlr_RequestIoQueues(ctrlr, &nrIoQueues);
   if (vmkStatus != VMK_OK) {
      EPRINT("Failed to allocate hardware IO Queue error.");
      goto err_out;
   }
   if (nrIoQueues != ctrlr->numIoQueues) {
      EPRINT("IO queue configuration changed! Failing controller.");
      goto err_out;
   }
   DPRINT_Q("Got %d hw IO queues", nrIoQueues);

   vmkStatus = NvmeCtrlr_RestartIoQueues(ctrlr, VMK_TRUE);
   if (vmkStatus) {
      EPRINT("Failed to restart IO queue %0x.", vmkStatus);
      goto err_out;
   }

   /**
    * Lastly, resume IO queues.
    */
   NvmeCtrlr_ResumeIoQueues(ctrlr);

   /**
    * reinitiate AEN requests.
    */
#if ASYNC_EVENTS_ENABLED
   NvmeExc_RegisterForEvents(ctrlr);
#endif


   /**
    * Device is operational, restart timer and kick restart
    * IO queue processing.
    */
   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_OPERATIONAL);

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

   DPRINT_CTRLR("Exit %d", vmkStatus);
   return vmkStatus;

err_out:
   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_FAILED);
#if ENABLE_REISSUE
   /**
    * Abort all commands in active list.
    */
   if (doReissue) {
      NvmeCtrlr_FlushIoQueues(ctrlr, ns, status, VMK_FALSE);
   }
#endif
   return VMK_FAILURE;
}


VMK_ReturnStatus
NvmeCtrlr_Remove(struct NvmeCtrlr *ctrlr)
{

  /* Drive transitions to missing state (a terminal state) */
  NvmeCtrlr_SetMissing(ctrlr);

  /*
   * Flush all I/O queues and  inform kernel of PDL.
   */
  vmk_SpinlockLock(ctrlr->lock);

  NvmeCtrlr_SuspendAdminQueue(ctrlr);
  NvmeCtrlr_SuspendIoQueues(ctrlr);

  NvmeCtrlr_FlushAdminQueue(ctrlr);
  NvmeCtrlr_ResetAdminQueue(ctrlr);

  NvmeCtrlr_FlushIoQueues(ctrlr, NULL, 0, VMK_FALSE);

  /* Asynchronous events */
  ctrlr->curAen = 0;

  vmk_SpinlockUnlock(ctrlr->lock);
  /*
   * Inform stack of  PDL.
   */
  if (ctrlr->ctrlOsResources.scsiAdapter) {
     OsLib_SetPathLostByDevice(ctrlr);
  }

  return VMK_OK;

}

/**
 * Quiesce a controller
 *
 * @param [in] ctrlr controller instance
 */
VMK_ReturnStatus
NvmeCtrlr_Quiesce(struct NvmeCtrlr *ctrlr)
{
   struct NvmeQueueInfo *qinfo;
   Nvme_CtrlrState state;
   int i = 0;

   /**
    * First, block IOs to the controller.
    */
   state = NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_SUSPEND);

   NvmeCtrlr_SuspendIoQueues(ctrlr);
   /**
    * Stop the controller, then give outstanding commands a chance to complete.
    */
   for (i = 0; i < ctrlr->numIoQueues; i++) {
      qinfo = &ctrlr->ioq[i];

      /**
       * Flush completed items, make sure that completed items are preserved.
       */
      NvmeCore_ProcessQueueCompletions(qinfo);
   }
   NvmeCtrlr_ResumeIoQueues(ctrlr);
   NvmeState_SetCtrlrState(ctrlr, state);
   return VMK_OK;
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

   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_QUIESCED);

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

   NvmeCtrlr_FlushIoQueues(ctrlr, NULL, 0, VMK_FALSE);

   /* Asynchronous events */
   ctrlr->curAen = 0;

   vmk_SpinlockUnlock(ctrlr->lock);

   /* Free queue resources and namespace resources */
   NvmeCtrlr_DeleteIoQueues(ctrlr);

#if NVME_MUL_COMPL_WORLD
   OsLib_EndCompletionWorlds(ctrlr);
#endif

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
VMK_ReturnStatus NvmeCtrlr_DoTaskMgmtReset(struct NvmeCtrlr *ctrlr,
                                           Nvme_ResetType resetType,
                                           struct NvmeNsInfo *ns)
{
   VMK_ReturnStatus vmkStatus;

   DPRINT_CTRLR("Reset ctrlr %s: %s", Nvme_GetCtrlrName(ctrlr),
                 Nvme_GetResetTypeName(resetType));

   if (nvme_dbg & NVME_DEBUG_DUMP_NS) {
      NvmeDebug_DumpNsInfo(ns);
   }

   switch(resetType) {
      case NVME_TASK_MGMT_BUS_RESET:
         /**
          * I_T Nexus Reset - Shall be supported by returning FUNCTION
          *                   SUCCEEDED if there are outstanding commands in
          *                   the submission queue, otherwise by returning
          *                   FUNCTION COMPLETE.
          */
         vmkStatus = NvmeCtrlr_HwReset(ctrlr, NULL, NVME_STATUS_RESET, VMK_TRUE);
         break;
      case NVME_TASK_MGMT_LUN_RESET:
         /**
          * LOGICAL UNIT RESET - Shall be supported by writing a 0 to Enable
          *                      (EN) field of Controller Configuration
          *                      register
          */
         vmkStatus = NvmeCtrlr_HwReset(ctrlr, ns, NVME_STATUS_RESET, VMK_TRUE);
         break;
      case NVME_TASK_MGMT_DEVICE_RESET:
         /**
          * DEVICE RESET - Shall be supported by writing a 0 to Enable
          *                      (EN) field of Controller Configuration
          *                      register
          */
         vmkStatus = NvmeCtrlr_HwReset(ctrlr, NULL, NVME_STATUS_RESET, VMK_TRUE);
         break;
      default:
         vmkStatus = VMK_BAD_PARAM;
         VMK_ASSERT(0);
         break;
   }

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
NvmeCtrlr_DoTaskMgmtAbort(struct NvmeCtrlr *ctrlr,
                          vmk_ScsiTaskMgmt *taskMgmt,
                          struct NvmeNsInfo *ns)
{
   int                   i;
   int                   cmdsFound = 0, qf = 0;
   int                   cmdsImpacted = 0, qi = 0;
   struct NvmeQueueInfo *qinfo;
   struct NvmeCmdInfo   *cmdInfo;
   vmk_ScsiCommand      *vmkCmd;
   vmk_ListLinks        *itemPtr;
   Nvme_CtrlrState       ctrlrState;
   vmk_ListLinks cmdActive;

   ctrlrState = NvmeState_GetCtrlrState(ctrlr);
   if (ctrlrState != NVME_CTRLR_STATE_OPERATIONAL) {
      WPRINT("task management abort received while controller is in"
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
   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_SUSPEND);

   NvmeCtrlr_SuspendIoQueues(ctrlr);

   /**
     *  We disabled the queues (and interrupts above) but we might still have
     *  interrupts just fired on a different CPU and executing the handlers
     *  will race with the enumeration of the active list below
     */
   vmk_WorldSleep(NVME_ABORT_DELAY_US);

   /**
    * Stop the controller, then give outstanding commands a chance to complete.
    */
   for (i = 0; i < ctrlr->numIoQueues; i++) {
      qinfo = &ctrlr->ioq[i];

      qf = 0;
      qi = 0;

      DPRINT_Q("scan %s I:%p SN:0x%lx in queue %d, act:%d.",
              vmk_ScsiGetTaskMgmtTypeName(taskMgmt->type),
              taskMgmt->cmdId.initiator, taskMgmt->cmdId.serialNumber, qinfo->id,
              qinfo->nrAct - qinfo->pendingCmdFree.freeListLength);


      /**
       * Flush completed items, make sure that completed items are preserved.
       */
      NvmeCore_ProcessQueueCompletions(qinfo);

      /**
       * Now search for still active cmds, if there are any, we need to do an
       * NVM reset to clear them
       */
      LOCK_FUNC(qinfo);

      NvmeCore_QueryActiveCommands(qinfo, &cmdActive);
      VMK_LIST_FORALL(&cmdActive, itemPtr) {
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
            DPRINT_CMD("vmkCmd %p [%Xh] I:%p SN:0x%lx found to be aborted.",
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

      DPRINT_Q("scan %s in queue %d completed, %d found, %d impacted.",
              vmk_ScsiGetTaskMgmtTypeName(taskMgmt->type), qinfo->id, qf, qi);

      UNLOCK_FUNC(qinfo);
   }

   /**
    * Finally, if we found commands to be aborted, issue NVM reset to clear them
    */
   if (cmdsFound) {
      WPRINT("scan %s completed, %d found, %d impacted, resetting controller.",
         vmk_ScsiGetTaskMgmtTypeName(taskMgmt->type), cmdsFound, cmdsImpacted);

      NvmeCtrlr_HwReset(ctrlr, ns, NVME_STATUS_ABORTED, VMK_FALSE);

      /**
       * After reset, the controller state should transition to OPERATIONAL.
       */
   } else {
      /**
       * No command is found matching the requested task management request.
       * the command should have been completed by hardware already.
       */

      NvmeCtrlr_ResumeIoQueues(ctrlr);

      NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_OPERATIONAL);
   }


   return VMK_OK;
}





#if USE_TIMER

/**
 * @brief This function is timer heartbeat to keep track of active commands.
 *              This function is called TIMEOUT_FREQ to check for timed
 *              out commands.
 *
 * @param[in] struct NvmeCtrlr *ctrlr pointer to nvme controller context
 *
 * @param[in] none.
 */
vmk_Bool
NvmeCtrlr_Timeout (struct NvmeCtrlr *ctrlr, vmk_uint32 * sleepTime)
{
   struct NvmeQueueInfo *qinfo;
   Nvme_CtrlrState ctrlrState;
   int i, newId;
   vmk_Bool ret = VMK_FALSE;
#if QUEUE_STAT
   int len = 0;
#endif

   DPRINT_TIMEOUT("In Timer %d", ctrlr->timeoutId);

   /**
    * Search all IO queues for staled request.
    */
   vmk_SpinlockLock (ctrlr->lock);
   newId = ctrlr->timeoutId + 1;
   if (newId >= ctrlr->ioTimeout)
      newId = 0;

   ctrlrState = NvmeState_GetCtrlrState(ctrlr);
   /**
    * Timer is only valid in OPERATIONAL state.
    */
   if (NVME_CTRLR_STATE_OPERATIONAL != ctrlrState)
   {
      DPRINT_TIMEOUT("Controller not in OPERATIONAL state: %s.",
            NvmeState_GetCtrlrStateString (ctrlrState));
      goto skip_timer;
   }

   for (i = 1; i <= ctrlr->numIoQueues; i++)
   {

      qinfo = ctrlr->queueList[i];
      if (!qinfo)
      {
         continue;
      }

      /**
       * Update first the counter with the value from completion.
       * the operation atomically resets the completion counter
       * the submission lock is assumed held here
       */
      LOCK_FUNC(qinfo);
      qinfo->timeoutCount[newId] -=
            (int)vmk_AtomicReadWrite32(&qinfo->timeoutComplCount[newId], 0);
      UNLOCK_FUNC(qinfo);

#if NVME_DEBUG
      if (nvme_dbg & NVME_DEBUG_DUMP_TIMEOUT)
      {
         NvmeDebug_DumpTimeoutInfo(qinfo);
      }
#endif
      /**
       * Timer is only valid when we are operational.
       */
      if (qinfo->flags & QUEUE_SUSPEND)
      {
         DPRINT_TIMEOUT("qinfo %p [%d] suspended, skipping ...\n",
                qinfo, qinfo->id);
         continue;
      }

      /**
       * Update queue timer slot.
       * Check next timer slot and see if there are currently commands
       * pending on this slot. Any commands with matching timeout Ids
       * must be aborted.
       * Update first the counter with the value from completion
       */
      if (qinfo->timeoutCount[newId])
      {
         ctrlr->timeoutId = newId;
         DPRINT_TIMEOUT("qinfo %p, timeout[%d]= %d\n", qinfo, newId,
               qinfo->timeoutCount[newId]);
         ret = VMK_TRUE;
         break;
      }
   }
   ctrlrState = NvmeState_GetCtrlrState(ctrlr);
   DPRINT_TIMEOUT("TimeoutId %d, ctrlrState [%d]: %s",
              ctrlr->timeoutId, ctrlrState, NvmeState_GetCtrlrStateString(ctrlrState));
   if (ctrlrState <= NVME_CTRLR_STATE_OPERATIONAL)
   {
      ctrlr->timeoutId = newId;
      DPRINT_TIMEOUT("new timeout_id %d\n", newId);
   }
skip_timer:
   vmk_SpinlockUnlock (ctrlr->lock);
   return ret;
}

#endif

#if ASYNC_EVENTS_ENABLED

const char *Nvme_ErrorStatusString[] = {
   "Invalid Submssion Queue",
   "Invalid Doorbell Write",
   "Diagnostic Failure",
   "Persistent Internal Device Error",
   "Transient Internal Device Error",
   "Firmware Image Load Error"
};

const char *
NvmeCtrlr_GetErrorStatusString (int errorStatus)
{
   if ((errorStatus < 0) || (errorStatus >= ASYNC_EVENT_ERROR_LAST)) {
      return "";
   }
   return Nvme_ErrorStatusString[errorStatus];
}



/**
 * @brief Asynchronous Event Completion
 *
 * @param qinfo
 * @param cmdInfo
 */
static void
AsyncEventReportComplete (struct NvmeQueueInfo *qinfo, struct NvmeCmdInfo *cmdInfo)
{
   struct NvmeCtrlr *ctrlr = qinfo->ctrlr;

   if (cmdInfo->cmdStatus != NVME_STATUS_SUCCESS) {
      /* Not a valid event when async event cmd is completed with error. */
      if (cmdInfo->cqEntry.SCT == 0x1 && cmdInfo->cqEntry.SC == 0x05) {
         EPRINT("Asynchronous event limit exceeded.");
      }
      goto out;
   }

   /**
    * Three types of event reported -
    *   1)Error event - general error not associated with a command. To clear this event,
    *   host uses Get Log Page to read error information log.
    *
    *   2)SMART/Health event - configured via Set Features. To clear this event, signal
    *   exception handler to issue Het Log Page to read the SMART/Health information log.
    *
    *   3) Vendor-Specific event - ignore.
    */
   ctrlr->asyncEventData.eventType = cmdInfo->cqEntry.param.cmdSpecific & 0x07;
   ctrlr->asyncEventData.eventInfo = (cmdInfo->cqEntry.param.cmdSpecific >> 8) & 0xff;
   ctrlr->asyncEventData.logPage   = (cmdInfo->cqEntry.param.cmdSpecific >> 16) & 0xff;

   WPRINT("Asynchronous event type=%x event Info = %x received\n",
         ctrlr->asyncEventData.eventType, ctrlr->asyncEventData.eventInfo);
   switch (ctrlr->asyncEventData.eventType)
   {
      case AER_ERR_STATUS:
         EPRINT("Error information: %s",
                NvmeCtrlr_GetErrorStatusString (ctrlr->asyncEventData.eventInfo));
         NvmeExc_SignalException (ctrlr, NVME_EXCEPTION_ERROR_CHECK);
         break;
      case AER_SMART_HEALTH_STATUS:
         EPRINT("Smart health event: %s",
                NvmeCtrlr_GetAsyncEventHealthStatusString(
                ctrlr->asyncEventData.eventInfo));
         NvmeExc_SignalException (ctrlr, NVME_EXCEPTION_HEALTH_CHECK);
         break;
      default:
         break;
   }

out:
   ctrlr->curAen--;
   NvmeCore_PutCmdInfo (qinfo, cmdInfo);

   return;
}

/**
 * @brief Enables events that will trigger asynchronous events to the host.
 *
 * @param ctrlr
 * @param eventConfig
 *
 * @return
 */
VMK_ReturnStatus
NvmeCtrlr_ConfigAsyncEvents(struct NvmeCtrlr *ctrlr, vmk_uint16 eventConfig)
{

   VMK_ReturnStatus vmkStatus = VMK_OK;
   struct cq_entry cqEntry;

   vmkStatus = NvmeCtrlrCmd_SetFeature(ctrlr, 0, FTR_ID_ASYN_EVENT_CONFIG,
                                       (eventConfig & 0xff), NULL, 0, &cqEntry);

   if (vmkStatus != VMK_OK) {
      WPRINT("Async event config failed");
   }

   vmkStatus = NvmeCtrlrCmd_GetFeature(ctrlr, 0, FTR_ID_ASYN_EVENT_CONFIG, 0, NULL, 0,
                                       &cqEntry);


   VPRINT("Async event config is 0x%x", cqEntry.param.cmdSpecific & 0xff);
   return vmkStatus;
}



/**
 * @brief This function sets up asynchrnous event notification.
 *
 *
 * @param[in] ctrlr pointer to nvme device context
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
VMK_ReturnStatus
NvmeCtrlrCmd_AsyncEventRequest (struct NvmeCtrlr * ctrlr)
{
   struct nvme_cmd entry;
   Nvme_Memset64 (&entry, 0LL, sizeof (entry) / sizeof (vmk_uint64));
   entry.header.opCode = NVM_ADMIN_CMD_ASYNC_EVENT_REQ;
   return NvmeCtrlrCmd_SendAdminAsync(ctrlr, &entry, NULL, 0, AsyncEventReportComplete,
                                      NULL);
}
#endif
