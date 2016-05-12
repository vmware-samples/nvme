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
#include "../../common/kernel/nvme_private.h"
#include "../../common/kernel/nvme_exc.h"
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
   vmk_uint64 minPage, maxPage, hwMaxQs, hwCap;

   hwCap = Nvme_Readq(ctrlr->regs + NVME_CAP);
   hwMaxQs = (hwCap & NVME_CAP_MQES_MSK64) + 1;

   DPRINT_CTRLR("Controller Capability reg: %016lx", hwCap);

   /* Validate completion and submission queue size */
   if (hwMaxQs && ((io_cpl_queue_size > hwMaxQs) ||
      (io_sub_queue_size > hwMaxQs))) {
      EPRINT("Parameter: maximum HW queue size %lu", hwMaxQs);
      EPRINT("Adapting Hardware suggested queue size.");
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
       EPRINT("Adjusting io_command_id_size to %d", io_command_id_size);
   }

   if (max_io_request > io_command_id_size) {
       max_io_request = io_command_id_size;
       EPRINT("Adjusting max_io_request to %d", io_command_id_size);
   }

   minPage = (1 << (((hwCap & NVME_CAP_MPSMIN_MSK64) >>
            NVME_CAP_MPSMIN_LSB) + 12));
   maxPage = (1 << (((hwCap & NVME_CAP_MPSMAX_MSK64) >>
            NVME_CAP_MPSMAX_LSB) + 12));
   DPRINT_CTRLR("hardware maximum page size %lu", maxPage);
   DPRINT_CTRLR("hardware minimum page size %lu", minPage);

   if ((maxPage < VMK_PAGE_SIZE) || (minPage > VMK_PAGE_SIZE)) {
       EPRINT("Controller does not support OS default Page size %u", VMK_PAGE_SIZE);
       return VMK_BAD_PARAM;
   }

   max_prp_list = (transfer_size * 1024) / VMK_PAGE_SIZE;
   DPRINT_CTRLR("Max xfer %d, Max PRP %d", transfer_size, max_prp_list);

#if NVME_MUL_COMPL_WORLD
   /* equal to PCPU number of server */
   vmk_int32 compl_worlds_upper_limit = Oslib_GetPCPUNum();
   /* equal to CPU node number of server */
   vmk_int32 compl_worlds_lower_limit = vmk_ScsiGetMaxNumCompletionQueues();

   /* verify limitation of completion worlds number */
   if (compl_worlds_lower_limit < 1) {
      EPRINT("Fatal Error: CPU nodes number is %d.", compl_worlds_lower_limit);
      return VMK_BAD_PARAM;
   }
   if (compl_worlds_upper_limit < compl_worlds_lower_limit) {
      EPRINT("Fatal Error: compl_worlds_upper_limit is less than  \
              compl_worlds_lower_limit.");
      return VMK_BAD_PARAM;
   }
   if (compl_worlds_upper_limit > NVME_MAX_COMPL_WORLDS) {
      compl_worlds_upper_limit = NVME_MAX_COMPL_WORLDS;
   }

   /* verify user configration of completion worlds number */
   if (nvme_compl_worlds_num < compl_worlds_lower_limit) {
      nvme_compl_worlds_num = compl_worlds_lower_limit;
      EPRINT("The range of nvme_compl_worlds_num is [%d, %d].      \
             Adjusting nvme_compl_worlds_num to %d", compl_worlds_lower_limit, \
             compl_worlds_upper_limit, nvme_compl_worlds_num);
   }
   else if (nvme_compl_worlds_num > compl_worlds_upper_limit) {
      nvme_compl_worlds_num = compl_worlds_upper_limit;
      EPRINT("The range of nvme_compl_worlds_num is [%d, %d].      \
             Adjusting nvme_compl_worlds_num to %d", compl_worlds_lower_limit, \
             compl_worlds_upper_limit, nvme_compl_worlds_num);
   }
#endif

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

   if (!nrIoQueues || (ctrlr->ctrlOsResources.msixEnabled && nrIoQueues >= ctrlr->ctrlOsResources.numVectors)) {
      EPRINT("nrIoQueues: %d, numVectors: %d.", nrIoQueues, ctrlr->ctrlOsResources.numVectors);
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
      intrIndex = ctrlr->ctrlOsResources.msixEnabled ? i : 0;

      ctrlr->ioq[i-1].ctrlr = ctrlr;
      vmkStatus = NvmeQueue_Construct(&ctrlr->ioq[i - 1], /* IO queue starts from index 1 */
         io_sub_queue_size, io_cpl_queue_size, i, shared, intrIndex);
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
         EPRINT("trying to delete active queue %d.", qinfo->id);
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
         DPRINT_Q("Destroyed sq %d, 0x%x.", sqInfo->id, vmkStatus);
         vmkStatus = NvmeCtrlrCmd_DeleteCq(ctrlr, qinfo->id);
         DPRINT_Q("Destroyed cq %d, 0x%x.", qinfo->id, vmkStatus);
      }

      NvmeCore_SuspendQueue(qinfo, 0);
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
      DPRINT_CTRLR("Initial disable result: 0x%x.", vmkStatus);
      if (vmkStatus != VMK_OK) {
         EPRINT("Controller reset clear enable failure status 0x%x.",
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
   DPRINT_CTRLR("Writing CC: 0x%08x.", config);
   Nvme_Writel(config, (regs + NVME_CC));
   Nvme_Readl((regs + NVME_CC));
   Nvme_WaitCond(ctrlr, ctrlr->hwTimeout,
      (Nvme_Readl(regs+NVME_CSTS)&NVME_CSTS_RDY), vmkStatus);
   DPRINT_CTRLR("Initial reset result: 0x%x.", Nvme_Readl(regs+NVME_CSTS));

   if (vmkStatus != VMK_OK) {
      EPRINT("Controller reset enable failure status 0x%x.",
             Nvme_Readl(regs + NVME_CSTS));
      // return vmkStatus;
   }

   Nvme_Writel(0, (regs + NVME_CC));
   Nvme_Readl((regs + NVME_CC));
   Nvme_WaitCond(ctrlr, ctrlr->hwTimeout,
      (!(Nvme_Readl(regs+NVME_CSTS)&NVME_CSTS_RDY)), vmkStatus);
   DPRINT_CTRLR("Controller disable status: 0x%x.", vmkStatus);
   if (vmkStatus != VMK_OK) {
      EPRINT("Controller reset clear enable failure status 0x%x.",
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
      EPRINT("Controller reset enable failure status: 0x%x.",
             Nvme_Readl(regs + NVME_CSTS));
      EPRINT("Failed to start controller, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   ctrlr->version = Nvme_Readl(regs + NVME_VS);
   if (ctrlr->version == 0xffffffff) {
       return VMK_FAILURE;
   }
   IPRINT("Controller version: 0x%04x", ctrlr->version);

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
   if (NvmeState_GetCtrlrState(ctrlr, VMK_TRUE) == NVME_CTRLR_STATE_MISSING) {
      return VMK_OK;
   }

   /* Clear controller Enable */
   if (Nvme_Readl(ctrlr->regs + NVME_CSTS) & NVME_CSTS_RDY)
       Nvme_Writel(0, (ctrlr->regs + NVME_CC));

   Nvme_WaitCond(ctrlr, ctrlr->hwTimeout,
      (!Nvme_Readl(ctrlr->regs+NVME_CSTS)&NVME_CSTS_RDY), vmkStatus);

   DPRINT_CTRLR("Status after controller stop: 0x%x.",
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
   LOCK_FUNC(qinfo);

   cmdInfo = NvmeCore_GetCmdInfo(qinfo);
   if (!cmdInfo) {
      UNLOCK_FUNC(qinfo);
      return VMK_NO_MEMORY;
   }
   UNLOCK_FUNC(qinfo);

   cmdInfo->type = ADMIN_CONTEXT;
   entry->header.cmdID = cmdInfo->cmdId;
   cmd = &cmdInfo->nvmeCmd;
   Nvme_Memcpy64(&cmdInfo->nvmeCmd, entry, sizeof(*entry)/sizeof(vmk_uint64));

   DPRINT_ADMIN("Submitting admin command 0x%x, id:%d.", cmd->header.opCode, cmdInfo->cmdId);
#if NVME_DEBUG
   if (nvme_dbg & NVME_DEBUG_DUMP_CMD) {
      NvmeDebug_DumpCmd(entry);
   }
#endif

   nvmeStatus = NvmeCore_SubmitCommandWait(qinfo, cmdInfo, cqEntry,
                                           timeoutUs);
   if (!SUCCEEDED(nvmeStatus)) {
      VPRINT("admin command %p [%d] failed, 0x%x, %s.",
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

   DPRINT_ADMIN("Completed admin command 0x%x, id:%d, status:0x%x",
           entry->header.opCode, entry->header.cmdID, vmkStatus);

#if NVME_DEBUG
   if (cqEntry && (nvme_dbg & NVME_DEBUG_DUMP_CPL)) {
      NvmeDebug_DumpCpl(cqEntry);
   }
#endif

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
   DPRINT_ADMIN("Identify [0x%04x] completion result 0x%x, Status 0x%x",
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

   DPRINT_ADMIN("qid: %d.", id);

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

   DPRINT_ADMIN("qid: %d.", id);

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

   DPRINT_ADMIN("qid: %d.", qid);

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

   DPRINT_ADMIN("qid: %d.", qid);

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

   DPRINT_NS("Feature ID 0x%0x, option 0x%08x", feature, option);
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

   DPRINT_NS("Feature ID 0x%0x", feature);
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

/**
 * @brief This function obtains log page via synchronous command
 * 
 * @para[in] ctrlr pointer to nvme controller structure
 * @para[in] cmd pointer to nvme_cmd that has been partly assigned values by caller
 * @para[out] logPage address to copy log page data to
 * 
 * @return VMK_OK if successful, otherwise error code.
 */
VMK_ReturnStatus
NvmeCtrlrCmd_GetLogPageSync(struct NvmeCtrlr *ctrlr, struct nvme_cmd *cmd, void* logPage)
{
   VMK_ReturnStatus     vmkStatus;
   struct NvmeDmaEntry  dmaEntry;

   /*create dma entry*/
   vmkStatus = OsLib_DmaAlloc(&ctrlr->ctrlOsResources, VMK_PAGE_SIZE, &dmaEntry, VMK_TIMEOUT_UNLIMITED_MS);
   if (vmkStatus != VMK_OK) {
      EPRINT("failed to allocate memory!");
      return VMK_FAILURE;
   }

   cmd->header.prp[0].addr = dmaEntry.ioa;
   cmd->header.prp[1].addr = (cmd->header.prp[0].addr + (VMK_PAGE_SIZE)) &
                     ~(VMK_PAGE_SIZE -1);

   /* Send sync command*/
   DPRINT_ADMIN("admin cmd 0x%x ", cmd->header.opCode);
   vmkStatus = NvmeCtrlr_SendAdmin(ctrlr, cmd, NULL, ADMIN_TIMEOUT);

   /* Copy log page info*/
   if(vmkStatus == VMK_OK) {
      Nvme_Memcpy64(logPage, (struct smart_log*)dmaEntry.va, LOG_PG_SIZE/sizeof(vmk_uint64));
   }
   OsLib_DmaFree(&ctrlr->ctrlOsResources, &dmaEntry);

   DPRINT_ADMIN("GetLogPage [0x%04x],timeout %d us, completion result 0x%x",
          cmd->header.namespaceID, ADMIN_TIMEOUT, vmkStatus);

   return vmkStatus;
}

/**
 * @brief This fucntion obtains log page via asycn command
 *
 * @para[in] ctrlr pointer to nvme controller structure
 * @para[in] cmd pointer to nvme_cmd that has been partly assigned values by caller
 * @para[out] logPage address to copy log page data to
 * @para[in] pointer to command info structure
 *
 * @return VMK_OK if successful, otherwise error code.
 */
VMK_ReturnStatus
NvmeCtrlrCmd_GetLogPageAsync(struct NvmeCtrlr *ctrlr, struct nvme_cmd *cmd, void* logPage,
                             struct NvmeCmdInfo* cmdInfo)
{
   VMK_ReturnStatus     vmkStatus = VMK_OK;
   struct NvmeQueueInfo *qinfo;
   Nvme_Status  nvmeStatus;

   /* handle async GetLogPage request */
   DPRINT_ADMIN("async GetLogPage cmd 0x%x ", cmd->header.opCode);
   VMK_ASSERT(cmdInfo != NULL);

   /* use cmdInfo->prps to save log page data*/
   cmd->header.prp[0].addr = cmdInfo->prpPhy;
   cmd->header.prp[1].addr = (cmd->header.prp[0].addr + (VMK_PAGE_SIZE)) &
                             ~(VMK_PAGE_SIZE -1);


   qinfo = &(ctrlr->adminq);

   cmdInfo->type = ADMIN_CONTEXT;
   cmdInfo->status = NVME_CMD_STATUS_ACTIVE;
   cmd->header.cmdID = cmdInfo->cmdId;
   Nvme_Memcpy64(&cmdInfo->nvmeCmd, cmd, sizeof(cmdInfo->nvmeCmd)/sizeof(vmk_uint64));
   DPRINT_ADMIN("submit async GetLogPage admin cmd 0x%x, id:%d", cmdInfo->nvmeCmd.header.opCode, cmdInfo->cmdId);

   nvmeStatus = NvmeCore_SubmitCommandAsync(qinfo, cmdInfo, cmdInfo->done);

   if(!SUCCEEDED(nvmeStatus)) {
      VPRINT("Failed to submit Get Log Page command in async manner");
      cmdInfo->type = ABORT_CONTEXT;
      vmkStatus = VMK_FAILURE;
      LOCK_FUNC(qinfo);
      if (cmdInfo->cleanup) {
         cmdInfo->cleanup(qinfo, cmdInfo);
      }
      NvmeCore_PutCmdInfo(qinfo, cmdInfo);
      qinfo->timeout[cmdInfo->timeoutId] --;
      UNLOCK_FUNC(qinfo);
   }
   return vmkStatus;
}
/**
 * @brief This function sends a request to retrieve a log page
 *
 * @param[in] ctrlr pointer to nvme device context
 * @param[in] nsID: namespace ID
 * @param[out] logPage pointer to memory used for copying log page data. It is
 *            used only when sending sync cmd, Asnyc cmd has nothing to do with it.
 * @param[in] logPageID Log Page Identifier
 * @param[in] cmdInfo pointer to command info structure, used only in async manner
 * @param[in] isSync If VMK_TRUE, issue command via sync cmd NvmeCtrlr_SendAdmin
 *            which waits the comand to complete. Otherwise use async cmd.
 *
 * @return This function returns vmk_OK if successful, otherwise Error Code
 *
 */

VMK_ReturnStatus
NvmeCtrlrCmd_GetLogPage(struct NvmeCtrlr *ctrlr, vmk_uint32 nsID, void* logPage, vmk_uint16 logPageID,
                        struct NvmeCmdInfo* cmdInfo, vmk_Bool isSync)
{
   VMK_ReturnStatus	vmkStatus;
   struct nvme_cmd	entry;

   Nvme_Memset64(&entry, 0LL, sizeof(entry)/sizeof(vmk_uint64));
   entry.header.namespaceID = nsID;
   entry.cmd.getLogPage.LogPageID = logPageID & 0xFFFF;
   entry.cmd.getLogPage.numDW = (LOG_PG_SIZE/sizeof(vmk_uint32)-1);
   entry.header.opCode = NVM_ADMIN_CMD_GET_LOG_PAGE;

   /* TODO: handle GLP_ID_ERR_INFO and GLP_ID_FIRMWARE_SLOT_INFO */

   if (isSync == VMK_TRUE) {
      vmkStatus = NvmeCtrlrCmd_GetLogPageSync(ctrlr, &entry, logPage);
   }
   else {
      vmkStatus = NvmeCtrlrCmd_GetLogPageAsync(ctrlr, &entry, logPage, cmdInfo);
   }
   return vmkStatus;
}



VMK_ReturnStatus
NvmeCtrlrCmd_GetSmartLog(struct NvmeCtrlr *ctrlr, vmk_uint32 nsID, struct smart_log *smartLog,
                         struct NvmeCmdInfo *cmdInfo, vmk_Bool isSyncCmd)
{
   return NvmeCtrlrCmd_GetLogPage(ctrlr, nsID, (void*)smartLog, GLP_ID_SMART_HEALTH, cmdInfo, isSyncCmd);
}

VMK_ReturnStatus
NvmeCtrlrCmd_GetErrorLog(struct NvmeCtrlr *ctrlr, vmk_uint32 nsID, struct error_log *errorLog,
                         struct NvmeCmdInfo *cmdInfo, vmk_Bool isSyncCmd)
{
   return NvmeCtrlrCmd_GetLogPage(ctrlr, nsID, (void*)errorLog, GLP_ID_ERR_INFO, cmdInfo, isSyncCmd);
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
   struct NvmeDmaEntry dmaEntry;

   vmkStatus = OsLib_DmaAlloc(&ctrlr->ctrlOsResources, VMK_PAGE_SIZE, &dmaEntry, VMK_TIMEOUT_UNLIMITED_MS);
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

   IPRINT("Controller: %s.", Nvme_GetCtrlrName(ctrlr));
   IPRINT("Serial no: %s.", ctrlr->serial);
   IPRINT("Model no: %s.", ctrlr->model);
   IPRINT("Firmware revision: %s.", ctrlr->firmwareRev);

   DPRINT_CTRLR("Admin Cmd Vendor Cfg: 0x%x.", ctrlr->admVendCmdCfg);
   DPRINT_CTRLR("NVM Cmd Vendor Cfg: 0x%x.", ctrlr->nvmVendCmdCfg);
   DPRINT_CTRLR("Number of namespaces: %d.", ctrlr->nsCount);

free_dma:
   OsLib_DmaFree(&ctrlr->ctrlOsResources, &dmaEntry);
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

   DPRINT_Q("attempting to allocate [%d] IO queues", *nrIoQueues);

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
      DPRINT_Q("maximum of [%d] IO queues", cqEntry.param.numCplQAlloc);
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
   DPRINT_NS("Releasing Namespace [%d] %p", ns->id, ns);
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

   vmkStatus = OsLib_DmaAlloc(&ctrlr->ctrlOsResources, VMK_PAGE_SIZE, &dmaEntry, VMK_TIMEOUT_UNLIMITED_MS);
   if (vmkStatus != VMK_OK) {
      goto free_ns;
   }


   vmkStatus = NvmeCtrlrCmd_Identify(ctrlr, nsId, dmaEntry.ioa);
   if (vmkStatus != VMK_OK) {
       EPRINT("Failed get NS Identify data.");
       goto free_dma;
   }

   ident = (struct iden_namespace *)dmaEntry.va;
   DPRINT_NS("NS [%d], size %lu, lba_fmt 0x%02x, Formats 0x%02x",
            nsId, ident->size,
            ident->fmtLbaSize, ident->numLbaFmt);
   DPRINT_NS("NS [%d], feature 0x%02x, Prot Cap 0x%02x, Prot Set 0x%02x",
            nsId, ident->feat,
            ident->dataProtCap, ident->dataProtSet);

   for (i = 0; i <= ident->numLbaFmt; i++) {
      DPRINT_NS("supported LBA format 0x%08x",
               *(vmk_uint32 *)&ident->lbaFmtSup[i]);
   }
   lba_format   = *(vmk_uint32 *)&ident->lbaFmtSup[ident->fmtLbaSize & 0x0F];
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
   ns->metaDataCap = ident->metaDataCap;
   ns->ctrlr       = ctrlr;

   ns->eui64       = ident->eui64;

   DPRINT_NS("NS [%d] %p, adding to dev list %p, lba size %u",
         ns->id, ns, &ctrlr->nsList, (1 << ns->lbaShift));
   vmk_ListInsert(&ns->list, vmk_ListAtRear(&ctrlr->nsList));

   /* Need to free the DMA buffer used here */
   OsLib_DmaFree(&ctrlr->ctrlOsResources, &dmaEntry);

   /* Mark ns as ONLINE by default */
   ns->flags |= NS_ONLINE;

   /* Initially set ref count to 0 */
   vmk_AtomicWrite64(&ns->refCount, 0);

   return (ns);

free_dma:
   OsLib_DmaFree(&ctrlr->ctrlOsResources, &dmaEntry);

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
   DPRINT_NS("ns %d refCount increased to %ld.",
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
   DPRINT_NS("ns %d refCount decreased to %ld.",
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

      DPRINT_NS("allocating Namespace %d", nsId);
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
   if (ctrlr->ctrlOsResources.scsiAdapter) {
      OsLib_SetPathLostByDevice(&ctrlr->ctrlOsResources);
   }

   VMK_LIST_FORALL_SAFE(&ctrlr->nsList, itemPtr, nextPtr) {
      ns = VMK_LIST_ENTRY(itemPtr, struct NvmeNsInfo, list);

      /*
       * Try to delete the path. This is a best-effort operation, if there
       * are open handles to the SCSI device and path, the clean up would
       * fail.
       */
      if (ctrlr->ctrlOsResources.scsiAdapter) {
         vmk_ScsiScanDeleteAdapterPath(&ctrlr->ctrlOsResources.scsiAdapter->name,
               0,
               0,
               ns->id - 1);
      }

      DPRINT_NS("NS [%d], releasing resource %p", ns->id, ns);
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
   LOCK_FUNC(qinfo);
   cmdInfo = NvmeCore_GetCmdInfo(qinfo);
   if (!cmdInfo) {
      UNLOCK_FUNC(qinfo);
      return VMK_NO_MEMORY;
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
   cmdInfo->timeoutId = ctrlr->timeoutId;
   cmdInfo->doneData = NULL;
   cmd->cmd.read.numLBA = 1;
   if (END2END_DPS_TYPE(ns->dataProtSet)) {
      cmd->cmd.read.protInfo = 0x8;
   }

   qinfo->timeout[cmdInfo->timeoutId] ++;

   cmdInfo->type = BIO_CONTEXT;
   cmdInfo->status = NVME_CMD_STATUS_ACTIVE;

   timeout = 1 * 1000 * 1000; /* 1 second in microseconds */
   DPRINT_CMD("issue read to fw");
   nvmeStatus = NvmeCore_SubmitCommandWait(qinfo, cmdInfo, NULL, timeout);

   if (SUCCEEDED(nvmeStatus)) {
      /* When the command has been submitted into hardware, we should check cmdStatus 
       * to confirm whether the command is completed successfully*/
      nvmeStatus = cmdInfo->cmdStatus;
   }
   /*(1) Theoretically, nvmeStatus should reflect whether the command is truly completed.
    * If not, sleep 1 second before issuing next command to avoid high CPU utilization.
    *(2) There is a minor possibility that the command times out due to fw problem, in this case,
    * the command will be marked with ABORT_CONTEXT and handled in processCq routine. Since at most
    * 60 commands will be issued, the submission queue will not get overwhelmed given that its size is 1024*/
   if(!SUCCEEDED(nvmeStatus)) {
      DPRINT_CMD("read fails, sleep 1s");
      vmk_WorldSleep(timeout);
      DPRINT_CMD("sleep finished");
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
   if(ctrlr->nsCount > 0) {
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
      VPRINT("nsCount = 0, no need to check IO, return success");
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
         VPRINT("device not ready after 60 seconds, quit");
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

   /*
    * Allocate IO queue information blocks, required DMA resources
    * and register IO queues with controller.
    */
   vmkStatus = NvmeCtrlr_CreateIoQueues(ctrlr);
   if (vmkStatus != VMK_OK) {
      EPRINT("Failed to allocate IO queues, 0x%x.", vmkStatus);
      goto stop_hw;
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

   DPRINT_CMD("device %p [%s], suspending %d queues",
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
   LOCK_FUNC(qinfo);
   NvmeCore_FlushQueue(qinfo, NULL, INVALID_TIMESLOT, NVME_STATUS_IN_RESET , VMK_FALSE);

#if ENABLE_REISSUE == 0
   for (id = 0; id < io_timeout; id ++) {
      qinfo->timeout[id] = 0;
   }
#endif
   UNLOCK_FUNC(qinfo);
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

   DPRINT_CMD("device %p [%s], flushing %d queues",
            ctrlr, Nvme_GetCtrlrName(ctrlr), ctrlr->numIoQueues);

   for(i = 1; i <= ctrlr->numIoQueues; i++ ) {
      qinfo = &ctrlr->ioq[i - 1];

      DPRINT_CMD("qinfo %p [%d], nr_req %d, nr_act %d", qinfo, qinfo->id,
               qinfo->nrReq, qinfo->nrAct);

      LOCK_FUNC(qinfo);
      NvmeCore_FlushQueue(qinfo, ns, ctrlr->timeoutId, status, doReissue);
#if ENABLE_REISSUE == 0
      /**
       * Clear timeout table.
       */
      for (id=0; id<io_timeout; id++) {
         qinfo->timeout[id] = 0;
      }
      qinfo->nrAct = 0;     /* reset active requests */
#endif
      UNLOCK_FUNC(qinfo);
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

   IPRINT("Restarting io queue %p[%d].", qinfo, qinfo->id);
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

      VMK_LIST_FORALL_SAFE(&qinfo->cmdActive, itemPtr, nextPtr) {
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
         if (!(cmdInfo->cmdBase == cmdInfo && cmdInfo->status == NVME_CMD_STATUS_DONE)) {
            nvmeStatus = NvmeCore_ReissueCommand(qinfo, cmdInfo);
            VMK_ASSERT(nvmeStatus == NVME_STATUS_SUCCESS);
         }

      }
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
NvmeCtrlr_HwReset(struct NvmeCtrlr *ctrlr, struct NvmeNsInfo* ns , Nvme_Status status, vmk_Bool doReissue)
{
   VMK_ReturnStatus vmkStatus;
   Nvme_CtrlrState state;
   int nrIoQueues;

   IPRINT("Restarting Controller %s.", Nvme_GetCtrlrName(ctrlr));
   state = NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_INRESET, VMK_TRUE);
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
      EPRINT("Controller Reset Failure.");
      EPRINT("Offlining Controller.");
      goto err_out;
   }

   /**
    *  Transit ctrlr state from INRESET to STARTED to make sure completion
    *  callback:nvmeCoreProcessCq would handle the admin commands later correctly.
    */
    NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_STARTED, VMK_TRUE);

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
       EPRINT("Controller Identify Failure.");
       EPRINT("Offlining Controller.");
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
       EPRINT("IO queue configuration changed!!!");
       EPRINT("Unsupported configuration, failing controller.");
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
   vmk_SpinlockLock(ctrlr->lock);
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

   DPRINT_CTRLR("Exit %d", vmkStatus);
   return vmkStatus;

err_out:
   vmk_SpinlockLock(ctrlr->lock);
   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_FAILED, VMK_FALSE);
   vmk_SpinlockUnlock(ctrlr->lock);
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
     OsLib_SetPathLostByDevice(&ctrlr->ctrlOsResources);
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
   state = NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_SUSPEND, VMK_TRUE);

   NvmeCtrlr_SuspendIoQueues(ctrlr);
   /**
    * Stop the controller, then give outstanding commands a chance to complete.
    */
   for (i = 0; i < ctrlr->numIoQueues; i++) {
      qinfo = &ctrlr->ioq[i];

      LOCK_FUNC(qinfo);
      /**
       * Flush completed items, make sure that completed items are preserved.
       */
      NvmeCore_ProcessQueueCompletions(qinfo);
      UNLOCK_FUNC(qinfo);
   }
   NvmeCtrlr_ResumeIoQueues(ctrlr);
   NvmeState_SetCtrlrState(ctrlr, state, VMK_TRUE);
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

   NvmeCtrlr_FlushIoQueues(ctrlr, NULL, 0, VMK_FALSE);

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
VMK_ReturnStatus NvmeCtrlr_DoTaskMgmtReset(struct NvmeCtrlr *ctrlr, Nvme_ResetType resetType, struct NvmeNsInfo *ns)
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


   ctrlrState = NvmeState_GetCtrlrState(ctrlr, VMK_TRUE);
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
   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_SUSPEND, VMK_TRUE);

   NvmeCtrlr_SuspendIoQueues(ctrlr);

   /**
    * Stop the controller, then give outstanding commands a chance to complete.
    */
   for (i = 0; i < ctrlr->numIoQueues; i++) {
      qinfo = &ctrlr->ioq[i];

      qf = 0;
      qi = 0;

      DPRINT_Q("scan %s I:%p SN:0x%lx in queue %d, req:%d act:%d.",
              vmk_ScsiGetTaskMgmtTypeName(taskMgmt->type),
              taskMgmt->cmdId.initiator, taskMgmt->cmdId.serialNumber, qinfo->id,
              qinfo->nrReq, qinfo->nrAct);

      LOCK_FUNC(qinfo);

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
      DPRINT_CMD("scan %s completed, %d found, %d impacted.",
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

      NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_OPERATIONAL, VMK_TRUE);
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

   DPRINT_CMD("In Timer %d", ctrlr->timeoutId);
   /**
    * Search all IO queues for staled request.
    */
   vmk_SpinlockLock (ctrlr->lock);
   newId = ctrlr->timeoutId + 1;
   if (newId >= io_timeout)
      newId = 0;

   ctrlrState = NvmeState_GetCtrlrState (ctrlr, VMK_FALSE);
   /**
    * Timer is only valid in OPERATIONAL state.
    */
   if (NVME_CTRLR_STATE_OPERATIONAL != ctrlrState)
   {
      DPRINT_CMD("Controller not in OPERATIONAL state: %s.",
            NvmeState_GetCtrlrStateString (ctrlrState));
      goto skip_timer;
   }

   for (i = 1; i <= ctrlr->numIoQueues; i++)
   {

      qinfo = ctrlr->queueList[i];
      if (!qinfo)
      {
         DPRINT_CMD("Qinfo %p\n", qinfo);
         continue;
      }
#if NVME_DEBUG
      if (nvme_dbg & NVME_DEBUG_DUMP_TIMEOUT)
      {
         DPRINT_CMD("timeoutId %d\n", newId);
         NvmeDebug_DumpTimeoutInfo(qinfo);
      }
#endif
      /**
       * Timer is only valid when we are operational.
       */
      if (qinfo->flags & QUEUE_SUSPEND)
      {
         DPRINT_CMD("qinfo %p [%d] suspended, skipping ...\n",
               qinfo, qinfo->id);
         continue;
      }

      /**
       * Update queue timer slot.
       * Check next timer slot and see if there are currently commands
       * pending on this slot. Any commands with matching timeout Ids
       * must be aborted.
       */
      if (qinfo->timeout[newId])
      {
         ctrlr->timeoutId = newId;
         DPRINT_CMD("qinfo %p, timeout[%d]= %d\n", qinfo, newId,
               qinfo->timeout[newId]);
         ret = VMK_TRUE;
         break;
      }
   }
   ctrlrState = NvmeState_GetCtrlrState (ctrlr, VMK_FALSE);
   DPRINT_CMD("TimeoutId %d, ctrlrState [%d]: %s",
                             ctrlr->timeoutId, ctrlrState, NvmeState_GetCtrlrStateString(ctrlrState));
   if (ctrlrState <= NVME_CTRLR_STATE_OPERATIONAL)
   {
      ctrlr->timeoutId = newId;
      DPRINT_CMD("new timeout_id %d\n", newId);
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
AsyncEventReportComplete (struct NvmeQueueInfo *qinfo,
      struct NvmeCmdInfo *cmdInfo)
{
   struct NvmeCtrlr *ctrlr = qinfo->ctrlr;

   if (cmdInfo->cmdStatus == NVME_STATUS_IN_RESET) {
      /* Not an event when async event cmd is completed by hw reset. */
      goto out;
   }

   /**
    * Three types of event reported -
    * 	1)Error event - general error not associated with a command. To clear this event,
    * 	host uses Get Log Page to read error information log.
    *
    * 	2)SMART/Health event - configured via Set Features. To clear this event, signal
    * 	exception handler to issue Het Log Page to read the SMART/Health information log.
    *
    * 	3) Vendor-Specific event - P600XC IAS does not describe any event in this category.
    */
   ctrlr->asyncEventData.eventType = cmdInfo->cqEntry.param.cmdSpecific & 0x07;

   ctrlr->asyncEventData.eventInfo = (cmdInfo->cqEntry.param.cmdSpecific >> 8) & 0xff;

   ctrlr->asyncEventData.logPage   = (cmdInfo->cqEntry.param.cmdSpecific >> 16) & 0xff;

   VPRINT("Asynchronous event type=%x event Info = %x received\n",
         ctrlr->asyncEventData.eventType, ctrlr->asyncEventData.eventInfo);
   if (cmdInfo->cqEntry.SC == 0x05)
   {
      EPRINT("Asynchronous event limit exceeded\n");
   }
   else
   {
      switch (ctrlr->asyncEventData.eventType)
      {
         case AER_ERR_STATUS:
            VPRINT ("Error information : %s\n",
                  NvmeCtrlr_GetErrorStatusString (ctrlr->asyncEventData.eventInfo));
            NvmeExc_SignalException (ctrlr, NVME_EXCEPTION_ERROR_CHECK);
            break;
         case AER_SMART_HEALTH_STATUS:
	    VPRINT("Smart health event : %s\n", NvmeCtrlr_GetAsyncEventHealthStatusString(ctrlr->asyncEventData.eventInfo));
            NvmeExc_SignalException (ctrlr, NVME_EXCEPTION_HEALTH_CHECK);
            break;
         default:
            break;
      }
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
VMK_ReturnStatus NvmeCtrlr_ConfigAsyncEvents(struct NvmeCtrlr *ctrlr, vmk_uint16 eventConfig)
{

   VMK_ReturnStatus vmkStatus = VMK_OK;
   struct cq_entry cqEntry;

   vmkStatus = NvmeCtrlrCmd_SetFeature(ctrlr, FTR_ID_ASYN_EVENT_CONFIG,
         (eventConfig & 0xff),
         NULL, &cqEntry);

   if (vmkStatus != VMK_OK)
   {
      Nvme_LogWarning("Async event config failed");
   }

   vmkStatus = NvmeCtrlrCmd_GetFeature(ctrlr, 0xffffffff, FTR_ID_ASYN_EVENT_CONFIG, 0,
         NULL, &cqEntry);


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
   struct nvme_cmd *cmd;
   struct NvmeQueueInfo *qinfo;
   struct NvmeCmdInfo *cmdInfo;
   Nvme_Status nvmeStatus;

   Nvme_Memset64 (&entry, 0LL, sizeof (entry) / sizeof (vmk_uint64));
   entry.header.opCode = NVM_ADMIN_CMD_ASYNC_EVENT_REQ;

   qinfo = &ctrlr->adminq;
   qinfo->lockFunc (qinfo->lock);

   cmdInfo = NvmeCore_GetCmdInfo (qinfo);
   if (!cmdInfo)
   {
      qinfo->unlockFunc (qinfo->lock);
      return VMK_NO_MEMORY;
   }
   qinfo->unlockFunc (qinfo->lock);

   cmdInfo->type = EVENT_CONTEXT;
   entry.header.cmdID = cmdInfo->cmdId;
   cmd = &cmdInfo->nvmeCmd;
   Nvme_Memcpy64 (&cmdInfo->nvmeCmd, &entry,
         sizeof (entry) / sizeof (vmk_uint64));

   qinfo->lockFunc (qinfo->lock);
   nvmeStatus =
      NvmeCore_SubmitCommandAsync (qinfo, cmdInfo, AsyncEventReportComplete);
   if (nvmeStatus != NVME_STATUS_SUCCESS)
   {
      /**
       * Failed to submit the command to the hardware.
       */
      NvmeCore_PutCmdInfo (qinfo, cmdInfo);
   }

   if (SUCCEEDED (nvmeStatus))
   {
      /**
       * Return WOULD_BLOCK indicating the command will be completed in
       * completion context.
       */
      nvmeStatus = NVME_STATUS_WOULD_BLOCK;
   }

   /**
    * Accounting for the number of IO requests to the queue
    */
   if (nvmeStatus == NVME_STATUS_WOULD_BLOCK)
   {
      qinfo->nrReq++;
      if (qinfo->maxReq < qinfo->nrReq)
      {
         qinfo->maxReq = qinfo->nrReq;
      }
   }

   qinfo->unlockFunc (qinfo->lock);

   return VMK_OK;
}
#endif
