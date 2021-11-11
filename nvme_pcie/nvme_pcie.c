/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (c) 2012-2013  Integrated Device Technology, Inc.
 * All rights reserved.
 *******************************************************************************/

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
static NVMEPCIECmdInfo* NVMEPCIEGetCmdInfo(NVMEPCIEQueueInfo *qinfo);
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

   vmkStatus = QueueConstruct(ctrlr, qinfo, qid, qsize, qsize, qid);
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
   int i;

   ctrlr = qinfo->ctrlr;

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
   cmdInfo = NVMEPCIEAlloc(qsize * sizeof(*cmdInfo), 0);
   if (cmdInfo == NULL) {
      EPRINT(ctrlr, "Failed to allocate cmd info array for queue %d.", qinfo->id);
      vmkStatus = VMK_NO_MEMORY;
      goto free_lock;
   }

   cmdList->list = cmdInfo;
   cmdList->idCount = qsize;
   for (i = 1; i <= qsize; i++) {
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
   DPRINT_Q(ctrlr, "Free cmdList for queue %d.", qinfo->id);

   return VMK_OK;
}

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

   return VMK_OK;

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
   vmk_SpinlockLock(qinfo->cqInfo->lock);
   NVMEPCIEProcessCq(qinfo);
   vmk_SpinlockUnlock(qinfo->cqInfo->lock);
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
   cmdList->nrAct -= oldValue.freeListLength;
   return oldValue.cmdOffset;
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
NVMEPCIEGetCmdInfo(NVMEPCIEQueueInfo *qinfo)
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
         WPRINT(ctrlr, "Queue[%d] command list empty. %d", qinfo->id, cmdList->nrAct);
         vmk_SpinlockUnlock(cmdList->lock);
         return NULL;
      }
   }

   cmdInfo = &cmdList->list[cmdList->freeCmdList-1];
   cmdList->freeCmdList = cmdInfo->freeLink;
   cmdList->nrAct ++;
   vmk_AtomicWrite32(&cmdInfo->atomicStatus, NVME_PCIE_CMD_STATUS_ACTIVE);

   vmk_SpinlockUnlock(cmdList->lock);

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
   NVMEPCIEPushCmdInfo(qinfo, cmdInfo);
   DPRINT_CMD(ctrlr, "Put cmd Info [%d] %p back to queue [%d], nrAct: %d.",
              cmdInfo->cmdId, cmdInfo, qinfo->id, qinfo->cmdList->nrAct);
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

   qinfo = &ctrlr->queueList[qid];
   if (vmk_AtomicRead32(&qinfo->state) != NVME_PCIE_QUEUE_ACTIVE) {
      vmkCmd->nvmeStatus = VMK_NVME_STATUS_VMW_IN_RESET;
      return VMK_FAILURE;
   }
   vmk_AtomicInc32(&qinfo->refCount);

   cmdInfo = NVMEPCIEGetCmdInfo(qinfo);
   if (cmdInfo == NULL) {
      vmkCmd->nvmeStatus = VMK_NVME_STATUS_VMW_QUEUE_FULL;
      vmk_AtomicDec32(&qinfo->refCount);
      return VMK_FAILURE;
   }

   cmdInfo->vmkCmd = vmkCmd;
   cmdInfo->type = NVME_PCIE_ASYNC_CONTEXT;

   nvmeStatus = NVMEPCIEIssueCommandToHw(qinfo, cmdInfo, NVMEPCIECompleteAsyncCommand);

   if (VMK_UNLIKELY(nvmeStatus != VMK_NVME_STATUS_VMW_WOULD_BLOCK)) {
      vmkCmd->nvmeStatus = nvmeStatus;
      WPRINT(ctrlr, "Failed to issue command %d, 0x%x", cmdInfo->cmdId, nvmeStatus);
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
   if (vmk_AtomicRead32(&qinfo->state) != NVME_PCIE_QUEUE_ACTIVE) {
      vmkCmd->nvmeStatus = VMK_NVME_STATUS_VMW_IN_RESET;
      return VMK_FAILURE;
   }
   vmk_AtomicInc32(&qinfo->refCount);

   cmdInfo = NVMEPCIEGetCmdInfo(qinfo);
   if (cmdInfo == NULL) {
      vmkCmd->nvmeStatus = VMK_NVME_STATUS_VMW_QUEUE_FULL;
      vmk_AtomicDec32(&qinfo->refCount);
      return VMK_FAILURE;
   }

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
   NVMEPCIEDumpCommand(qinfo->ctrlr, vmkCmd);
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

   vmk_Memcpy(&sqInfo->subq[tail], &cmdInfo->vmkCmd->nvmeCmd, VMK_NVME_SQE_SIZE);
   NVMEPCIEDumpSqe(qinfo->ctrlr, &cmdInfo->vmkCmd->nvmeCmd);
   sqInfo->subq[tail].cdw0.cid = cmdInfo->cmdId;

   tail ++;
   if (tail >= sqInfo->qsize) {
      tail = 0;
   }

   NVMEPCIEWritel(tail, sqInfo->doorbell);
   sqInfo->tail = tail;
   vmk_SpinlockUnlock(sqInfo->lock);

   return VMK_NVME_STATUS_VMW_WOULD_BLOCK;
}

/**
 * Process the commands completed by hardware in the given queue
 *
 * @param[in] qinfo    Queue instance
 */
void
NVMEPCIEProcessCq(NVMEPCIEQueueInfo *qinfo)
{
   NVMEPCIECompQueueInfo *cqInfo = qinfo->cqInfo;
   NVMEPCIECmdInfoList *cmdList = qinfo->cmdList;
   NVMEPCIESubQueueInfo *sqInfo = qinfo->sqInfo;
   NVMEPCIEController *ctrlr = qinfo->ctrlr;
   NVMEPCIECmdInfo *cmdInfo;
   vmk_NvmeCompletionQueueEntry *cqEntry;
   vmk_uint16 head, phase, sqHead;

   head = cqInfo->head;
   phase = cqInfo->phase;
   sqHead = sqInfo->head;

   while(1) {
      cqEntry = &cqInfo->compq[head];
      if (cqEntry->dw3.p != phase) {
         break;
      }
      cmdInfo = &cmdList->list[cqEntry->dw3.cid - 1];
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
      cmdInfo->vmkCmd->cqEntry.dw3.cid = cmdInfo->vmkCmd->nvmeCmd.cdw0.cid;
      cmdInfo->vmkCmd->nvmeStatus = GetCommandStatus(cqEntry);
      if (cmdInfo->done) {
         cmdInfo->done(qinfo, cmdInfo);
      }
      if (++head >= cqInfo->qsize) {
         head = 0;
         phase = !phase;
      }
   }

   if (!((head == cqInfo->head) && (phase == cqInfo->phase))) {
      cqInfo->head = head;
      cqInfo->phase = phase;
      NVMEPCIEWritel(head, cqInfo->doorbell);
   }
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
   createCqCmd->cdw11.iv = qinfo->cqInfo->intrIndex;

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

   state = vmk_AtomicReadWrite32(&qinfo->state, NVME_PCIE_QUEUE_SUSPENDED);
   if (state == NVME_PCIE_QUEUE_SUSPENDED) {
      WPRINT(ctrlr, "Trying to suspend inactive queue %d.", qinfo->id);
      return;
   }
   if (ctrlr->osRes.intrType == VMK_PCI_INTERRUPT_TYPE_MSIX) {
      // Flush out all in-flight interrupts.
      vmk_IntrSync(ctrlr->osRes.intrArray[qinfo->cqInfo->intrIndex]);
      vmk_IntrDisable(ctrlr->osRes.intrArray[qinfo->cqInfo->intrIndex]);
   }
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
   if (ctrlr->osRes.intrType == VMK_PCI_INTERRUPT_TYPE_MSIX) {
      vmkStatus = vmk_IntrEnable(ctrlr->osRes.intrArray[qinfo->cqInfo->intrIndex]);
      VMK_ASSERT(vmkStatus == VMK_OK);
   }
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
   NVMEPCIECmdInfo *cmdInfo = qinfo->cmdList->list;
   vmk_atomic32 atomicStatus;
   int i;

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
