/*****************************************************************************
 * Copyright (c) 2016-2022 VMware, Inc. All rights reserved.
 * -- VMware Confidential
 *****************************************************************************/

/*
 * @file: nvme_pcie.c --
 *
 *   Main functions for native nvme_pcie driver.
 */

#include "nvme_pcie_int.h"

static VMK_ReturnStatus CompQueueConstruct(NVMEPCIEQueueInfo *qinfo, int qid,
                                           int qsize, int intrIndex);
static VMK_ReturnStatus CompQueueDestroy(NVMEPCIEQueueInfo *qinfo);
static VMK_ReturnStatus SubQueueConstruct(NVMEPCIEQueueInfo *qinfo,
                                          int qid, int qsize);
static VMK_ReturnStatus SubQueueDestroy(NVMEPCIEQueueInfo *qinfo);
static VMK_ReturnStatus CmdInfoListConstruct(NVMEPCIEQueueInfo *qinfo,
                                             int qsize);
static VMK_ReturnStatus CmdInfoListDestroy(NVMEPCIEQueueInfo *qinfo);
static VMK_ReturnStatus QueueConstruct(NVMEPCIEController *ctrlr,
                                       NVMEPCIEQueueInfo *qinfo,
                                       int qid, int sqsize,
                                       int cqsize, int intrIndex);
static VMK_ReturnStatus QueueDestroy(NVMEPCIEQueueInfo *qinfo);
static void NVMEPCIECompleteAsyncCommand(NVMEPCIEQueueInfo *qinfo,
                                         NVMEPCIECmdInfo *cmdInfo);
static void NVMEPCIECompleteSyncCommand(NVMEPCIEQueueInfo *qinfo,
                                        NVMEPCIECmdInfo *cmdInfo);
static vmk_NvmeStatus NVMEPCIEIssueCommandToHw(NVMEPCIEQueueInfo *qinfo,
                                               NVMEPCIECmdInfo *cmdInfo,
                                               NVMEPCIECompleteCommandCb cb);
static NVMEPCIECmdInfo* NVMEPCIEGetCmdInfo(NVMEPCIEQueueInfo *qinfo,
                                           vmk_uint16 cid);
static NVMEPCIECmdInfo* NVMEPCIEGetCmdInfoLegacy(NVMEPCIEQueueInfo *qinfo);
static void NVMEPCIEPutCmdInfo(NVMEPCIEQueueInfo *qinfo, NVMEPCIECmdInfo *cmdInfo);
static inline vmk_NvmeStatus GetCommandStatus(vmk_NvmeCompletionQueueEntry *cqe);
static VMK_ReturnStatus CreateSq(NVMEPCIEController *ctrlr,
                                 NVMEPCIEQueueInfo *qinfo);
static VMK_ReturnStatus CreateCq(NVMEPCIEController *ctrlr,
                                 NVMEPCIEQueueInfo *qinfo);
static VMK_ReturnStatus DeleteSq(NVMEPCIEController *ctrlr,
                                 vmk_uint16 qid);
static VMK_ReturnStatus DeleteCq(NVMEPCIEController *ctrlr,
                                 vmk_uint16 qid);
void NVMEPCIESuspendQueue(NVMEPCIEQueueInfo *qinfo);
VMK_ReturnStatus NVMEPCIEResumeQueue(NVMEPCIEQueueInfo *qinfo);

/**
 * Create queue and allocate queue resources
 *
 * @param[in] ctrlr  Controller instance
 * @param[in] qid    Queue ID
 * @param[in] qsize  Queue size
 *
 * @return VMK_OK on success, error code otherwise
 */
VMK_ReturnStatus
NVMEPCIEQueueCreate(NVMEPCIEController *ctrlr,
                      vmk_uint32 qid,
                      vmk_uint32 qsize)
{
   VMK_ReturnStatus vmkStatus;
   NVMEPCIEQueueInfo *qinfo;

   if (qid > ctrlr->maxIoQueues) {
      return VMK_BAD_PARAM;
   }

   qinfo = &ctrlr->queueList[qid];

   if (vmk_AtomicRead32(&qinfo->state) != NVME_PCIE_QUEUE_NON_EXIST) {
      WPRINT(ctrlr, "queue %d already exists", qid);
      return VMK_OK;
   }

   if (ctrlr->osRes.intrType == VMK_PCI_INTERRUPT_TYPE_MSIX) {
      vmkStatus = QueueConstruct(ctrlr, qinfo, qid, qsize, qsize, qid);
   } else {
      vmkStatus = QueueConstruct(ctrlr, qinfo, qid, qsize, qsize, 0);
   }

   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to construct IO queue [%d], 0x%x.", qid, vmkStatus);
      return vmkStatus;
   }

   vmkStatus = NVMEPCIEStartQueue(qinfo);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to start IO queue %d, 0x%x.", qid, vmkStatus);
      goto destroy_q;
   }

   if (qid > 0) {
      vmk_AtomicInc32(&ctrlr->numIoQueues);
   }
   return VMK_OK;

destroy_q:
   QueueDestroy(qinfo);
   return vmkStatus;
}

/**
 * Delete queue and free queue resources
 *
 * @param[in] ctrlr  Controller instance
 * @param[in] qid    Queue ID
 *
 * @return VMK_OK on success, error code otherwise
 */
VMK_ReturnStatus
NVMEPCIEQueueDestroy(NVMEPCIEController *ctrlr, vmk_uint32 qid, vmk_NvmeStatus status)
{
   VMK_ReturnStatus vmkStatus;
   NVMEPCIEQueueInfo *qinfo;

   if (qid > NVME_PCIE_MAX_IO_QUEUES) {
      return VMK_BAD_PARAM;
   }

   qinfo = &ctrlr->queueList[qid];
   if (vmk_AtomicRead32(&qinfo->state) == NVME_PCIE_QUEUE_NON_EXIST) {
      return VMK_OK;
   }

   vmkStatus = NVMEPCIEStopQueue(qinfo, status);
   vmkStatus = QueueDestroy(qinfo);

   if (qid > 0) {
      vmk_AtomicDec32(&ctrlr->numIoQueues);
   }

   return VMK_OK;
}

/**
 * Allocate and initialize a completion queue
 *
 * @param[in] qinfo  Queue instance
 * @param[in] qid    Completion queue identifier
 * @param[in] qsize  Completion queue size
 *
 * @return VMK_OK on success, error code otherwise
 */
static VMK_ReturnStatus
CompQueueConstruct(NVMEPCIEQueueInfo *qinfo, int qid, int qsize, int intrIndex)
{
   VMK_ReturnStatus vmkStatus;
   NVMEPCIECompQueueInfo *cqInfo;
   NVMEPCIEController *ctrlr;
   char lockName[VMK_MISC_NAME_MAX];

   ctrlr = qinfo->ctrlr;

   /** Allocate completion queue info struct */
   cqInfo = NVMEPCIEAlloc(sizeof(*cqInfo), 0);
   if (cqInfo == NULL) {
      EPRINT(ctrlr, "Failed to allocate cq %d.", qid);
      return VMK_NO_MEMORY;
   }

   qinfo->cqInfo = cqInfo;
   cqInfo->id = qid;
   cqInfo->qsize = qsize;
   cqInfo->intrIndex = intrIndex;

   /** Create completion queue lock */
   vmk_StringFormat(lockName, sizeof(lockName), NULL,
                    "cqLock-%s-%d",
                     NVMEPCIEGetCtrlrName(ctrlr), qid);

   vmkStatus = NVMEPCIELockCreate(ctrlr->osRes.lockDomain,
                                  NVME_LOCK_RANK_MEDIUM,
                                  lockName, &cqInfo->lock);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to create lock for cq %d, 0x%x.", qid, vmkStatus);
      goto free_cq;
   }

   /** Allocate completion queue DMA buffer */
   vmkStatus = NVMEPCIEDmaAlloc(&ctrlr->osRes,
                                qsize * sizeof(vmk_NvmeCompletionQueueEntry),
                                &cqInfo->dmaEntry, VMK_TIMEOUT_UNLIMITED_MS);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to allocate DMA buffer for cq %d, 0x%x.", qid, vmkStatus);
      goto free_lock;
   }
   cqInfo->compq = (vmk_NvmeCompletionQueueEntry *) cqInfo->dmaEntry.va;
   cqInfo->compqPhy = cqInfo->dmaEntry.ioa;
   cqInfo->doorbell = ctrlr->regs + VMK_NVME_REG_CQHDBL(qid, ctrlr->dstrd);
   cqInfo->phase = 1;
   cqInfo->head = 0;
   cqInfo->tail = 0;

   /** Register interrupt */
   if (ctrlr->osRes.intrType == VMK_PCI_INTERRUPT_TYPE_MSIX
       && intrIndex >= 0
       && intrIndex < ctrlr->osRes.numIntrs) {
      vmkStatus = NVMEPCIEIntrRegister(ctrlr->osRes.device,
                                       ctrlr->osRes.intrArray[cqInfo->intrIndex],
                                       qinfo,
                                       NVMEPCIEGetCtrlrName(ctrlr),
                                       NVMEPCIEQueueIntrAck,
                                       NVMEPCIEQueueIntrHandler);
      if (vmkStatus != VMK_OK) {
         EPRINT(ctrlr, "Failed to register interrupt for cq %d, 0x%x.", qid, vmkStatus);
         goto free_cq_dma;
      }
   }

   return VMK_OK;

free_cq_dma:
   NVMEPCIEDmaFree(&ctrlr->osRes, &cqInfo->dmaEntry);

free_lock:
   NVMEPCIELockDestroy(&cqInfo->lock);

free_cq:
   NVMEPCIEFree(cqInfo);
   qinfo->cqInfo = NULL;

   return vmkStatus;
}

/**
 * Destroy completion queue
 *
 * @param[in] qinfo  Queue instance
 *
 * @return VMK_OK on success, error code otherwise
 */
static VMK_ReturnStatus
CompQueueDestroy(NVMEPCIEQueueInfo *qinfo)
{
   VMK_ReturnStatus vmkStatus;
   NVMEPCIECompQueueInfo *cqInfo;
   NVMEPCIEController *ctrlr;

   ctrlr = qinfo->ctrlr;
   cqInfo = qinfo->cqInfo;

   if (ctrlr->osRes.intrType == VMK_PCI_INTERRUPT_TYPE_MSIX) {
      vmkStatus = NVMEPCIEIntrUnregister(ctrlr->osRes.intrArray[cqInfo->intrIndex], qinfo);
      DPRINT_Q(ctrlr, "Free interrupt for cq %d, 0x%x.", cqInfo->id, vmkStatus);
      VMK_ASSERT(vmkStatus == VMK_OK);
   }

   vmkStatus = NVMEPCIEDmaFree(&ctrlr->osRes, &cqInfo->dmaEntry);
   cqInfo->compq = NULL;
   cqInfo->compqPhy = 0L;
   DPRINT_Q(ctrlr, "Free DMA buffer for cq %d, 0x%x.", cqInfo->id, vmkStatus);

   NVMEPCIELockDestroy(&cqInfo->lock);
   DPRINT_Q(ctrlr, "Free lock for cq %d.", cqInfo->id);

   NVMEPCIEFree(cqInfo);
   qinfo->cqInfo = NULL;
   DPRINT_Q(ctrlr, "Free cq %d.", qinfo->id);

   return vmkStatus;
}

/**
 * Allocate and initialize a submission queue
 *
 * @param[in] qinfo  Queue instance
 * @param[in] qid    Submission queue identifier
 * @param[in] qsize  Submission queue size
 *
 * @return VMK_OK on success, error code otherwise
 */
static
VMK_ReturnStatus SubQueueConstruct(NVMEPCIEQueueInfo *qinfo, int qid, int qsize)
{
   VMK_ReturnStatus vmkStatus;
   NVMEPCIESubQueueInfo *sqInfo;
   NVMEPCIEController *ctrlr;
   char lockName[VMK_MISC_NAME_MAX];

   ctrlr = qinfo->ctrlr;

   /** Allocate submission queue info struct */
   sqInfo = NVMEPCIEAlloc(sizeof(*sqInfo), 0);
   if (sqInfo == NULL) {
      EPRINT(ctrlr, "Failed to allocate sq %d.", qid);
      return VMK_NO_MEMORY;
   }

   qinfo->sqInfo = sqInfo;
   sqInfo->id = qid;
   sqInfo->qsize = qsize;

   /** Create submission queue lock */
   vmk_StringFormat(lockName, sizeof(lockName), NULL,
                    "sqLock-%s-%d",
                     NVMEPCIEGetCtrlrName(ctrlr), qid);

   vmkStatus = NVMEPCIELockCreate(ctrlr->osRes.lockDomain,
                                  NVME_LOCK_RANK_HIGH,
                                  lockName, &sqInfo->lock);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to create lock for sq %d, 0x%x.", qid, vmkStatus);
      goto free_sq;
   }

   /** Allocate submission queue DMA buffer */
   vmkStatus = NVMEPCIEDmaAlloc(&ctrlr->osRes,
                                qsize * sizeof(vmk_NvmeSubmissionQueueEntry),
                                &sqInfo->dmaEntry, VMK_TIMEOUT_UNLIMITED_MS);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to allocate DMA buffer for sq %d, 0x%x.", qid, vmkStatus);
      goto free_lock;
   }

   sqInfo->subq = (vmk_NvmeSubmissionQueueEntry *) sqInfo->dmaEntry.va;
   sqInfo->subqPhy = sqInfo->dmaEntry.ioa;
   sqInfo->doorbell = ctrlr->regs + VMK_NVME_REG_SQTDBL(qid, ctrlr->dstrd);

   return VMK_OK;

free_lock:
   NVMEPCIELockDestroy(&sqInfo->lock);

free_sq:
   NVMEPCIEFree(sqInfo);

   return vmkStatus;
}

/**
 * Destroy a submission queue
 *
 * @param[in] qinfo  Queue instance
 *
 * @return VMK_OK on success, error code otherwise
 */
static VMK_ReturnStatus
SubQueueDestroy(NVMEPCIEQueueInfo *qinfo)
{
   VMK_ReturnStatus vmkStatus;
   NVMEPCIESubQueueInfo *sqInfo;
   NVMEPCIEController *ctrlr;

   ctrlr = qinfo->ctrlr;
   sqInfo = qinfo->sqInfo;

   vmkStatus = NVMEPCIEDmaFree(&ctrlr->osRes, &sqInfo->dmaEntry);
   sqInfo->subq = NULL;
   sqInfo->subqPhy = 0L;
   DPRINT_Q(ctrlr, "Free DMA buffer for sq %d, 0x%x.", sqInfo->id, vmkStatus);

   NVMEPCIELockDestroy(&sqInfo->lock);
   DPRINT_Q(ctrlr, "Free lock for sq %d.", sqInfo->id);

   NVMEPCIEFree(sqInfo);
   qinfo->sqInfo = NULL;
   DPRINT_Q(ctrlr, "Free sq %d.", qinfo->id);

   return vmkStatus;
}

/**
 * Allocate and initialize command informantion list
 *
 * @param[in] qinfo  Queue instance
 * @param[in] qsize  Queue size
 *
 * @return VMK_OK on success, error code otherwise
 */
static VMK_ReturnStatus
CmdInfoListConstruct(NVMEPCIEQueueInfo *qinfo, int qsize)
{
   VMK_ReturnStatus vmkStatus;
   NVMEPCIECmdInfoList *cmdList;
   NVMEPCIECmdInfo *cmdInfo;
   NVMEPCIEController *ctrlr;
   char lockName[VMK_MISC_NAME_MAX];
   int i, idCount;

   ctrlr = qinfo->ctrlr;
   idCount = qsize * 2 + NVME_PCIE_SYNC_CMD_NUM;

   /** Allocate cmdInfoList struct */
   cmdList = NVMEPCIEAlloc(sizeof(*cmdList), 0);
   if (cmdList == NULL) {
      EPRINT(ctrlr, "Failed to allocate cmdList for queue %d.", qinfo->id);
      return VMK_NO_MEMORY;
   }

   qinfo->cmdList = cmdList;

   /** Create cmd list lock */
   vmk_StringFormat(lockName, sizeof(lockName), NULL,
                    "cmdListLock-%s-%d",
                     NVMEPCIEGetCtrlrName(ctrlr), qinfo->cqInfo->id);

   vmkStatus = NVMEPCIELockCreate(ctrlr->osRes.lockDomain,
                                  NVME_LOCK_RANK_HIGH,
                                  lockName, &cmdList->lock);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to create cmdList lock for queue %d, 0x%x.",
             qinfo->id, vmkStatus);
      goto free_cmdlist;
   }

   /** Allocate cmd info array */
   cmdInfo = NVMEPCIEAlloc(idCount * sizeof(*cmdInfo), 0);
   if (cmdInfo == NULL) {
      EPRINT(ctrlr, "Failed to allocate cmd info array for queue %d.", qinfo->id);
      vmkStatus = VMK_NO_MEMORY;
      goto free_lock;
   }

   cmdList->list = cmdInfo;
   cmdList->idCount = idCount;
   for (i = 1; i <= idCount; i++) {
      cmdInfo->cmdId = i;
      cmdInfo ++;
   }

   return VMK_OK;

free_lock:
   NVMEPCIELockDestroy(&cmdList->lock);

free_cmdlist:
   NVMEPCIEFree(cmdList);

   return vmkStatus;
}

/**
 * Destroy command info list
 *
 * @param[in] qinfo  Queue instance
 *
 * @return VMK_OK on success, error code otherwise
 */
static VMK_ReturnStatus
CmdInfoListDestroy(NVMEPCIEQueueInfo *qinfo)
{
   NVMEPCIECmdInfoList *cmdList;
   NVMEPCIECmdInfo *cmdInfo;
   NVMEPCIEController *ctrlr;

   ctrlr = qinfo->ctrlr;
   cmdList = qinfo->cmdList;
   cmdInfo = cmdList->list;

   NVMEPCIEFree(cmdList->list);
   cmdList->list = NULL;
   DPRINT_Q(ctrlr, "Free cmd info array for queue %d.", qinfo->id);

   NVMEPCIELockDestroy(&cmdList->lock);
   DPRINT_Q(ctrlr, "Free cmdList lock for queue %d.", qinfo->id);

   NVMEPCIEFree(cmdList);
   qinfo->cmdList = NULL;
   DPRINT_Q(ctrlr, "Free cmdList for queue %d.", qinfo->id);

   return VMK_OK;
}

#if NVME_STATS
/**
 * Allocate and initialize object of NVMEPCIEQueueStats for queue
 *
 * @param[in] qinfo  Queue instance
 *
 * @return VMK_OK on success, error code otherwise
 */
static VMK_ReturnStatus
QueueStatsContruct(NVMEPCIEQueueInfo *qinfo)
{
   NVMEPCIEController *ctrlr = qinfo->ctrlr;
   qinfo->stats = NVMEPCIEAlloc(sizeof(NVMEPCIEQueueStats), 0);
   if (NULL == qinfo->stats) {
      EPRINT(ctrlr, "Failed to allocate stats for queue %d", qinfo->id);
      return VMK_NO_MEMORY;
   }
   qinfo->stats->cqHead = 0;
   qinfo->stats->cqePhase = 1;
   qinfo->stats->intrCount = 0;
   return VMK_OK;
}

static VMK_ReturnStatus
QueueStatsDestroy(NVMEPCIEQueueInfo *qinfo)
{
   NVMEPCIEController *ctrlr = qinfo->ctrlr;
   NVMEPCIEFree(qinfo->stats);
   qinfo->stats = NULL;
   DPRINT_Q(ctrlr, "Free stats for queue %d", qinfo->id);
   return VMK_OK;
}

/**
 * Walk through CQ, collect nvme-stats.
 *
 * @param[in] qinfo      Queue instance
 * @param[in] countIntr  Whether to count interrupts
 */
static void
NVMEPCIEStatsWalkThrough(NVMEPCIEQueueInfo *qinfo, vmk_Bool countIntr)
{
   NVMEPCIECompQueueInfo *cqInfo = qinfo->cqInfo;
   NVMEPCIEController *ctrlr = qinfo->ctrlr;
   vmk_NvmeCompletionQueueEntry *cqEntry;
   NVMEPCIEQueueStats *stats;
   vmk_uint16 head, phase;
   NVMEPCIECmdInfo *cmdInfo;
   NVMEPCIECmdInfoList *cmdList;
   vmk_TimerCycles ts;

   if (!ctrlr->statsEnabled) {
      return;
   }
   stats = qinfo->stats;
   cmdList = qinfo->cmdList;
   /**
    * Walk through CQ, collect time stamp of arrival of entries.
    * Iteration should soon be done as it's simple memory accessing.
    * Take time stamp of very begining of iteration as precise
    * value for all CQ entries to save calling of vmk_GetTimerCycles.
    **/
   head = stats->cqHead;
   phase = stats->cqePhase;
   ts = vmk_GetTimerCycles();

   // In interruption mode, count interrupts while not in polling mode
   if (countIntr) {
      stats->intrCount ++;
   }

   while (1) {
      cqEntry = &cqInfo->compq[head];
      if (cqEntry->dw3.p != phase) {
         break;
      }
      if (ctrlr->abortEnabled) {
         cmdInfo = &cmdList->list[cqEntry->dw3.cid];
      } else {
         cmdInfo = &cmdList->list[cqEntry->dw3.cid - 1];
      }
      cmdInfo->doneByHwTs = ts;

      if (++head >= cqInfo->qsize) {
         head = 0;
         phase = !phase;
      }
   }
   if (!((head == stats->cqHead) && (stats->cqePhase == phase))) {
      stats->cqHead = head;
      stats->cqePhase = phase;
   }
}
#endif

/**
 * Allocate queue resources
 *
 * @note Caller should allocate the qinfo struct.
 *
 * @param[in] ctrlr      Controller instance
 * @param[in] qinfo      Queue instance
 * @param[in] qid        Queue identifier
 * @param[in] sqsize     Submission queue size
 * @param[in] cqsize     Completion queue size
 * @param[in] intrIndex  Index of vectors into MSIx table
 *
 * @return VMK_OK on success, error code otherwise
 */
static VMK_ReturnStatus
QueueConstruct(NVMEPCIEController *ctrlr, NVMEPCIEQueueInfo *qinfo,
               int qid, int sqsize, int cqsize, int intrIndex)
{
   VMK_ReturnStatus vmkStatus;

   qinfo->ctrlr = ctrlr;
   qinfo->id = qid;

   vmk_AtomicWrite32(&qinfo->state, NVME_PCIE_QUEUE_SUSPENDED);
   vmk_AtomicWrite32(&qinfo->refCount, 0);

   vmkStatus = CompQueueConstruct(qinfo, qid, cqsize, intrIndex);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to construct completion queue %d, 0x%x.", qid, vmkStatus);
      return vmkStatus;
   }

   vmkStatus = SubQueueConstruct(qinfo, qid, sqsize);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to construct submission queue %d, 0x%x.", qid, vmkStatus);
      goto destroy_cq;
   }

   VPRINT(ctrlr, "sq[%d].doorbell: 0x%lx, cq[%d].doorbell: 0x%lx",
          qid, qinfo->sqInfo->doorbell, qid, qinfo->cqInfo->doorbell);

   vmkStatus = CmdInfoListConstruct(qinfo, sqsize - 1);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to constrct command list %d, 0x%x.", qid, vmkStatus);
      goto destroy_sq;
   }

#ifdef NVME_STATS
   vmkStatus = QueueStatsContruct(qinfo);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to contruct object of statistics %d, 0x%0x", qid, vmkStatus);
      goto destroy_cmdinfolist;
   }
#endif

#if NVME_PCIE_STORAGE_POLL
   if (ctrlr->pollEnabled && (qinfo->id > 0)) {
      NVMEPCIEStoragePollCreate(qinfo);
   }
#endif

   return VMK_OK;

destroy_cmdinfolist:
   CmdInfoListDestroy(qinfo);

destroy_sq:
   SubQueueDestroy(qinfo);

destroy_cq:
   CompQueueDestroy(qinfo);

   vmk_AtomicWrite32(&qinfo->state, NVME_PCIE_QUEUE_NON_EXIST);

   return vmkStatus;
}

/**
 * Free queue resources
 *
 * @note This function only frees resources allocated by QueueConstruct().
 *       Since qinfo struct is not allocated by QueueConstruct(), qinfo is
 *       not freed in this function.
 *
 * @param[in] qinfo  Queue instance
 *
 * @return VMK_OK on success, error code otherwise
 */
static VMK_ReturnStatus
QueueDestroy(NVMEPCIEQueueInfo *qinfo)
{
   VMK_ReturnStatus vmkStatus;
   NVMEPCIEController *ctrlr = qinfo->ctrlr;

   vmk_AtomicWrite32(&qinfo->state, NVME_PCIE_QUEUE_NON_EXIST);
   while(vmk_AtomicRead32(&qinfo->refCount) != 0) {
      WPRINT(ctrlr, "Wait for queue refcount to be zero");
      vmk_WorldSleep(1000);
   }
#ifdef NVME_STATS
   vmkStatus = QueueStatsDestroy(qinfo);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to destroy object of statistics %d, 0x%x.",
             qinfo->id, vmkStatus);
   }
#endif

#if NVME_PCIE_STORAGE_POLL
   /**
    * Destroy poll handler if StoragePoll feature enabled and handler created
    * successfully
    */
   if (ctrlr->pollEnabled) {
      NVMEPCIEStoragePollDestory(qinfo);
   }
#endif

   vmkStatus = CmdInfoListDestroy(qinfo);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to destroy command list %d, 0x%x.", qinfo->id, vmkStatus);
   }

   vmkStatus = SubQueueDestroy(qinfo);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to destroy submission queue %d, 0x%x.",
             qinfo->sqInfo->id, vmkStatus);
   }

   vmkStatus = CompQueueDestroy(qinfo);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to destroy completion queue %d, 0x%x.",
             qinfo->cqInfo->id, vmkStatus);
   }

   return vmkStatus;
}

VMK_ReturnStatus
NVMEPCIECtrlMsiAck(void *handlerData, vmk_IntrCookie intrCookie)
{
   return VMK_OK;
}

void
NVMEPCIECtrlMsiHandler(void *handlerData, vmk_IntrCookie intrCookie)
{
   NVMEPCIEController *ctrlr = (NVMEPCIEController *)handlerData;
   NVMEPCIEQueueInfo *qinfo = (NVMEPCIEQueueInfo *)handlerData;
   int i;

   NVMEPCIEQueueIntrHandler(&ctrlr->queueList[0], intrCookie);

   for (i = 1; i <= ctrlr->numIoQueues; i++) {
      qinfo = &ctrlr->queueList[i];
      vmk_SpinlockLock(qinfo->cqInfo->lock);
      NVMEPCIEProcessCq(qinfo);
      vmk_SpinlockUnlock(qinfo->cqInfo->lock);
   }
}

/**
 * Acknowledge interrupt
 *
 * @param[in] handlerData  Queue instance
 * @param[in] intrCookie   Interrupt cookie
 *
 * @return VMK_OK Interrupt acknowledged
 */
VMK_ReturnStatus
NVMEPCIEQueueIntrAck(void *handlerData, vmk_IntrCookie intrCookie)
{
#if NVME_STATS
   NVMEPCIEQueueInfo *qinfo = (NVMEPCIEQueueInfo *)handlerData;

   NVMEPCIEStatsWalkThrough(qinfo, VMK_TRUE);
#endif
   return VMK_OK;
}

/**
 * Interrupt handler
 *
 * Handles interrupts by processing completion queues
 *
 * @param[in] handlerData  Queue instance
 * @param[in] intrCookie   Interrupt cookie
 */
void
NVMEPCIEQueueIntrHandler(void *handlerData, vmk_IntrCookie intrCookie)
{
   NVMEPCIEQueueInfo *qinfo = (NVMEPCIEQueueInfo *)handlerData;
#if NVME_PCIE_STORAGE_POLL
   vmk_StoragePollState pollState = VMK_STORAGEPOLL_DISABLED;
#endif

#if NVME_PCIE_STORAGE_POLL
   /**
    * To avoid the following unnecessary process when interrupt has been
    * disabled.
    */
   if (!vmk_AtomicRead8(&qinfo->isIntrEnabled)) {
      return;
   }

   if (NVMEPCIEStoragePollSwitch(qinfo)) {
      vmk_StoragePollCheckState(qinfo->pollHandler, &pollState);
      if (VMK_LIKELY(pollState != VMK_STORAGEPOLL_DISABLED)) {
         // Do not synchronize interrupt here to avoid endless waiting
         NVMEPCIEDisableIntr(qinfo, VMK_FALSE);
         vmk_StoragePollActivate(qinfo->pollHandler);
      }
   } else {
      vmk_SpinlockLock(qinfo->cqInfo->lock);
      NVMEPCIEProcessCq(qinfo);
      vmk_SpinlockUnlock(qinfo->cqInfo->lock);
   }
#else
   vmk_SpinlockLock(qinfo->cqInfo->lock);
   NVMEPCIEProcessCq(qinfo);
   vmk_SpinlockUnlock(qinfo->cqInfo->lock);
#endif
}

static inline vmk_uint32
NVMEPCIEFlushFreeCmdInfo(NVMEPCIEQueueInfo *qinfo)
{
   NVMEPCIECmdInfoList *cmdList = qinfo->cmdList;
   NVMEPCIEPendingCmdInfo oldValue;
   do {
      oldValue.atomicComposite = vmk_AtomicRead64(
                                    &cmdList->pendingFreeCmdList.atomicComposite);
      if (oldValue.cmdOffset == 0) {
         VMK_ASSERT(oldValue.freeListLength == 0);
         return 0;
      }
   } while (vmk_AtomicReadIfEqualWrite64(&cmdList->pendingFreeCmdList.atomicComposite,
                                         oldValue.atomicComposite, (vmk_uint64)0)
            != oldValue.atomicComposite);
   return oldValue.cmdOffset;
}

static NVMEPCIECmdInfo*
NVMEPCIEGetCmdInfo(NVMEPCIEQueueInfo *qinfo, vmk_uint16 cid)
{
   NVMEPCIECmdInfo *cmdInfo = NULL;
   NVMEPCIEController *ctrlr = qinfo->ctrlr;
   NVMEPCIECmdInfoList *cmdList = qinfo->cmdList;
   int i;

   if (VMK_LIKELY(cid != NVME_PCIE_SYNC_CMD_ID)) {
      cmdInfo = &cmdList->list[cid];
      VMK_ASSERT(vmk_AtomicRead32(&cmdInfo->atomicStatus) == NVME_PCIE_CMD_STATUS_FREE);
      vmk_AtomicWrite32(&cmdInfo->atomicStatus, NVME_PCIE_CMD_STATUS_ACTIVE);
   } else {
      for (i = cmdList->idCount; i > cmdList->idCount - NVME_PCIE_SYNC_CMD_NUM; i--) {
         cmdInfo = &cmdList->list[i-1];
         if (vmk_AtomicReadIfEqualWrite32(&cmdInfo->atomicStatus,
                                          NVME_PCIE_CMD_STATUS_FREE,
                                          NVME_PCIE_CMD_STATUS_ACTIVE)
                                       == NVME_PCIE_CMD_STATUS_FREE) {
            break;
         }
      }
      if (i == cmdList->idCount - NVME_PCIE_SYNC_CMD_NUM) {
         WPRINT(ctrlr, "Failed to get free command info.");
         return NULL;
      }
   }

   vmk_AtomicInc32(&qinfo->cmdList->nrAct);

#ifdef NVME_STATS
   cmdInfo->sendToHwTs = 0;
   cmdInfo->doneByHwTs = 0;
   cmdInfo->statsOn = VMK_FALSE;
#endif
   DPRINT_CMD(ctrlr, "Get cmd info [%d] %p from queue [%d].",
              cmdInfo->cmdId, cmdInfo, qinfo->id);

   return cmdInfo;

}

/**
 * Get a command info from a queue
 *
 * @param[in] qinfo  Queue instance
 *
 * @return pointer to the command info
 * @return NULL if quue is full
 *
 * @note It is assumed that cmdList lock is held by caller.
 */
static NVMEPCIECmdInfo*
NVMEPCIEGetCmdInfoLegacy(NVMEPCIEQueueInfo *qinfo)
{
   NVMEPCIECmdInfo *cmdInfo;
   NVMEPCIEController *ctrlr = qinfo->ctrlr;
   NVMEPCIECmdInfoList *cmdList = qinfo->cmdList;

   vmk_SpinlockLock(cmdList->lock);

   if (VMK_UNLIKELY(cmdList->freeCmdList == 0)) {
      cmdList->freeCmdList = NVMEPCIEFlushFreeCmdInfo(qinfo);
      if (VMK_UNLIKELY(cmdList->freeCmdList == 0)) {
         /**
          * There shouldn't be queue full errors as vmknvme knows the number of
          * active commands and won't issue commands when there is no free slot.
          */
         WPRINT(ctrlr, "Queue[%d] command list empty. %d", qinfo->id,
                vmk_AtomicRead32(&cmdList->nrAct));
         vmk_SpinlockUnlock(cmdList->lock);
         return NULL;
      }
   }

   cmdInfo = &cmdList->list[cmdList->freeCmdList-1];
   cmdList->freeCmdList = cmdInfo->freeLink;
   vmk_AtomicInc32(&cmdList->nrAct);
   vmk_AtomicWrite32(&cmdInfo->atomicStatus, NVME_PCIE_CMD_STATUS_ACTIVE);

   vmk_SpinlockUnlock(cmdList->lock);
#ifdef NVME_STATS
   cmdInfo->sendToHwTs = 0;
   cmdInfo->doneByHwTs = 0;
   cmdInfo->statsOn = VMK_FALSE;
#endif
   DPRINT_CMD(ctrlr, "Get cmd info [%d] %p from queue [%d].",
              cmdInfo->cmdId, cmdInfo, qinfo->id);

   return cmdInfo;
}

static inline void
NVMEPCIEPushCmdInfo(NVMEPCIEQueueInfo *qinfo, NVMEPCIECmdInfo *cmdInfo)
{
   NVMEPCIEPendingCmdInfo oldValue, newValue;
   NVMEPCIECmdInfoList *cmdList = qinfo->cmdList;
   VMK_ASSERT(cmdInfo == &cmdList->list[cmdInfo->cmdId-1]);
   do {
      oldValue.atomicComposite = vmk_AtomicRead64(
                                    &cmdList->pendingFreeCmdList.atomicComposite);
      cmdInfo->freeLink = oldValue.cmdOffset;
      newValue.cmdOffset = cmdInfo->cmdId;
      newValue.freeListLength = oldValue.freeListLength+1;
   } while (vmk_AtomicReadIfEqualWrite64(&cmdList->pendingFreeCmdList.atomicComposite,
                                         oldValue.atomicComposite,
                                         newValue.atomicComposite)
            != oldValue.atomicComposite);
}

/**
 * Put a command info to a queue
 *
 * @param[in] qinfo    Queue instance
 * @param[in] cmdInfo  Command info
 *
 * @note It is assumed that queue lock is held by caller.
 */
static void
NVMEPCIEPutCmdInfo(NVMEPCIEQueueInfo *qinfo, NVMEPCIECmdInfo *cmdInfo)
{
   NVMEPCIEController *ctrlr = qinfo->ctrlr;
   vmk_AtomicWrite32(&cmdInfo->atomicStatus, NVME_PCIE_CMD_STATUS_FREE);

   vmk_AtomicDec32(&qinfo->cmdList->nrAct);

   if (!ctrlr->abortEnabled) {
      NVMEPCIEPushCmdInfo(qinfo, cmdInfo);
   }
   DPRINT_CMD(ctrlr, "Put cmd Info [%d] %p back to queue [%d], nrAct: %d.",
              cmdInfo->cmdId, cmdInfo, qinfo->id,
              vmk_AtomicRead32(&qinfo->cmdList->nrAct));
}

/**
 * Get block size of a 'vmk_NvmeCommand' type command
 *
 * @param[in] vmkCmd  'vmk_NvmeCommand' type command
 *
 * @return    0       Cannot get block size of this command
 * @return    Not 0   Block size of this command
 */
inline vmk_uint16
NVMEPCIEGetCmdBlockSize(vmk_NvmeCommand *vmkCmd)
{
   vmk_uint16 bs = 0;

   if (vmkCmd == NULL) {
      return 0;
   }

   switch (vmkCmd->nvmeCmd.cdw0.opc) {
      case VMK_NVME_NVM_CMD_READ:
         bs = (((vmk_NvmeReadCmd *)(&vmkCmd->nvmeCmd))->cdw12.nlb) >> 1;
         break;

      case VMK_NVME_NVM_CMD_WRITE:
         bs = (((vmk_NvmeReadCmd *)(&vmkCmd->nvmeCmd))->cdw12.nlb) >> 1;
         break;

      case VMK_NVME_NVM_CMD_COMPARE:
         bs = (((vmk_NvmeReadCmd *)(&vmkCmd->nvmeCmd))->cdw12.nlb) >> 1;
         break;

      case VMK_NVME_NVM_CMD_WRITE_ZEROES:
         bs = (((vmk_NvmeReadCmd *)(&vmkCmd->nvmeCmd))->cdw12.nlb) >> 1;
         break;

      default:
         /**
          * Other types of commands don't have 'nlb' field, remains 'bs'
          * as 0 to claim unavailable.
          */
         break;
   }

   return bs;
}

/**
 * Submit a command to a queue
 *
 * @param[in] ctrlr   Controller instance
 * @param[in] vmkCmd  NVME command
 * @param[in] qid     Queue ID
 *
 * @return VMK_OK Command submitted successfully
 * @return VMK_FAILURE Failed to submit command
 */
VMK_ReturnStatus
NVMEPCIESubmitAsyncCommand(NVMEPCIEController *ctrlr,
                           vmk_NvmeCommand *vmkCmd,
                           vmk_uint32 qid)
{
   NVMEPCIECmdInfo *cmdInfo;
   NVMEPCIEQueueInfo *qinfo;
   vmk_NvmeStatus nvmeStatus;
   vmk_uint16 cid;
#if NVME_PCIE_BLOCKSIZE_AWARE
   vmk_uint16 bs = NVMEPCIEGetCmdBlockSize(vmkCmd);
#endif

   qinfo = &ctrlr->queueList[qid];
   vmk_AtomicInc32(&qinfo->refCount);
   if (vmk_AtomicRead32(&qinfo->state) != NVME_PCIE_QUEUE_ACTIVE) {
      vmkCmd->nvmeStatus = VMK_NVME_STATUS_VMW_IN_RESET;
      vmk_AtomicDec32(&qinfo->refCount);
      return VMK_FAILURE;
   }

   if (ctrlr->abortEnabled) {
      cid = vmkCmd->nvmeCmd.cdw0.cid;
      VMK_ASSERT(cid < qinfo->cmdList->idCount - NVME_PCIE_SYNC_CMD_NUM);
      if (VMK_UNLIKELY(cid >= qinfo->cmdList->idCount - NVME_PCIE_SYNC_CMD_NUM)) {
         vmkCmd->nvmeStatus = VMK_NVME_STATUS_VMW_BAD_PARAMETER;
         vmk_AtomicDec32(&qinfo->refCount);
         return VMK_FAILURE;
      }
      cmdInfo = NVMEPCIEGetCmdInfo(qinfo, cid);
   } else {
      cmdInfo = NVMEPCIEGetCmdInfoLegacy(qinfo);
   }
   if (cmdInfo == NULL) {
      vmkCmd->nvmeStatus = VMK_NVME_STATUS_VMW_QUEUE_FULL;
      vmk_AtomicDec32(&qinfo->refCount);
      return VMK_FAILURE;
   }

#if NVME_PCIE_BLOCKSIZE_AWARE
   if (ctrlr->blkSizeAwarePollEnabled && (bs > 0) &&
       (bs <= NVME_PCIE_SMALL_BLOCKSIZE)) {
      vmk_AtomicInc32(&qinfo->cmdList->nrActSmall);
   }
#endif

   cmdInfo->vmkCmd = vmkCmd;
   cmdInfo->type = NVME_PCIE_ASYNC_CONTEXT;

   nvmeStatus = NVMEPCIEIssueCommandToHw(qinfo, cmdInfo, NVMEPCIECompleteAsyncCommand);

   if (VMK_UNLIKELY(nvmeStatus != VMK_NVME_STATUS_VMW_WOULD_BLOCK)) {
      vmkCmd->nvmeStatus = nvmeStatus;
      WPRINT(ctrlr, "Failed to issue command %d, 0x%x", cmdInfo->cmdId, nvmeStatus);
#if NVME_PCIE_BLOCKSIZE_AWARE
      if (qinfo->ctrlr->blkSizeAwarePollEnabled && (bs > 0) &&
          (bs <= NVME_PCIE_SMALL_BLOCKSIZE)) {
         vmk_AtomicDec32(&qinfo->cmdList->nrActSmall);
      }
#endif
      NVMEPCIEPutCmdInfo(qinfo, cmdInfo);
      vmk_AtomicDec32(&qinfo->refCount);
      return VMK_FAILURE;
   }

   vmk_AtomicDec32(&qinfo->refCount);
   return VMK_OK;
}

static NVMEPCIEDmaEntry*
PrepareDmaEntry(NVMEPCIEController *ctrlr,
                vmk_NvmeCommand *vmkCmd,
                vmk_uint8 *buf,
                vmk_uint32 length)
{
   NVMEPCIEDmaEntry *dmaEntry = NULL;
   VMK_ReturnStatus vmkStatus;
   vmk_uint8 opcode = vmkCmd->nvmeCmd.cdw0.opc;

   if (length == 0 || buf == NULL) {
      return NULL;
   }

   if ((opcode & 0x3) != 0x1 && (opcode & 0x3) != 0x2) {
      return NULL;
   }

   dmaEntry = NVMEPCIEAlloc(sizeof(NVMEPCIEDmaEntry), 0);
   if (dmaEntry == NULL) {
      EPRINT(ctrlr, "Failed to allocate dma entry.");
      return NULL;
   }

   vmkStatus = NVMEPCIEDmaAlloc(&ctrlr->osRes, length, dmaEntry, VMK_TIMEOUT_UNLIMITED_MS);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to allocate dma buffer, 0x%x.", vmkStatus);
      NVMEPCIEFree(dmaEntry);
      return NULL;
   }

   if ((opcode & 0x3) == 0x1) {
      vmk_Memcpy((void *)dmaEntry->va, buf, length);
      dmaEntry->direction = VMK_DMA_DIRECTION_FROM_MEMORY;
   } else {
      dmaEntry->direction = VMK_DMA_DIRECTION_TO_MEMORY;
   }
   return dmaEntry;
}

/**
 * Submit a command to a queue, and wait for its completion
 *
 * @param[in] qinfo      Queue instance
 * @param[in] vmkCmd     Nvme command
 * @param[in] qid        Queue ID
 * @param[in] buf        virtual addr of data transfer buffer
 * @param[in] length     length of data transfer buffer
 * @param[in] timeoutUs  Command timeout in microseconds
 *
 * @return VMK_OK Command submitted and completed
 * @return VMK_TIMEOUT Command submitted but not completed
 * @return VMK_FAILURE Failed to submit command
 *
 * Note: Caller shouldn't free vmkCmd if this function returns TIMEOUT.
 */
VMK_ReturnStatus
NVMEPCIESubmitSyncCommand(NVMEPCIEController *ctrlr,
                          vmk_NvmeCommand *vmkCmd,
                          vmk_uint32 qid,
                          vmk_uint8 *buf,
                          vmk_uint32 length,
                          int timeoutUs)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;
   NVMEPCIEQueueInfo *qinfo;
   NVMEPCIECmdInfo *cmdInfo;
   vmk_uint64 timeout = 0;
   vmk_NvmeStatus nvmeStatus;
   vmk_atomic32 existingStatus;
   NVMEPCIEDmaEntry *dmaEntry = NULL;

   /**
    * Currently there is no need to support commands which transfer
    * large amount of data and need prp list.
    */
   if (length > VMK_PAGE_SIZE) {
      return VMK_NOT_SUPPORTED;
   }

   qinfo = &ctrlr->queueList[qid];
   vmk_AtomicInc32(&qinfo->refCount);
   if (vmk_AtomicRead32(&qinfo->state) != NVME_PCIE_QUEUE_ACTIVE) {
      vmkCmd->nvmeStatus = VMK_NVME_STATUS_VMW_IN_RESET;
      vmk_AtomicDec32(&qinfo->refCount);
      return VMK_FAILURE;
   }

   if (ctrlr->abortEnabled) {
      cmdInfo = NVMEPCIEGetCmdInfo(qinfo, NVME_PCIE_SYNC_CMD_ID);
   } else {
      cmdInfo = NVMEPCIEGetCmdInfoLegacy(qinfo);
   }

   if (cmdInfo == NULL) {
      vmkCmd->nvmeStatus = VMK_NVME_STATUS_VMW_QUEUE_FULL;
      vmk_AtomicDec32(&qinfo->refCount);
      return VMK_FAILURE;
   }

   vmkCmd->nvmeCmd.cdw0.cid = cmdInfo->cmdId - 1;
   if (length > 0) {
      dmaEntry = PrepareDmaEntry(ctrlr, vmkCmd, buf, length);
      if (dmaEntry == NULL) {
         vmkCmd->nvmeStatus = VMK_NVME_STATUS_VMW_NO_MEMORY;
         NVMEPCIEPutCmdInfo(qinfo, cmdInfo);
         vmk_AtomicDec32(&qinfo->refCount);
         return VMK_FAILURE;
      }
      vmkCmd->nvmeCmd.dptr.prps.prp1.pbao = dmaEntry->ioa;
      cmdInfo->doneData = dmaEntry;
   } else {
      cmdInfo->doneData = NULL;
   }

   cmdInfo->vmkCmd = vmkCmd;
   cmdInfo->type = NVME_PCIE_SYNC_CONTEXT;

   nvmeStatus = NVMEPCIEIssueCommandToHw(qinfo, cmdInfo,
                                         NVMEPCIECompleteSyncCommand);
   if (nvmeStatus != VMK_NVME_STATUS_VMW_WOULD_BLOCK) {
      vmkCmd->nvmeStatus = nvmeStatus;
      if (dmaEntry != NULL) {
         NVMEPCIEDmaFree(&ctrlr->osRes, dmaEntry);
         NVMEPCIEFree(dmaEntry);
         cmdInfo->doneData = NULL;
      }
      NVMEPCIEPutCmdInfo(qinfo, cmdInfo);
      vmk_AtomicDec32(&qinfo->refCount);
      return VMK_FAILURE;
   }

   timeout = NVMEPCIEGetTimerUs() + timeoutUs;
   do {
      vmkStatus = vmk_WorldWait((vmk_WorldEventID)cmdInfo, VMK_LOCK_INVALID,
                                timeoutUs / 1000, __FUNCTION__);
   } while(vmkStatus == VMK_OK &&
           vmk_AtomicRead32(&cmdInfo->atomicStatus) == NVME_PCIE_CMD_STATUS_ACTIVE &&
           timeout > NVMEPCIEGetTimerUs());

   do {
      existingStatus = vmk_AtomicRead32(&cmdInfo->atomicStatus);
      if (existingStatus == NVME_PCIE_CMD_STATUS_DONE) {
         vmk_AtomicDec32(&qinfo->refCount);
         if (dmaEntry != NULL) {
            if (dmaEntry->direction == VMK_DMA_DIRECTION_TO_MEMORY) {
               vmk_Memcpy(buf, (void *)dmaEntry->va, length);
            }
            NVMEPCIEDmaFree(&ctrlr->osRes, dmaEntry);
            NVMEPCIEFree(dmaEntry);
            vmkCmd->doneData = NULL;
         }
         NVMEPCIEPutCmdInfo(qinfo, cmdInfo);
         return VMK_OK;
      }
   } while(vmk_AtomicReadIfEqualWrite32(&cmdInfo->atomicStatus,
                                        existingStatus,
                                        NVME_PCIE_CMD_STATUS_FREE_ON_COMPLETE)
           != existingStatus);

   vmk_AtomicDec32(&qinfo->refCount);
   return VMK_TIMEOUT;
}

/**
 * Command completion callback for asynchronous command
 *
 * @param[in] qinfo    Queue instance
 * @param[in] cmdInfo  Command info
 */
static void
NVMEPCIECompleteAsyncCommand(NVMEPCIEQueueInfo *qinfo,
                             NVMEPCIECmdInfo *cmdInfo)
{
   vmk_NvmeCommand *vmkCmd = cmdInfo->vmkCmd;
   vmk_AtomicWrite32(&cmdInfo->atomicStatus, NVME_PCIE_CMD_STATUS_DONE);
   NVMEPCIEDumpCommand(qinfo->ctrlr, vmkCmd);
#if NVME_PCIE_BLOCKSIZE_AWARE
   vmk_uint16 bs = NVMEPCIEGetCmdBlockSize(vmkCmd);
   if (qinfo->ctrlr->blkSizeAwarePollEnabled && (bs > 0) &&
       (bs <= NVME_PCIE_SMALL_BLOCKSIZE)) {
      vmk_AtomicDec32(&qinfo->cmdList->nrActSmall);
   }
#endif
   NVMEPCIEPutCmdInfo(qinfo, cmdInfo);
   vmkCmd->done(vmkCmd);
}

/**
 * Command completion callback for synchronous command
 *
 * @param[in] qinfo    Queue instance
 * @param[in] cmdInfo  Command info
 */
static void
NVMEPCIECompleteSyncCommand(NVMEPCIEQueueInfo *qinfo, NVMEPCIECmdInfo *cmdInfo)
{
   vmk_atomic32 existingStatus;
   NVMEPCIEDmaEntry *dmaEntry;

   do {
      existingStatus = vmk_AtomicRead32(&cmdInfo->atomicStatus);
      if (VMK_UNLIKELY(existingStatus == NVME_PCIE_CMD_STATUS_FREE_ON_COMPLETE)) {
         /** Command timeout, free resources here. */
         if (cmdInfo->doneData) {
            dmaEntry = (NVMEPCIEDmaEntry *)cmdInfo->doneData;
            NVMEPCIEDmaFree(&qinfo->ctrlr->osRes, dmaEntry);
            NVMEPCIEFree(dmaEntry);
            cmdInfo->doneData = NULL;
         }
         NVMEPCIEFree(cmdInfo->vmkCmd);
         NVMEPCIEPutCmdInfo(qinfo, cmdInfo);
         return;
      }
   } while(vmk_AtomicReadIfEqualWrite32(&cmdInfo->atomicStatus,
                                        existingStatus,
                                        NVME_PCIE_CMD_STATUS_DONE)
           != existingStatus);

   vmk_WorldWakeup((vmk_WorldEventID) cmdInfo);
}

static inline void
NVMEPCIEUpdateSubQueueHead(NVMEPCIESubQueueInfo *sqInfo)
{
   vmk_uint32 sqHead;
   sqHead = vmk_AtomicReadWrite32(&sqInfo->pendingHead, NVME_INVALID_SQ_HEAD);
   if (sqHead != NVME_INVALID_SQ_HEAD) {
      sqInfo->head = (vmk_uint16)sqHead;
   }
}

/**
 * Issue a command to hardware
 *
 * @param[in] qinfo    Queue instance
 * @param[in] cmdInfo  Command info
 * @param[in] cb       Command completion callback
 *
 * @return VMK_NVME_STATUS_VMW_WOULD_BLOCK Command submitted to hardware successfully
 * @return VMK_NVME_STATUS_VMW_QFULL Failed to submit command due to queue being full
 */
static vmk_NvmeStatus
NVMEPCIEIssueCommandToHw(NVMEPCIEQueueInfo *qinfo,
                         NVMEPCIECmdInfo *cmdInfo,
                         NVMEPCIECompleteCommandCb cb)
{
   NVMEPCIESubQueueInfo *sqInfo = qinfo->sqInfo;
   vmk_uint16 tail;
   vmk_uint16 head;

   vmk_SpinlockLock(sqInfo->lock);
   head = sqInfo->head;
   tail = sqInfo->tail;

   cmdInfo->done = cb;

   if (VMK_UNLIKELY((head == tail + 1) || (head == 0 && tail == sqInfo->qsize - 1))) {
      NVMEPCIEUpdateSubQueueHead(sqInfo);
      head = sqInfo->head;
   }

   if (VMK_UNLIKELY((head == tail + 1) || (head == 0 && tail == sqInfo->qsize - 1))) {
      vmk_SpinlockUnlock(sqInfo->lock);
      return VMK_NVME_STATUS_VMW_QUEUE_FULL;
   }

   if (VMK_UNLIKELY(vmk_AtomicRead32(&qinfo->state) == NVME_PCIE_QUEUE_SUSPENDED)) {
      vmk_SpinlockUnlock(sqInfo->lock);
      return VMK_NVME_STATUS_VMW_IN_RESET;
   }

   if (VMK_UNLIKELY(qinfo->ctrlr->isRemoved)) {
      vmk_SpinlockUnlock(sqInfo->lock);
      return VMK_NVME_STATUS_VMW_QUIESCED;
   }

   vmk_Memcpy(&sqInfo->subq[tail], &cmdInfo->vmkCmd->nvmeCmd, VMK_NVME_SQE_SIZE);
   NVMEPCIEDumpSqe(qinfo->ctrlr, &cmdInfo->vmkCmd->nvmeCmd);
   if (!qinfo->ctrlr->abortEnabled) {
      sqInfo->subq[tail].cdw0.cid = cmdInfo->cmdId;
   }

   tail ++;
   if (tail >= sqInfo->qsize) {
      tail = 0;
   }

#ifdef NVME_STATS
   if (qinfo->ctrlr->statsEnabled) {
      cmdInfo->sendToHwTs = vmk_GetTimerCycles();
      cmdInfo->statsOn = VMK_TRUE;
   }
#endif
   NVMEPCIEWritel(tail, sqInfo->doorbell);
   sqInfo->tail = tail;
   vmk_SpinlockUnlock(sqInfo->lock);

   return VMK_NVME_STATUS_VMW_WOULD_BLOCK;
}

#if NVME_PCIE_STORAGE_POLL
/**
 * Poll routine for the IO queue defined in vmkapi_storage_poll.h.
 *
 * @param[in]  driverData  NVMEPCIEQueueInfo passed to poll handler when
 *                         creating
 * @param[in]  leastPoll   Minimum number of IO commands to be processed
 *                         in this invocation.
 * @param[in]  budget      Maximum number of IO commands to be processed
 *                         in this invocation.
 *
 * @return                 The number of completed IO commands.
 */
vmk_uint32
NVMEPCIEStoragePollCB(vmk_AddrCookie driverData,          // IN
                      vmk_uint32 leastPoll,               // IN
                      vmk_uint32 budget)                  // IN
{
   vmk_StoragePollState pollState = VMK_STORAGEPOLL_DISABLED;
   NVMEPCIEQueueInfo *qinfo = driverData.ptr;
   vmk_StoragePoll pollHandler = qinfo->pollHandler;
   vmk_uint32 ret = 0;
   vmk_Bool needPoll = VMK_FALSE;

   if (VMK_LIKELY(budget != 0)) {
      NVMEPCIEStoragePollAccumCmd(qinfo, leastPoll);

      vmk_SpinlockLock(qinfo->cqInfo->lock);
#if NVME_STATS
      NVMEPCIEStatsWalkThrough(qinfo, VMK_FALSE);
#endif
      ret += NVMEPCIEProcessCq(qinfo);
      vmk_SpinlockUnlock(qinfo->cqInfo->lock);

      /** Check if the number of completed IO commands is valid */
      if (ret >= leastPoll && ret <= budget) {
         needPoll = VMK_TRUE;
      }
   }

   vmk_StoragePollCheckState(pollHandler, &pollState);
   if ((!needPoll) &&
       VMK_LIKELY(pollState != VMK_STORAGEPOLL_DISABLED)) {
      NVMEPCIEEnableIntr(qinfo);

      /**
       * @brief Avoid Dead CQE
       *
       * Consider a situation below, when it comes to IO ending, just after
       * this callback invocation, and before enabling CQ's interruption
       * devices may post new CQEs whose interrupts cannot be acknowledged
       * due to Edge Trigger mode of NVMe, which results in Dead CQE.
       *
       * Just invoke NVMEPCIEProcessCq() once again to avoid.
       */
      vmk_SpinlockLock(qinfo->cqInfo->lock);
#if NVME_STATS
      NVMEPCIEStatsWalkThrough(qinfo, VMK_FALSE);
#endif
      NVMEPCIEProcessCq(qinfo);
      vmk_SpinlockUnlock(qinfo->cqInfo->lock);
   } else if (VMK_UNLIKELY(pollState == VMK_STORAGEPOLL_DISABLED)) {
      vmk_AtomicWrite8(&qinfo->isPollHdlrEnabled, VMK_FALSE);
   }

   return ret;
}

/**
 * Delay some time to accumulate adequate IO commands to be polled.
 *
 * @param[in]  qinfo       Queue instance
 * @param[in]  leastPoll   Minimum number of IO commands to be processed
 *                         in this invocation.
 */
void
NVMEPCIEStoragePollAccumCmd(NVMEPCIEQueueInfo *qinfo,   // IN
                            vmk_uint32 leastPoll)       // IN
{
   // Get pending info in CQ
   NVMEPCIECompQueueInfo *cqInfo = qinfo->cqInfo;
   vmk_uint32 tryPollTimes = 0;
   vmk_uint32 tryLen = leastPoll;
   vmk_NvmeCompletionQueueEntry *cqEntry;
   vmk_uint16 head, tryHead, phase;
   vmk_uint32 qsize = cqInfo->qsize;

   /** Try to access CQE as more as possible. */
   tryLen = (tryLen > qsize) ? qsize : tryLen;

   /**
    * TODO:
    * Maximum delay times, may be determined dynamically in future.
    */
   while (tryPollTimes < 3) {
      /** Determine number of CQEs */
      head = cqInfo->head;
      tryHead = head + tryLen;
      phase = cqInfo->phase;
      if (tryHead >= qsize) {
         tryHead -= qsize;
         phase = !phase;
      }

      cqEntry = &cqInfo->compq[tryHead];
      if (cqEntry->dw3.p != phase) {
         tryPollTimes++;

         /**
          * TODO:
          * Delay time, may be determined dynamically in future.
          */
         vmk_WorldSleep(50);
      } else {
         break;
      }
   }
}

void
NVMEPCIEStoragePollSetup(NVMEPCIEController *ctrlr)
{
   vmk_NvmeQueueID qid;
   NVMEPCIEQueueInfo *qinfo = NULL;

   for (qid = 1; qid <= vmk_AtomicRead32(&ctrlr->numIoQueues); qid++) {
      qinfo = &ctrlr->queueList[qid];

      NVMEPCIEStoragePollCreate(qinfo);
      NVMEPCIEStoragePollEnable(qinfo);
   }
}

void
NVMEPCIEStoragePollCreate(NVMEPCIEQueueInfo *qinfo)
{
   VMK_ReturnStatus status = VMK_OK;
   NVMEPCIEController *ctrlr = qinfo->ctrlr;
   vmk_StoragePollProps propInit;
   const char *adapterName = NULL;

   if (vmk_AtomicRead32(&qinfo->state) == NVME_PCIE_QUEUE_NON_EXIST) {
      return;
   }

   adapterName = vmk_NvmeGetAdapterName(ctrlr->osRes.vmkAdapter);
   if ((adapterName == NULL) ||
       (vmk_Strnlen(adapterName, VMK_MISC_NAME_MAX) == 0)) {
      return;
   }

   if (qinfo->pollHandler == NULL) {
      propInit.moduleID = vmk_ModuleCurrentID;
      propInit.pollObjectID = qinfo->id;
      propInit.heapID = NVME_PCIE_DRIVER_RES_HEAP_ID;
      vmk_NameInitialize(&propInit.adapterName, adapterName);
      propInit.driverData.ptr = qinfo;
      propInit.pollCb = NVMEPCIEStoragePollCB;

      status = vmk_StoragePollCreate(&propInit, &qinfo->pollHandler);
      if (VMK_OK != status) {
         EPRINT(ctrlr, "Failed to create storagePoll handler for queue %d!"
                       " Return to interruption mode for this queue.",
                qinfo->id);

         /** Set as NULL to claim that failed to create poll handler */
         qinfo->pollHandler = NULL;
         vmk_AtomicWrite8(&qinfo->isPollHdlrEnabled, VMK_FALSE);
      } else {
         vmk_StoragePollSetInterval(qinfo->pollHandler, nvmePCIEPollInterval);
      }
   }
}

void
NVMEPCIEStoragePollEnable(NVMEPCIEQueueInfo *qinfo)
{
   VMK_ReturnStatus status = VMK_OK;
   NVMEPCIEController *ctrlr = qinfo->ctrlr;

   if (qinfo->pollHandler == NULL) {
      return;
   }

   if (!vmk_AtomicReadIfEqualWrite8(&qinfo->isPollHdlrEnabled, VMK_FALSE,
                                    VMK_TRUE)) {
      status = vmk_StoragePollEnable(qinfo->pollHandler);
      if (status != VMK_OK) {
         WPRINT(ctrlr, "Failed to enable poll handler %p for queue %d"
                       " due to %s! Return to interruption mode for"
                       " this queue.",
                qinfo->pollHandler, qinfo->id, vmk_StatusToString(status));

         vmk_StoragePollDestroy(qinfo->pollHandler);
         qinfo->pollHandler = NULL;
         vmk_AtomicWrite8(&qinfo->isPollHdlrEnabled, VMK_FALSE);
      }
   }
}

void
NVMEPCIEStoragePollDisable(NVMEPCIEQueueInfo *qinfo)
{
   if ((qinfo->pollHandler != NULL) &&
       (vmk_AtomicReadIfEqualWrite8(&qinfo->isPollHdlrEnabled, VMK_TRUE,
                                    VMK_FALSE))) {
      vmk_StoragePollDisable(qinfo->pollHandler);
   }
}

void
NVMEPCIEStoragePollDestory(NVMEPCIEQueueInfo *qinfo)
{
   if (qinfo->pollHandler != NULL) {
      vmk_StoragePollDestroy(qinfo->pollHandler);
      qinfo->pollHandler = NULL;
   }
}

/**
 * Whether to switch to polling mode determining by some strategies.
 *
 * @param[in]  qinfo       Queue instance
 *
 * @return                 Whether to switch to polling mode
 */
vmk_Bool
NVMEPCIEStoragePollSwitch(NVMEPCIEQueueInfo *qinfo)
{
   NVMEPCIEController *ctrlr = qinfo->ctrlr;
   vmk_Bool pollEnabled = qinfo->ctrlr->pollEnabled;
   vmk_atomic32 *nrActPtr = &qinfo->cmdList->nrAct;
   vmk_atomic32 *iopsLastSecPtr;
   vmk_Bool doSwitch = VMK_FALSE;

   /**
    * If 'iopsTimer' is invalid, queue's 'iopsLastSec' will never be reset,
    * thus, mark 'iopsLastSec' as invalid by setting 'iopsLastSecPtr' as NULL.
    */
   iopsLastSecPtr = (ctrlr->iopsTimer != VMK_INVALID_TIMER) ?
                    &qinfo->iopsLastSec : NULL;
   /**
    * Just poll for IO queues if StoragePoll feature enabled and handler
    * created successfully.
    */
   if (pollEnabled && VMK_LIKELY(qinfo->pollHandler != NULL)) {
      /**
       * Activate polling Strategy
       *
       * 1. If OIO is adequate, it is appropriate to replace a large quantity
       *    of interrupts by polling
       * 2. If IOPs is greater than 'NVME_PCIE_POLL_IOPS_THRES_PER_QUEUE',
       *    but the OIO is low, device may have low latency feature, enable
       *    polling as well
       */
      if ((vmk_AtomicRead32(nrActPtr) >= nvmePCIEPollThr) ||
          ((iopsLastSecPtr != NULL) && (vmk_AtomicRead32(iopsLastSecPtr) >=
           NVME_PCIE_POLL_IOPS_THRES_PER_QUEUE))) {
#if NVME_PCIE_BLOCKSIZE_AWARE
         if (NVMEPCIEStoragePollBlkSizeAwareSwitch(qinfo)) {
            doSwitch = VMK_TRUE;
         }
#else
         doSwitch = VMK_TRUE;
#endif
      }
   }

   return doSwitch;
}
#endif

#if NVME_PCIE_BLOCKSIZE_AWARE
/**
 * Whether to switch to polling mode determining by Block Size Aware Polling
 * strategies.
 *
 * In the context of this function, polling must have been enabled.
 *
 * @param[in]  qinfo       Queue instance
 *
 * @return                 Whether to switch to polling mode
 */
inline vmk_Bool
NVMEPCIEStoragePollBlkSizeAwareSwitch(NVMEPCIEQueueInfo *qinfo)
{
   vmk_Bool blkSizeAwarePollEnabled = qinfo->ctrlr->blkSizeAwarePollEnabled;
   vmk_atomic32 *nrActPtr = &qinfo->cmdList->nrAct;
   vmk_atomic32 *nrActSmallPtr = &qinfo->cmdList->nrActSmall;
   vmk_Bool doSwitch = VMK_TRUE;

   /**
    * Block Size Aware Polling Strategy
    *
    * If the number of small block size OIO is less than half of total
    * OIO, use interruption to avoid inefficiency.
    */
   if ((blkSizeAwarePollEnabled) && (vmk_AtomicRead32(nrActPtr) >
       (vmk_AtomicRead32(nrActSmallPtr) << 1))) {
      doSwitch = VMK_FALSE;
   }

   return doSwitch;
}
#endif

/**
 * Enable interrupt cookie binding to a queue
 *
 * @param[in]  qinfo       Queue instance
 */
inline void
NVMEPCIEEnableIntr(NVMEPCIEQueueInfo *qinfo)
{
   NVMEPCIEController *ctrlr = qinfo->ctrlr;
   NVMEPCIECompQueueInfo *cqInfo = qinfo->cqInfo;
   VMK_ReturnStatus status = VMK_OK;
   vmk_atomic8 *isIntrEnPtr = &qinfo->isIntrEnabled;

   if (VMK_LIKELY(ctrlr->osRes.intrType == VMK_PCI_INTERRUPT_TYPE_MSIX)) {

      if (!vmk_AtomicReadIfEqualWrite8(isIntrEnPtr, VMK_FALSE, VMK_TRUE)) {
         status = vmk_IntrEnable(ctrlr->osRes.intrArray[cqInfo->intrIndex]);
         VMK_ASSERT(VMK_OK == status);
      }
   }
}

/**
 * Disable interrupt cookie binding to a queue
 *
 * @param[in]  qinfo       Queue instance
 * @param[in]  intrSync    Whether to synchronize interrupt cookie binding to
 *                         the queue
 */
inline void
NVMEPCIEDisableIntr(NVMEPCIEQueueInfo *qinfo, vmk_Bool intrSync)
{
   NVMEPCIEController *ctrlr = qinfo->ctrlr;
   NVMEPCIECompQueueInfo *cqInfo = qinfo->cqInfo;
   VMK_ReturnStatus status = VMK_OK;
   vmk_atomic8 *isIntrEnPtr = &qinfo->isIntrEnabled;

   if (VMK_LIKELY(ctrlr->osRes.intrType == VMK_PCI_INTERRUPT_TYPE_MSIX)) {
      if (vmk_AtomicReadIfEqualWrite8(isIntrEnPtr, VMK_TRUE, VMK_FALSE)) {
         if (VMK_UNLIKELY(intrSync)) {
            vmk_IntrSync(ctrlr->osRes.intrArray[qinfo->cqInfo->intrIndex]);
         }
         status = vmk_IntrDisable(ctrlr->osRes.intrArray[cqInfo->intrIndex]);
         VMK_ASSERT(VMK_OK == status);
      }
   }
}

/**
 * Process the commands completed by hardware in the given queue, return
 * the number of completed IO commands.
 *
 * @param[in]  qinfo    Queue instance
 *
 * @return              The number of completed IO commands.
 */
vmk_uint32
NVMEPCIEProcessCq(NVMEPCIEQueueInfo *qinfo)
{
   NVMEPCIECompQueueInfo *cqInfo = qinfo->cqInfo;
   NVMEPCIECmdInfoList *cmdList = qinfo->cmdList;
   NVMEPCIESubQueueInfo *sqInfo = qinfo->sqInfo;
   NVMEPCIEController *ctrlr = qinfo->ctrlr;
   NVMEPCIECmdInfo *cmdInfo;
   vmk_NvmeCompletionQueueEntry *cqEntry;
   vmk_uint16 head, phase, sqHead;
   vmk_uint32 numCmdCompleted = 0;
#ifdef NVME_STATS
   vmk_TimerRelCycles latency = 0;
   vmk_TimerCycles lastValidTs = 0;
#endif

   head = cqInfo->head;
   phase = cqInfo->phase;
   sqHead = sqInfo->head;

   while(1) {
      cqEntry = &cqInfo->compq[head];
      if (cqEntry->dw3.p != phase) {
         break;
      }
      if (ctrlr->abortEnabled) {
         cmdInfo = &cmdList->list[cqEntry->dw3.cid];
      } else {
         cmdInfo = &cmdList->list[cqEntry->dw3.cid - 1];
      }
      sqHead = cqEntry->dw2.sqhd;
      if (sqHead >= (vmk_uint16)sqInfo->qsize) {
         EPRINT(ctrlr, "Invalid sqhd 0x%x returned from controller for qid %d, cid 0x%x",
                sqHead, qinfo->id, cmdInfo->vmkCmd->nvmeCmd.cdw0.cid);
         VMK_ASSERT(0);
      } else {
         vmk_AtomicWrite32(&sqInfo->pendingHead, (vmk_uint32)sqHead);
      }
      VMK_ASSERT(cmdInfo->vmkCmd != NULL);
      vmk_Memcpy(&cmdInfo->vmkCmd->cqEntry,
                 cqEntry,
                 VMK_NVME_CQE_SIZE);
      if (!ctrlr->abortEnabled) {
         cmdInfo->vmkCmd->cqEntry.dw3.cid = cmdInfo->vmkCmd->nvmeCmd.cdw0.cid;
      }
      cmdInfo->vmkCmd->nvmeStatus = GetCommandStatus(cqEntry);
#ifdef NVME_STATS
      /**
       * For corner case where CQ entries had been written to CQ but interrupt
       * is not generated yet. These arrived entries might be processed in
       * this loop before being processed by IntrAck that fill doneByHwTs for entries.
       * If so, the doneByHwTs of these entries are empty.
       * To cover this corner case, use the latest valid doneByHwTs as real doneByHwTs
       * of above stated case. Which is a simple way. Let's make a compromise on
       * preciseness.
       */
      if (cmdInfo->statsOn) {
         if (cmdInfo->doneByHwTs) {
            latency = (cmdInfo->doneByHwTs - cmdInfo->sendToHwTs);
            if (VMK_UNLIKELY(latency <= 0)) {
               latency = 0;
            }
            cmdInfo->vmkCmd->deviceLatency = latency;
            lastValidTs = cmdInfo->doneByHwTs;
         } else {
            latency = (lastValidTs - cmdInfo->sendToHwTs);
            if (VMK_UNLIKELY(latency <= 0)) {
               latency = 0;
            }
            cmdInfo->vmkCmd->deviceLatency = latency;
         }
      }
#endif
      if (cmdInfo->done) {
         cmdInfo->done(qinfo, cmdInfo);
      }

      numCmdCompleted++;
      vmk_AtomicInc32(&qinfo->numCmdComplThisSec);

      if (++head >= cqInfo->qsize) {
         head = 0;
         phase = !phase;
      }
   }

   if (!((head == cqInfo->head) && (phase == cqInfo->phase))) {
      cqInfo->head = head;
      cqInfo->phase = phase;
      if (VMK_LIKELY(!ctrlr->isRemoved)) {
         NVMEPCIEWritel(head, cqInfo->doorbell);
      }
   }

   return numCmdCompleted;
}

static VMK_ReturnStatus
CreateSq(NVMEPCIEController *ctrlr, NVMEPCIEQueueInfo *qinfo)
{
   vmk_NvmeCommand *vmkCmd;
   VMK_ReturnStatus vmkStatus;
   vmk_NvmeCreateIOSQCmd *createSqCmd;

   vmkCmd = NVMEPCIEAlloc(sizeof(vmk_NvmeCommand), 0);
   if (vmkCmd == NULL) {
      return VMK_NO_MEMORY;
   }
   createSqCmd = (vmk_NvmeCreateIOSQCmd *)&vmkCmd->nvmeCmd;
   createSqCmd->cdw0.opc= VMK_NVME_ADMIN_CMD_CREATE_IO_SQ;
   createSqCmd->dptr.prps.prp1.pbao = qinfo->sqInfo->subqPhy;
   createSqCmd->cdw10.qid = qinfo->sqInfo->id;
   createSqCmd->cdw10.qsize = qinfo->sqInfo->qsize - 1;
   createSqCmd->cdw11.pc = 1;
   createSqCmd->cdw11.qprio = 0;
   createSqCmd->cdw11.cqid = qinfo->cqInfo->id;

   vmkStatus = NVMEPCIESubmitSyncCommand(ctrlr, vmkCmd, 0, NULL, 0, ADMIN_TIMEOUT);

   if (VMK_UNLIKELY(vmkStatus == VMK_TIMEOUT)) {
      return vmkStatus;
   }

   if (vmkCmd->nvmeStatus == VMK_NVME_STATUS_GC_SUCCESS) {
      DPRINT_Q(ctrlr, "sq [%d] created", qinfo->sqInfo->id);
   } else {
      EPRINT(ctrlr, "Create sq command failed, 0x%x", vmkCmd->nvmeStatus);
      vmkStatus = VMK_FAILURE;
   }

   NVMEPCIEFree(vmkCmd);
   return vmkStatus;
}

static VMK_ReturnStatus
CreateCq(NVMEPCIEController *ctrlr, NVMEPCIEQueueInfo *qinfo)
{
   vmk_NvmeCommand *vmkCmd;
   VMK_ReturnStatus vmkStatus;
   vmk_NvmeCreateIOCQCmd *createCqCmd;

   vmkCmd = NVMEPCIEAlloc(sizeof(vmk_NvmeCommand), 0);
   if (vmkCmd == NULL) {
      return VMK_NO_MEMORY;
   }
   createCqCmd = (vmk_NvmeCreateIOCQCmd *)&vmkCmd->nvmeCmd;
   createCqCmd->cdw0.opc= VMK_NVME_ADMIN_CMD_CREATE_IO_CQ;
   createCqCmd->dptr.prps.prp1.pbao = qinfo->cqInfo->compqPhy;
   createCqCmd->cdw10.qid = qinfo->cqInfo->id;
   createCqCmd->cdw10.qsize = qinfo->cqInfo->qsize - 1;
   createCqCmd->cdw11.pc = 1;
   createCqCmd->cdw11.ien = 1;
   if (ctrlr->osRes.intrType == VMK_PCI_INTERRUPT_TYPE_MSIX) {
      createCqCmd->cdw11.iv = qinfo->cqInfo->intrIndex;
   } else {
      createCqCmd->cdw11.iv = 0;
   }

   vmkStatus = NVMEPCIESubmitSyncCommand(ctrlr, vmkCmd, 0, NULL, 0, ADMIN_TIMEOUT);

   if (VMK_UNLIKELY(vmkStatus == VMK_TIMEOUT)) {
      return vmkStatus;
   }

   if (vmkCmd->nvmeStatus == VMK_NVME_STATUS_GC_SUCCESS) {
      DPRINT_Q(ctrlr, "cq [%d] created", qinfo->sqInfo->id);
   } else {
      EPRINT(ctrlr, "Create cq command failed, 0x%x", vmkCmd->nvmeStatus);
      vmkStatus = VMK_FAILURE;
   }

   NVMEPCIEFree(vmkCmd);
   return vmkStatus;
}

static VMK_ReturnStatus
DeleteSq(NVMEPCIEController *ctrlr, vmk_uint16 qid)
{
   vmk_NvmeCommand *vmkCmd;
   VMK_ReturnStatus vmkStatus;
   vmk_NvmeDeleteIOSQCmd *deleteSqCmd;

   vmkCmd = NVMEPCIEAlloc(sizeof(vmk_NvmeCommand), 0);
   if (vmkCmd == NULL) {
      return VMK_NO_MEMORY;
   }
   deleteSqCmd = (vmk_NvmeDeleteIOSQCmd *)&vmkCmd->nvmeCmd;
   deleteSqCmd->cdw0.opc= VMK_NVME_ADMIN_CMD_DELETE_IO_SQ;
   deleteSqCmd->cdw10.qid = qid;

   vmkStatus = NVMEPCIESubmitSyncCommand(ctrlr, vmkCmd, 0, NULL, 0, ADMIN_TIMEOUT);

   if (VMK_UNLIKELY(vmkStatus == VMK_TIMEOUT)) {
      return vmkStatus;
   }

   if (vmkCmd->nvmeStatus == VMK_NVME_STATUS_GC_SUCCESS) {
      DPRINT_Q(ctrlr, "sq [%d] deleted", qid);
   } else {
      EPRINT(ctrlr, "Delete sq command failed, 0x%x", vmkCmd->nvmeStatus);
      vmkStatus = VMK_FAILURE;
   }
   NVMEPCIEFree(vmkCmd);

   return vmkStatus;
}

static VMK_ReturnStatus
DeleteCq(NVMEPCIEController *ctrlr, vmk_uint16 qid)
{
   vmk_NvmeCommand *vmkCmd;
   VMK_ReturnStatus vmkStatus;
   vmk_NvmeDeleteIOCQCmd *deleteCqCmd;

   vmkCmd = NVMEPCIEAlloc(sizeof(vmk_NvmeCommand), 0);
   if (vmkCmd == NULL) {
      return VMK_NO_MEMORY;
   }
   deleteCqCmd = (vmk_NvmeDeleteIOCQCmd *)&vmkCmd->nvmeCmd;
   deleteCqCmd->cdw0.opc= VMK_NVME_ADMIN_CMD_DELETE_IO_CQ;
   deleteCqCmd->cdw10.qid = qid;

   vmkStatus = NVMEPCIESubmitSyncCommand(ctrlr, vmkCmd, 0, NULL, 0, ADMIN_TIMEOUT);

   if (VMK_UNLIKELY(vmkStatus == VMK_TIMEOUT)) {
      return vmkStatus;
   }

   if (vmkCmd->nvmeStatus == VMK_NVME_STATUS_GC_SUCCESS) {
      DPRINT_Q(ctrlr, "cq [%d] deleted", qid);
   } else {
      EPRINT(ctrlr, "Delete cq command failed, 0x%x", vmkCmd->nvmeStatus);
      vmkStatus = VMK_FAILURE;
   }
   NVMEPCIEFree(vmkCmd);

   return vmkStatus;
}

/**
 * Get command status from cq entry
 *
 * @param [in] cqe  Completion queue entry
 *
 * return vmk_NvmeStatus
 */
static inline vmk_NvmeStatus
GetCommandStatus(vmk_NvmeCompletionQueueEntry *cqe)
{
   vmk_NvmeStatus nvmeStatus = (cqe->dw3.sct << 8) | cqe->dw3.sc;
   //TODO: Should such special status be processed in driver or in core layer?
   if (VMK_UNLIKELY(nvmeStatus == VMK_NVME_STATUS_GC_NS_NOT_READY)) {
      nvmeStatus = cqe->dw3.dnr? VMK_NVME_STATUS_VMW_NS_NOT_READY_NO_RETRY : VMK_NVME_STATUS_VMW_NS_NOT_READY_RETRY;
   }
   return nvmeStatus;
}

/**
 * Suspend a queue
 *
 * @param [in] qinfo  Queue instance
 *
 */
void NVMEPCIESuspendQueue(NVMEPCIEQueueInfo *qinfo)
{
   NVMEPCIEController *ctrlr = qinfo->ctrlr;
   NVMEPCIEQueueState state;

   vmk_AtomicInc32(&qinfo->refCount);
   state = vmk_AtomicReadIfEqualWrite32(&qinfo->state, NVME_PCIE_QUEUE_ACTIVE,
                                        NVME_PCIE_QUEUE_SUSPENDED);
   if (state != NVME_PCIE_QUEUE_ACTIVE) {
      WPRINT(ctrlr, "Trying to suspend inactive queue %d.", qinfo->id);
      vmk_AtomicDec32(&qinfo->refCount);
      return;
   }

   // Reset reset IOPs statistics of queue
   vmk_AtomicWrite32(&qinfo->iopsLastSec, 0);
   vmk_AtomicWrite32(&qinfo->numCmdComplThisSec, 0);

#if NVME_PCIE_STORAGE_POLL
   /**
    * Disable poll handler and re-enable interrupt if StoragePoll feature
    * enabled and handler created successfully
    */
   if (ctrlr->pollEnabled) {
      NVMEPCIEStoragePollDisable(qinfo);
   }
#endif

   NVMEPCIEDisableIntr(qinfo, VMK_TRUE);
   vmk_AtomicDec32(&qinfo->refCount);
   return;
}

/**
 * Resume a queue
 *
 * @param [in] qinfo  Queue instance
 *
 * return VMK_ReturnStatus
 */
VMK_ReturnStatus
NVMEPCIEResumeQueue(NVMEPCIEQueueInfo *qinfo)
{
   NVMEPCIEController *ctrlr = qinfo->ctrlr;
   VMK_ReturnStatus vmkStatus = VMK_OK;
   NVMEPCIEQueueState state;

   state = vmk_AtomicReadWrite32(&qinfo->state, NVME_PCIE_QUEUE_ACTIVE);
   if (state == NVME_PCIE_QUEUE_ACTIVE) {
      WPRINT(qinfo->ctrlr, "Trying to resume active queue %d.", qinfo->id);
      return VMK_OK;
   }

#if NVME_PCIE_STORAGE_POLL
   /**
    * Enable poll handler if StoragePoll feature enabled and handler created
    * successfully
    */
   if (ctrlr->pollEnabled) {
      NVMEPCIEStoragePollEnable(qinfo);
   }
#endif

   NVMEPCIEEnableIntr(qinfo);

   return vmkStatus;
}

/**
 * Flush all outstanding commands on a queue
 *
 * @param [in] qinfo  Queue instance
 * @param [in] status Status code for non-completed commands
 *
 * return VMK_ReturnStatus
 */
void
NVMEPCIEFlushQueue(NVMEPCIEQueueInfo *qinfo, vmk_NvmeStatus status)
{
   NVMEPCIECmdInfo *cmdInfo = NULL;
   vmk_atomic32 atomicStatus;
   int i;

   vmk_AtomicInc32(&qinfo->refCount);
   if (vmk_AtomicRead32(&qinfo->state) == NVME_PCIE_QUEUE_NON_EXIST) {
      WPRINT(qinfo->ctrlr, "Trying to flush non exist queue %d.", qinfo->id);
      vmk_AtomicDec32(&qinfo->refCount);
      return;
   }

   cmdInfo = qinfo->cmdList->list;
   vmk_SpinlockLock(qinfo->cqInfo->lock);
   NVMEPCIEProcessCq(qinfo);
   vmk_SpinlockUnlock(qinfo->cqInfo->lock);

   for (i = 1; i <= qinfo->cmdList->idCount; i++) {
      atomicStatus = vmk_AtomicRead32(&cmdInfo->atomicStatus);
      if (atomicStatus == NVME_PCIE_CMD_STATUS_ACTIVE ||
          atomicStatus == NVME_PCIE_CMD_STATUS_FREE_ON_COMPLETE) {
         cmdInfo->vmkCmd->nvmeStatus = status;
         VMK_ASSERT(cmdInfo->done);
         cmdInfo->done(qinfo, cmdInfo);
      }
      cmdInfo++;
   }
   vmk_AtomicDec32(&qinfo->refCount);
}

/**
 * Stop queue
 *
 * - Suspend the queue
 * - Delete HW cq and sq
 * - Flush all oustanding commands on the queue
 *
 * @param [in] qinfo   Queue instance
 * @param [in] status  Status code to be set for uncompleted commands
 *
 * @return VMK_OK on success, error code otherwise
 */
VMK_ReturnStatus
NVMEPCIEStopQueue(NVMEPCIEQueueInfo *qinfo, vmk_NvmeStatus status)
{
   NVMEPCIEController *ctrlr = qinfo->ctrlr;
   vmk_NvmeRegCsts csts;

   NVMEPCIESuspendQueue(qinfo);

   if (!ctrlr->isRemoved) {
      *(vmk_uint32*)&csts = NVMEPCIEReadl(qinfo->ctrlr->regs + VMK_NVME_REG_CSTS);
      /** Delete hw sq and cq. If controller is disabled, no need to delete queues.*/
      if (qinfo->id != 0 && csts.rdy && !csts.cfs) {
         DeleteSq(ctrlr, qinfo->id);
         DeleteCq(ctrlr, qinfo->id);
      }
   }

   NVMEPCIEFlushQueue(qinfo, status);

   return VMK_OK;
}

static VMK_ReturnStatus
NVMEPCIEInitQueue(NVMEPCIEQueueInfo *qinfo)
{
   NVMEPCIECmdInfo *cmdInfo;
   NVMEPCIECmdInfoList *cmdList = qinfo->cmdList;
   int i;

   if (vmk_AtomicRead32(&qinfo->state) != NVME_PCIE_QUEUE_SUSPENDED) {
      WPRINT(qinfo->ctrlr, "Trying to init active queue %d.", qinfo->id);
      return VMK_BUSY;
   }

   //TODO: need to grab queue lock?

   /** Reset submission queue */
   qinfo->sqInfo->head = 0;
   qinfo->sqInfo->tail = 0;
   vmk_AtomicWrite32(&qinfo->sqInfo->pendingHead, NVME_INVALID_SQ_HEAD);
   vmk_Memset(qinfo->sqInfo->subq, 0,
      qinfo->sqInfo->qsize * sizeof(vmk_NvmeSubmissionQueueEntry));

   /** Reset completion queue */
   qinfo->cqInfo->head = 0;
   qinfo->cqInfo->tail = 0;
   qinfo->cqInfo->phase = 1;
   vmk_Memset(qinfo->cqInfo->compq, 0,
      qinfo->cqInfo->qsize * sizeof(vmk_NvmeCompletionQueueEntry));

   /** Reset cmd info list */
   cmdList->nrAct = 0;
   cmdList->nrActSmall = 0;
   cmdList->freeCmdList = 0;
   vmk_AtomicWrite64(&cmdList->pendingFreeCmdList.atomicComposite, 0);
   cmdInfo = cmdList->list;
   for (i = 1; i <= cmdList->idCount; i++) {
      cmdInfo->cmdId = i;
      vmk_AtomicWrite32(&cmdInfo->atomicStatus, NVME_PCIE_CMD_STATUS_FREE);
      cmdInfo->freeLink = cmdList->freeCmdList;
      cmdList->freeCmdList = cmdInfo->cmdId;
      cmdInfo ++;
   }

   return VMK_OK;
}

/**
 * Start queue
 *
 * - Reset queue to initial state
 * - For IO queue, create HW cq and sq
 * - Resume the queue
 *
 * @param [in] qinfo  Queue instance
 *
 * @return VMK_OK on success, error code otherwise
 */
VMK_ReturnStatus
NVMEPCIEStartQueue(NVMEPCIEQueueInfo *qinfo)
{
   VMK_ReturnStatus vmkStatus;
   NVMEPCIEController *ctrlr = qinfo->ctrlr;

   vmkStatus = NVMEPCIEInitQueue(qinfo);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to init queue %d, 0x%x.", qinfo->id, vmkStatus);
      return vmkStatus;
   }

   if (qinfo->id == 0) {
      goto resume_queue;
   }

   vmkStatus = CreateCq(ctrlr, qinfo);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to create cq [%d], 0x%x.", qinfo->id, vmkStatus);
      return vmkStatus;
   }

   vmkStatus = CreateSq(ctrlr, qinfo);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to create sq [%d], 0x%x.", qinfo->id, vmkStatus);
      DeleteCq(ctrlr, qinfo->id);
      return vmkStatus;
   }

resume_queue:
   NVMEPCIEResumeQueue(qinfo);
   return VMK_OK;
}

VMK_ReturnStatus
NVMEPCIEIdentify(NVMEPCIEController *ctrlr,
                 vmk_NvmeCnsField cns,
                 vmk_uint32 nsID,
                 vmk_uint8 *data)
{
   vmk_NvmeCommand *vmkCmd;
   VMK_ReturnStatus vmkStatus;
   vmk_NvmeIdentifyCmd *identCmd;

   vmkCmd = NVMEPCIEAlloc(sizeof(vmk_NvmeCommand), 0);
   if (vmkCmd == NULL) {
      return VMK_NO_MEMORY;
   }
   identCmd = (vmk_NvmeIdentifyCmd *)&vmkCmd->nvmeCmd;
   identCmd->cdw0.opc = VMK_NVME_ADMIN_CMD_IDENTIFY;
   identCmd->nsid = nsID;
   identCmd->cdw10.cns = cns;

   vmkStatus = NVMEPCIESubmitSyncCommand(ctrlr, vmkCmd, 0, data, VMK_PAGE_SIZE, ADMIN_TIMEOUT);
   if (VMK_UNLIKELY(vmkStatus == VMK_TIMEOUT)) {
      return vmkStatus;
   }

   if (vmkCmd->nvmeStatus != VMK_NVME_STATUS_GC_SUCCESS) {
      EPRINT(ctrlr, "Identify command failed, 0x%x", vmkCmd->nvmeStatus);
      vmkStatus = VMK_FAILURE;
   }
   NVMEPCIEFree(vmkCmd);
   return vmkStatus;
}
