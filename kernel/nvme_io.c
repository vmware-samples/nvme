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
 * @file: nvme_scsi_io.c --
 *
 *    Low level IO.
 */

#include "nvme_private.h"
#include "nvme_debug.h"
#include "nvme_scsi_cmds.h"

/**
 * @brief This function prepares a prp list.
 *
 * @param[in] qinfo pointer to queue information block
 * @param[in] cmdInfo pointer to command information blcck
 *
 * @return This function returns int 0 if successful, otherwise Error Code
 *
 * @note It is assumed that queue lock is held by caller.
 */
vmk_ByteCount
NvmeIo_ProcessPrps(struct NvmeQueueInfo *qinfo, struct NvmeCmdInfo *cmdInfo)
{
   vmk_int64           dmaLen, offset;
   vmk_int64           length, processed;
   vmk_IOA             dmaAddr;
   int                 elemIndex;
   int                 thisPrpLen;
   vmk_SgArray        *sgArray = cmdInfo->sgPosition.sg;
   struct nvme_prp    *prps;
   struct NvmeCmdInfo *cmdBase = cmdInfo->cmdBase;

   VMK_ASSERT(cmdInfo->sgPosition.type == VMK_SG_POSITION_TYPE_ELEMENT);
   VMK_ASSERT(cmdInfo->sgPosition.sg != NULL);
   /* We should always split at offset 0 of an SG element */
   VMK_ASSERT(cmdInfo->sgPosition.element.offset == 0);
   VMK_ASSERT(cmdInfo->sgPosition.sg->numElems > cmdInfo->sgPosition.element.element);

   elemIndex = cmdInfo->sgPosition.element.element;
   prps      = cmdInfo->nvmeCmd.header.prp;
   processed = 0;
   /*
    * maximum possible bytes to be transferred in this command
    * (e.g. all remaining SG elements are logically virtual contiguous)
    */
   length    = cmdBase->requiredLength - cmdBase->requestedLength;

   dmaAddr   = sgArray->elem[elemIndex].ioAddr;
   dmaLen    = sgArray->elem[elemIndex].length;
   offset    = dmaAddr & VMK_PAGE_MASK;

   DPRINT6("length %ld, dma_addr 0x%0lx, offset 0x%0lx, dma_len %ld",
      length,  dmaAddr, offset, dmaLen);

   prps->addr = dmaAddr;
   prps ++;

   thisPrpLen  = min_t(vmk_uint32, dmaLen, (VMK_PAGE_SIZE - offset));
   length     -= thisPrpLen;
   processed  += thisPrpLen;
   dmaAddr    += thisPrpLen;
   dmaLen     -= thisPrpLen;

   /*
    * Fast track for small payloads
    */
   if (length <= 0) {
      /* Only 1 prp entry (prp0) is sufficient */
      return processed;
   }

   /*
    * More than a single entry, should use prp list.
    *
    * Note: it is also possible that prp1 is sufficient instead of using prp
    *       entry list. We will check that condition after we have completed
    *       processing the list by reaching the end or finding a split point.
    */
   prps->addr = cmdInfo->prpPhy;
   prps = cmdInfo->prps;
   DPRINT6("List PRP1 %016lx, PRP2 %016lx, length %ld",
      cmdInfo->nvmeCmd.header.prp[0].addr,
      cmdInfo->nvmeCmd.header.prp[1].addr,
      length);

   /*
    * Process the rest of sglist.
    *
    * Note: We will stop if we reach the end of the SG array (in case of length
    *       <= 0), or the SG element being processed is not virtually contiguous
    *       (in case of (dmaAddr & VMK_PAGE_MASK) is non-zero. In the latter case,
    *       we need to break out and split into another NVMe command to process
    *       the rest of the IO since NVMe can only process virtually contiguous
    *       SG PRPs in a single command.
    */
   while (length > 0 && ((dmaAddr & VMK_PAGE_MASK) == 0)) {
      if (dmaLen > 0) {
         prps->addr = dmaAddr;
         DPRINT3("PRP list [%p] = %016lx", prps, prps->addr);
         prps++;
         thisPrpLen = min_t(vmk_uint32, dmaLen, VMK_PAGE_SIZE);
         length    -= thisPrpLen;
         processed += thisPrpLen;
         dmaAddr   += thisPrpLen;
         dmaLen    -= thisPrpLen;
      } else {
         elemIndex ++;
         dmaAddr = sgArray->elem[elemIndex].ioAddr;
         dmaLen = sgArray->elem[elemIndex].length;
      }
   }

   /*
    * When we completed processing the list, we should either have length > 0
    * with valid elemIndex, or length == 0 and elemIndex points to the last
    * element in the array.
    */
   VMK_ASSERT((length > 0 && elemIndex < sgArray->numElems) ||
      (length == 0 && elemIndex == sgArray->numElems - 1));

   /*
    * If we break when prps points to the second entry in prp list, that
    * means two PRP entries are sufficient for carrying this request, and we
    * should move cmdInfo->prps[0] to nvmeCmd->header.prp[1] for such case.
    */
   if (prps == &(cmdInfo->prps[1])) {
      cmdInfo->nvmeCmd.header.prp[1].addr = cmdInfo->prps[0].addr;
      DPRINT6("using prp1 for io: vmkCmd %p base %p info %p prp0 0x%lx prp1 0x%lx",
         cmdBase->vmkCmd, cmdBase, cmdInfo, cmdInfo->nvmeCmd.header.prp[0].addr,
         cmdInfo->nvmeCmd.header.prp[1].addr);
   }

   /*
    * Done processing
    */

   /* Total number of processed bytes plus remaining length should match
    * up with the total number of bytes left for the base request */
   VMK_ASSERT((processed + length) ==
      (cmdBase->requiredLength - cmdBase->requestedLength));

   return (processed);
}


/**
 * @brief This function called to generate scatter gather list
 *
 * Note: It is assumed that queue lock is held by caller.
 *
 * @param[in] qinfo pointer to queue information block
 * @param[in] cmdInfo pointer to command information block
 * @param[in] vmkCmd pointer to block io request
 * @param[in] dmaDir Data direction
 *
 * @return This function returns int length, number of bytes transferred.
 *
 */
vmk_ByteCount
NvmeIo_ProcessSgArray(struct NvmeQueueInfo *qinfo, struct NvmeCmdInfo *cmdInfo,
   vmk_ScsiCommand *vmkCmd, vmk_DMADirection dmaDir)
{
   VMK_ReturnStatus vmkStatus;
   int nsegs;
   vmk_ByteCount length;
   struct NvmeCmdInfo *cmdBase = cmdInfo->cmdBase;

   /*
    * TODO: this should never happen. Maybe just igore this check
    */
   nsegs = vmkCmd->sgIOArray->numElems;
   if (nsegs > max_prp_list) {
      Nvme_LogError("nsegs %d max_prp_list %d out of range.", nsegs, max_prp_list);
      /*
       * Ideally PSA should prevent such thing from happening. If it does
       * happen, a DATA UNDERRUN should be generated.
       */
      VMK_ASSERT(0);
      return 0;
   }

   /*
    * Figure out where we should start processing the SG array.
    */
   vmkStatus = vmk_SgFindPosition(vmkCmd->sgIOArray, cmdBase->requestedLength,
      &cmdInfo->sgPosition);
   if (vmkStatus != VMK_OK) {
      /* out of range, should never happen. */
      Nvme_LogError("Invalid position, vmkCmd %p, lba 0x%lx, lbc %d, cmdInfo %p, cmdBase %p, reqeustedLen %d.",
         vmkCmd, vmkCmd->lba, vmkCmd->lbc, cmdInfo, cmdBase, cmdBase->requestedLength);
      VMK_ASSERT(0);
      return 0;
   }

   /* Convert SG array starting at this position to prp lists.
    *
    * Nvme_ProcessPrps returns the length that has been processed.
    */
   length = NvmeIo_ProcessPrps(qinfo, cmdInfo);

   cmdInfo->count = length;
   return length;
}


/**
 * scsiIoDummyCompleteCommand - dummy completion callback, completing active
 *                              base command that has already been processed
 *                              during NVM reset queue flush.
 */
static void
scsiIoDummyCompleteCommand(struct NvmeQueueInfo *qinfo,
                           struct NvmeCmdInfo *cmdInfo)
{
   Nvme_LogInfo("double completing io cmd %p [%d] base %p vmkCmd %p.",
               cmdInfo, cmdInfo->cmdId, cmdInfo->cmdBase,
               cmdInfo->vmkCmd);
}


/**
 * scsiIoCompleteCommand - completion callback for I/O commands
 */
static void
scsiIoCompleteCommand(struct NvmeQueueInfo *qinfo, struct NvmeCmdInfo *cmdInfo)
{
   vmk_ScsiCommand    *vmkCmd;
   struct NvmeCmdInfo *baseInfo;
   Nvme_Status         nvmeStatus;
   struct nvme_cmd         *cmd;

#if NVME_DEBUG
   if (nvme_dbg & NVME_DEBUG_DUMP_CE) {
      Nvme_LogDebug("compl q %p[%d] cmdInfo %p.", qinfo, qinfo->id, cmdInfo);
   }
#endif

   cmdInfo->status = NVME_CMD_STATUS_DONE;

   if (cmdInfo->cmdStatus) {
      Nvme_LogError("I/O Error: cmd %p status 0x%x, %s.", cmdInfo,
                    cmdInfo->cmdStatus,
                    NvmeCore_StatusToString(cmdInfo->cmdStatus));
      /**
       * TODO:
       *
       * 1. If this is a sub-command:
       *    Propagate the error code to base command;
       *    Return;
       * 2. If this is a base command:
       *    1. set Scsi return code based on cmdInfo->cmdStatus;
       *    2. complete vmkCmd;
       */
      if (cmdInfo->cmdBase &&
          cmdInfo->cmdBase->cmdStatus == NVME_STATUS_SUCCESS) {
         cmdInfo->cmdBase->cmdStatus = cmdInfo->cmdStatus;
      }
      nvmeStatus = cmdInfo->cmdStatus;
   } else {
      nvmeStatus = NVME_STATUS_SUCCESS;
   }

   /**
    * TODO:
    *
    * Check the status of the controller and queue
    *
    * This is mainly for detecting updated controller/queue status, i.e. if the
    * controller has been hot removed.
    */

   /**
    * Proceed to I/O handling
    */
   qinfo->timeout[cmdInfo->timeoutId] --;
   if (cmdInfo->cmdBase) {
      baseInfo = cmdInfo->cmdBase;

      if (baseInfo != cmdInfo) {
         /**
          * This is a splitted command
          */
         NvmeCore_PutCmdInfo(qinfo, cmdInfo);
      } else {
         /**
          * The base cmd is being completed here. We cannot return the base
          * command to the free list at this stage, because there might be
          * split commands still outstanding that is depending on this base
          * command. So the command will remain in the active cmd list. This is
          * normally ok because when the last split command has been completed,
          * we will proceed to process the base command again and eventually
          * return it to the free list at the end of this function; however, if
          * the queue is flushed for NVM reset, there could be problems because
          * the base command might be picked up and completed again even if it
          * has already been processed here.
          *
          * To handle this scenario, we set the base command's completion
          * handler to a dummy one, so that when we flush the active cmd list
          * during NVM reset, the dummy completion handler is used for a base
          * command that has already been processed by scsiIoCompleteCommand.
          */
         baseInfo->done = scsiIoDummyCompleteCommand;
      }

      if (baseInfo->cmdCount -- > 1) {
         /**
          * This is not the last splitted command, continue processing.
          */
         return;
      }

      cmdInfo = baseInfo;
   }

   vmkCmd = cmdInfo->vmkCmd;
   VMK_ASSERT(vmkCmd);

   qinfo->nrReq --;
   cmd = &cmdInfo->nvmeCmd;
   /* Check OVERRUN/UNDERRUN for READ and WRITE commands.
    * Other commands dont need this because no bytesXferred reported by hw
    */
   if((cmd->header.opCode == NVM_CMD_READ) ||
      (cmd->header.opCode == NVM_CMD_WRITE)) {
      vmkCmd->bytesXferred = cmdInfo->requestedLength;
      if ( vmkCmd->bytesXferred != (vmkCmd->lbc << cmdInfo->ns->lbaShift)) {
         if (vmkCmd->bytesXferred < (vmkCmd->lbc << cmdInfo->ns->lbaShift)) {
            /**
             * UNDERRUN condition
             */
            nvmeStatus = NVME_STATUS_UNDERRUN;
         } else {
            nvmeStatus = NVME_STATUS_OVERRUN;
         }

         Nvme_LogVerb("vmkCmd %p[%Xh I:%p SN:0x%lx] %s %d/%d.",
                      vmkCmd, vmkCmd->cdb[0], vmkCmd->cmdId.initiator,
                      vmkCmd->cmdId.serialNumber,
                      nvmeStatus == NVME_STATUS_UNDERRUN ?
                        "UNDERRUN" : "OVERRUN",
                      vmkCmd->bytesXferred,
                      (vmkCmd->lbc << cmdInfo->ns->lbaShift));
      }
   }
   NvmeScsiCmd_SetReturnStatus(vmkCmd, nvmeStatus);
#if NVME_MUL_COMPL_WORLD
   Nvme_IOCOmpletionEnQueue(qinfo->ctrlr, vmkCmd);
#else
   NvmeScsiCmd_CompleteCommand(vmkCmd);
#endif

   NvmeCore_PutCmdInfo(qinfo, cmdInfo);
}


/**
 * nvmeCoreSubmitIoRequest - submit an I/O command to an I/O queue.
 *
 * @param [IN]  qinfo     pointer to the completion queue
 * @param [IN]  ns        pointer to namespace info
 * @param [IN]  vmkCmd    poniter to SCSI command to be processed
 * @param [IN]  retries   retry counts
 *
 * @return      NVME_STATUS_WOULD_BLOCK  if the command is submitted to hardware
 *                                       for processing sucessfully, the command
 *                                       will be returned in completion context.
 *              others                   failed to submit command to hardware,
 *                                       the command needs to be terminated and
 *                                       returned to storage stack immediately.
 *
 * @note        It is assumed that queue lock is held by caller.
 */
static Nvme_Status
nvmeCoreSubmitIoRequest(struct NvmeQueueInfo *qinfo, struct NvmeNsInfo *ns,
                vmk_ScsiCommand *vmkCmd, int retries)
{
   Nvme_Status              nvmeStatus;
   struct NvmeCtrlr        *ctrlr  = qinfo->ctrlr;
   struct NvmeCmdInfo      *cmdInfo, *baseInfo;
   struct nvme_cmd         *cmd;
   vmk_ByteCount            length;
   vmk_DMADirection         dmaDir;

   /**
    * TODO:
    *
    * 1. Check ctrlr status here
    * 2. Check NS status here
    * 3. Check congestion queue status here
    */

   if (!(ns->flags & NS_ONLINE)) {
      Nvme_LogDebug("*** ERROR *** Received request while Offlined. ns_id %d",
                    ns->id);
      return NVME_STATUS_QUIESCED;
   }

   if (NvmeState_GetCtrlrState(ctrlr, VMK_FALSE) == NVME_CTRLR_STATE_INRESET) {
      Nvme_LogDebug("****** Error Completion Command %p, dev state %d",
                    vmkCmd, NvmeState_GetCtrlrState(ctrlr, VMK_FALSE));
      return NVME_STATUS_IN_RESET;
   }

   /**
    * Initialize the status to WOULD_BLOCK
    */
   baseInfo = NULL;

   do {
      cmdInfo = NvmeCore_GetCmdInfo(qinfo);
      if (!cmdInfo) {
         /**
          * We ran out of command slot now. break here and let error handling
          * process this error.
          */
         Nvme_LogVerb("qinfo %p [%d] failing request, qfull.",
                      qinfo, qinfo->id);
         nvmeStatus = NVME_STATUS_QFULL;
         break;
      }

      if (!baseInfo) {
         /**
          * This is the base command
          */
         baseInfo                   = cmdInfo;
         baseInfo->vmkCmd           = vmkCmd;
         baseInfo->cmdStatus        = 0;
         baseInfo->requestedLength  = 0;
         baseInfo->requiredLength   = vmk_SgGetDataLen(vmkCmd->sgIOArray);
         baseInfo->cmdRetries       = retries;
      } else {
         cmdInfo->vmkCmd      = NULL;
      }

      cmdInfo->cmdCount = 0;
      cmdInfo->cmdBase  = baseInfo;
      cmdInfo->ns       = ns;

      cmd = &cmdInfo->nvmeCmd;
      Nvme_Memset64(cmd, 0LL, sizeof(&cmd)/sizeof(vmk_uint64));

      /*
       * process bio sglist and setup prp list.
       */
      if (vmk_ScsiIsReadCdb(vmkCmd->cdb[0])) {
         cmd->header.opCode = NVM_CMD_READ;
         dmaDir = VMK_DMA_DIRECTION_TO_MEMORY;
      } else {
         VMK_ASSERT(vmk_ScsiIsWriteCdb(vmkCmd->cdb[0]));
         cmd->header.opCode = NVM_CMD_WRITE;
         dmaDir = VMK_DMA_DIRECTION_FROM_MEMORY;
      }

      cmd->header.namespaceID = ns->id;
      length = NvmeIo_ProcessSgArray(qinfo, cmdInfo, vmkCmd, dmaDir);
      /*
       * Length should be a multiply of sector size (1 << ns->lbaShift).
       */
      VMK_ASSERT((length & ((1 << ns->lbaShift) - 1)) == 0);

      cmd->cmd.read.numLBA   = (length >> ns->lbaShift) - 1;
      cmd->cmd.read.startLBA = vmkCmd->lba +
                               (baseInfo->requestedLength >> ns->lbaShift);

      cmd->header.cmdID      = cmdInfo->cmdId;
      cmdInfo->timeoutId     = ctrlr->timeoutId;
      qinfo->timeout[cmdInfo->timeoutId] ++;

#if DO_IO_STAT
      cmdInfo->startTime = OsLib_GetTimerUs();
#endif

      cmdInfo->type   = BIO_CONTEXT;
      cmdInfo->status = NVME_CMD_STATUS_ACTIVE;

      nvmeStatus = NvmeCore_SubmitCommandAsync(qinfo, cmdInfo,
                                               scsiIoCompleteCommand);
      if (nvmeStatus != NVME_STATUS_SUCCESS) {
         /**
          * Failed to submit command to the hardware.
          */
         Nvme_LogVerb("qinfo %p[%d] failed to submit command, 0x%x, %s.",
                      qinfo, qinfo->id, nvmeStatus,
                      NvmeCore_StatusToString(nvmeStatus));
         NvmeCore_PutCmdInfo(qinfo, cmdInfo);
         if (baseInfo == cmdInfo) {
            baseInfo = NULL;
         }
         break;
      }

      /*
       * Update commands sent for request.
       */
      baseInfo->cmdCount ++;
      baseInfo->requestedLength += cmdInfo->count;

   } while (baseInfo->requestedLength < baseInfo->requiredLength);

   if (nvmeStatus != NVME_STATUS_SUCCESS) {
      /**
       * We ran into some errors during command submission.
       */
      if (baseInfo) {
         /**
          * Check whether the command has been partially submitted to hardware
          */
         if (baseInfo->requestedLength == 0) {
            /**
             * The command has never been submitted to hardware.
             *
             * Since the SCSI command never reached the hardware, it is safe to
             * just return QFULL condition here.
             */

            nvmeStatus = NVME_STATUS_QFULL;
         } else {
            /**
             * The SCSI command has been split and some of the sub commands has
             * been submitted to hardware. We have two options here:
             *
             * 1. put the request into a congestion queue for later retry; or
             * 2. just complete the command here and raise an UNDERRUN condition
             *
             * We go with option two here for simplicity.
             */
            Nvme_LogDebug("UNDERRUN: vmkCmd %p[%Xh I:%p SN:0x%lx] %d/%ld",
                          vmkCmd, vmkCmd->cdb[0], vmkCmd->cmdId.initiator,
                          vmkCmd->cmdId.serialNumber, baseInfo->requestedLength,
                          baseInfo->requiredLength);

            /**
             * Return WOULD_BLOCK to indicate the command will be completed
             * in completion context.
             */
            nvmeStatus = NVME_STATUS_WOULD_BLOCK;
         }
      } else {
         /**
          * Can't get the first cmd info out of the queue, must be in QFULL
          * condition.
          */
         VMK_ASSERT(nvmeStatus == NVME_STATUS_QFULL);
         nvmeStatus = NVME_STATUS_QFULL;
      }
   }

   if (baseInfo) {
      if (baseInfo->cmdCount == 1) {
         baseInfo->cmdBase = NULL;
      }
   }

   /**
    * If all the command submissions are successful, return WOULD_BLOCK to
    * indicate the commands will be completed in completion context.
    */
   if (nvmeStatus == NVME_STATUS_SUCCESS) {
      nvmeStatus = NVME_STATUS_WOULD_BLOCK;
   }

   return nvmeStatus;
}


/**
 * NvmeIo_SubmitIo - submit a SCSI command to a namespace
 */
Nvme_Status
NvmeIo_SubmitIo(struct NvmeNsInfo *ns, vmk_ScsiCommand *vmkCmd)
{
   Nvme_Status           nvmeStatus;
   struct NvmeCtrlr     *ctrlr = ns->ctrlr;
   struct NvmeQueueInfo *qinfo;
   int                   qid;

   /* Get the queue for submitting I/O.
    * Note: we should prevent the mismatch between no. of SCSI completion queues
    * and no. of sq/cqs on the hardware.
    */
   qid = OsLib_GetQueue(ctrlr, vmkCmd);
   if (qid >= ctrlr->numIoQueues) {
      /**
       * This could only happen if our driver has been quiesced before PSA
       * quiesce completes.
       */
      Nvme_LogError("invalid compleiton queue: %d numIoQueues: %d.",
                    qid, ctrlr->numIoQueues);
      return NVME_STATUS_QUIESCED;
   }

   qinfo = &ctrlr->ioq[qid];
   DPRINT6("ns_id %d, Cmd %p[0x%x], Qinfo %p [%d], lba 0x%lx lbc %d",
            ns->id, vmkCmd, vmkCmd->cdb[0], qinfo, qinfo->id, vmkCmd->lba,
            vmkCmd->lbc);

#if NVME_DEBUG
   if (nvme_dbg & NVME_DEBUG_DUMP) {
      NvmeDebug_DumpSgArray(vmkCmd->sgArray);
   }
#endif

   qinfo->lockFunc(qinfo->lock);
   nvmeStatus = nvmeCoreSubmitIoRequest(qinfo, ns, vmkCmd, 0);

   /**
    * Accounting for the number of IO requests to the queue
    */
   if (nvmeStatus == NVME_STATUS_WOULD_BLOCK) {
      qinfo->nrReq ++;
      if (qinfo->maxReq < qinfo->nrReq) {
         qinfo->maxReq = qinfo->nrReq;
      }
   }

   qinfo->unlockFunc(qinfo->lock);

   return nvmeStatus;
}


/**
 * NvmeIo_SubmitDsm - submit a Dataset Management command to a namespace
 */
Nvme_Status
NvmeIo_SubmitDsm(struct NvmeNsInfo *ns, vmk_ScsiCommand *vmkCmd,
                 struct nvme_dataset_mgmt_data *dsmData, int count)
{
   Nvme_Status              nvmeStatus;
   struct NvmeCtrlr        *ctrlr;
   struct nvme_cmd         *cmd;
   struct NvmeCmdInfo      *cmdInfo;
   struct NvmeQueueInfo    *qinfo;
   struct NvmeSubQueueInfo *sqInfo;
   int                      qid;

   ctrlr  = ns->ctrlr;
   qid = OsLib_GetQueue(ctrlr, vmkCmd);
   if (qid >= ctrlr->numIoQueues) {
      qid = 0;
   }
   qinfo  = &ctrlr->ioq[qid];
   sqInfo = qinfo->subQueue;

   qinfo->lockFunc(qinfo->lock);
   cmdInfo = NvmeCore_GetCmdInfo(qinfo);
   qinfo->unlockFunc(qinfo->lock);

   if (!cmdInfo) {
      return NVME_STATUS_QFULL;
   }

   cmdInfo->cmdCount        = 1;
   cmdInfo->vmkCmd          = vmkCmd;
   cmdInfo->cmdStatus       = 0;
   cmdInfo->requiredLength  = count * sizeof(struct nvme_dataset_mgmt_data);
   cmdInfo->requestedLength = cmdInfo->requiredLength;
   cmdInfo->cmdRetries      = 0;
   cmdInfo->ns              = ns;
   cmdInfo->cmdBase         = NULL;  /* no split command */

   cmd = &cmdInfo->nvmeCmd;
   Nvme_Memset64(cmd, 0LL, sizeof(*cmd)/sizeof(vmk_uint64));

   /*
    * We use cmdInfo's prp pool to save the DSM data
    */
   vmk_Memcpy(cmdInfo->prps, dsmData,
      count * sizeof(struct nvme_dataset_mgmt_data));

   /*
    * Populate DSM command data
    */
   cmd->header.opCode = NVM_CMD_DATASET_MGMNT;
   cmd->header.namespaceID = ns->id;
   cmd->header.prp[0].addr = cmdInfo->prpPhy;
   cmd->header.prp[1].addr = 0;
   cmd->cmd.dataset.numRanges = count - 1; /* 0 based value */
   cmd->cmd.dataset.attribute = (1 << 2); /* Deallocate */

   cmd->header.cmdID = cmdInfo->cmdId;
   cmdInfo->timeoutId = ctrlr->timeoutId;
   qinfo->timeout[cmdInfo->timeoutId] ++;

   cmdInfo->type = BIO_CONTEXT;
   cmdInfo->status = NVME_CMD_STATUS_ACTIVE;

   qinfo->lockFunc(qinfo->lock);
   nvmeStatus = NvmeCore_SubmitCommandAsync(qinfo, cmdInfo,
                                            scsiIoCompleteCommand);
   if (nvmeStatus != NVME_STATUS_SUCCESS) {
      /**
       * Failed to submit the command to the hardware.
       */
      NvmeCore_PutCmdInfo(qinfo, cmdInfo);
   }

   if (SUCCEEDED(nvmeStatus)) {
      /**
       * Return WOULD_BLOCK indicating the command will be completed in
       * completion context.
       */
      nvmeStatus = NVME_STATUS_WOULD_BLOCK;
   }

   /**
    * Accounting for the number of IO requests to the queue
    */
   if (nvmeStatus == NVME_STATUS_WOULD_BLOCK) {
      qinfo->nrReq ++;
      if (qinfo->maxReq < qinfo->nrReq) {
         qinfo->maxReq = qinfo->nrReq;
      }
   }

   qinfo->unlockFunc(qinfo->lock);

   return nvmeStatus;
}
