/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (c) 2012-2013  Integrated Device Technology, Inc.
 * All rights reserved.
 *******************************************************************************/

/*
 * @file: nvme_core.c --
 *
 *    Nvme core related stuff
 */

#include "vmkapi.h"
//#include "../../common/kernel/nvme_debug.h"
//#include "nvme_os.h"
#include "oslib.h"
//#include "../../common/kernel/nvme_private.h"

/**
 * Only Temp
 */
//#include "nvme_scsi_cmds.h"


/******************************************************************************
 * Temporary Zone
 *
 * TODO: for those who doesn't belong to anybody for now.
 *****************************************************************************/



const char * Nvme_StatusString[] = {
   "SUCCESS",
   "DEVICE MISSING",
   "NOT READY",
   "IN RESET",
   "QUIESCED",
   "FATAL ERROR",
   "MEDIUM ERROR",
   "QFULL",
   "BUSY",
   "INVALID OPCODE",
   "INVALID FIELD IN CDB",
   "INVALID NS OR FORMAT",
   "NS NOT READY",
   "NS OFFLINE",
   "IO ERROR",
   "IO WRITE ERROR",
   "IO READ ERROR",
   "ABORTED",
   "TIMEOUT",
   "RESET",
   "WOULD BLOCK",
   "UNDERRUN",
   "OVERRUN",
   "LBA OUT OF RANGE",
   "CAPACITY EXCEEDED",
   "CONFLICT ATTRIBUTES",
   "INVALID PI",
   "PROTOCOL ERROR",
   "BAD PARAM",
   "FAILURE",
   "MEDIA ERROR",
   "OVERTEMP",
   "(invalid)",
};


VMK_ASSERT_LIST(nvmeStatusAssertions,
   VMK_ASSERT_ON_COMPILE(sizeof(Nvme_StatusString) / sizeof(const char *)
                         == (NVME_STATUS_LAST + 1));
)


/**
 * NvmeCore_StatusToString - refer to header file.
 */
inline const char *
NvmeCore_StatusToString(Nvme_Status nvmeStatus)
{
   if (nvmeStatus < 0 || nvmeStatus >= NVME_STATUS_LAST) {
      nvmeStatus = NVME_STATUS_LAST;
   }

   return Nvme_StatusString[nvmeStatus];
}


/**
 * NvmeCore_IsNsOnline - refer to header file.
 */
inline vmk_Bool
NvmeCore_IsNsOnline(struct NvmeNsInfo *ns)
{
   return (vmk_Bool)(ns->flags & NS_ONLINE);
}


/**
 * NvmeScsi_UpdatePaths - update SCSI paths status based on the currnet
 *                        namespace list on the ctrlr
 *
 * @note    this function could block.
 */
VMK_ReturnStatus
NvmeScsi_UpdatePaths(struct NvmeCtrlr *ctrlr, vmk_Bool isOnline)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;

   if (isOnline) {
      /**
       * Scan and claim newly onlined namespace
       */
      vmkStatus = vmk_ScsiScanAndClaimPaths(&ctrlr->ctrlOsResources.scsiAdapter->name,
                                            0,
                                            0,
                                            VMK_SCSI_PATH_ANY_LUN);
   } else {
      /**
       * TODO:
       *
       * We should try to unclaim SCSI paths here since the namespaces are
       * already offline. However, the VMKAPI asked as iterate through all the
       * LUNs and unclaim/delete each one by one. To do that, we should go
       * through ctrlr->nsList, but the only lock around ctrlr->nsList is a
       * spin lock which we cannot grab while unclaiming/delething paths (which
       * is a blocking operation.
       *
       * So before we have other protections setup around ctrlr->nsList, just
       * ignore the offline auto cleanup. In this way, end user will need to
       * manually issue rescan on the vmhba to cleanup all the LUNs.
       */
       vmkStatus = VMK_OK;
   }

   return vmkStatus;
}


/**
 * NvmeCore_SetNsOnline - refer to the header file.
 */
Nvme_Status
NvmeCore_SetNsOnline(struct NvmeNsInfo *ns, vmk_Bool isOnline)
{
   /**
    * Do nothing if namespace is already online/offline
    */
   vmk_SpinlockLock(ns->lock);

   if (NvmeCore_IsNsOnline(ns) == isOnline) {
      vmk_SpinlockUnlock(ns->lock);
      return NVME_STATUS_SUCCESS;
   }

   if (isOnline) {
      ns->flags |= NS_ONLINE;
   } else {
      ns->flags &= ~NS_ONLINE;
   }

   vmk_SpinlockUnlock(ns->lock);

   return NVME_STATUS_SUCCESS;
}

/**
 * NvmeCore_ValidateNs - refer to the header file.
 */
VMK_ReturnStatus
NvmeCore_ValidateNs(struct NvmeNsInfo *ns)
{
   /**
    * Dump the info of namespace.
    */
   if (nvme_dbg & NVME_DEBUG_DUMP_NS) {
      NvmeDebug_DumpNsInfo(ns);
   }

   /**
    * We don't export offlined namespaces
    */
   if (!NvmeCore_IsNsOnline(ns)) {
      return VMK_NO_CONNECT;
   }

   /**
    * Validate block size
    */
   if (ns->blockCount <= 0) {
      EPRINT("Size of namespace is invalid, current size: %lu.",
             ns->blockCount);
      goto out;
   }

   /**
    * We only support fixed sector size (512)
    */
   if (1 << ns->lbaShift != VMK_SECTOR_SIZE) {
      EPRINT("LBA size not supported, required 512, formatted %d.",
             1 << ns->lbaShift);
      goto out;
   }

   /**
    * We don't support metadata for now.
    */
   if (ns->metasize != 0) {
      EPRINT("Metadata not supported, current metadata size: %d.",
             ns->metasize);
      goto out;
   }

   /**
    * PI not implemented yet
    */
   if (ns->dataProtSet) {
      EPRINT("Data Protection not supported, set 0x%x.",
             ns->dataProtSet);
      goto out;
   }

   return VMK_OK;

out:

   /**
    * Offline the unsupported namespace.
    */
   NvmeCore_SetNsOnline(ns, VMK_FALSE);
   return VMK_NOT_SUPPORTED;
}

/**
 * NvmeCore_SetCtrlrOnline - refer to the header file.
 */
Nvme_Status
NvmeCore_SetCtrlrOnline(struct NvmeCtrlr *ctrlr, vmk_Bool isOnline)
{
   vmk_ListLinks     *itemPtr, *nextPtr;
   struct NvmeNsInfo *ns;
   Nvme_Status        nvmeStatus, rc;

   vmk_SpinlockLock(ctrlr->lock);

   rc = NVME_STATUS_SUCCESS;

   VMK_LIST_FORALL_SAFE(&ctrlr->nsList, itemPtr, nextPtr) {
      ns = VMK_LIST_ENTRY(itemPtr, struct NvmeNsInfo, list);
      nvmeStatus = NvmeCore_SetNsOnline(ns, isOnline);
      if (!SUCCEEDED(nvmeStatus)) {
         rc = nvmeStatus;
      }
   }

   vmk_SpinlockUnlock(ctrlr->lock);

   /**
    * After all namespaces are marked properly, update SCSI layer path status
    */
   NvmeScsi_UpdatePaths(ctrlr, isOnline);

   return rc;
}

VMK_ReturnStatus
NvmeScsi_UpdatePath(struct NvmeCtrlr *ctrlr, int nsid, vmk_Bool isOnline)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;

   if (isOnline) {
      vmkStatus = vmk_ScsiScanAndClaimPaths(&ctrlr->ctrlOsResources.scsiAdapter->name,
                                            0,
                                            0,
                                            VMK_SCSI_PATH_ANY_LUN);
   } else {
      vmkStatus = vmk_ScsiScanDeleteAdapterPath(&ctrlr->ctrlOsResources.scsiAdapter->name,
                                                0,
                                                0,
                                                nsid - 1);
   }

   return vmkStatus;
}


/**
 * NvmeCore_SetNamespaceOnline - refer to the header file.
 */
Nvme_Status
NvmeCore_SetNamespaceOnline(struct NvmeCtrlr *ctrlr, vmk_Bool isOnline, int nsid)
{
   vmk_ListLinks     *itemPtr, *nextPtr;
   struct NvmeNsInfo *ns = NULL;
   Nvme_Status        nvmeStatus = NVME_STATUS_FAILURE;

   vmk_SpinlockLock(ctrlr->lock);
   VMK_LIST_FORALL_SAFE(&ctrlr->nsList, itemPtr, nextPtr) {
      ns = VMK_LIST_ENTRY(itemPtr, struct NvmeNsInfo, list);
      if (ns->id == nsid) {
         nvmeStatus = NvmeCore_SetNsOnline(ns, isOnline);
         break;
      }
   }
   vmk_SpinlockUnlock(ctrlr->lock);

   if (nvmeStatus) {
      return nvmeStatus;
   }

   NvmeScsi_UpdatePath(ctrlr, nsid, isOnline);

   if (ns != NULL && NvmeCore_IsNsOnline(ns) != isOnline) {
      return NVME_STATUS_FAILURE;
   }
   return NVME_STATUS_SUCCESS;
}

/******************************************************************************
 * NVMe Command Completion Routines
 *****************************************************************************/

/**
 * nvmeCoreLogError  - log command failures
 */
static void
nvmeCoreLogError(struct NvmeCmdInfo *cmdInfo)
{
   EPRINT("command failed: %p.", cmdInfo);
}


/**
 * NvmeCore_GetStatus - refer to the header file.
 */
Nvme_Status
NvmeCore_GetStatus(struct cq_entry *cqEntry)
{
   Nvme_Status nvmeStatus = NVME_STATUS_SUCCESS;

   if (VMK_LIKELY(cqEntry->SCT == 0 && cqEntry->SC == 0)) {
      return NVME_STATUS_SUCCESS;
   }

   switch (cqEntry->SCT) {
      case SF_SCT_GENERIC:
         {
            switch (cqEntry->SC) {
               case SF_SC_INV_OPCODE:
                  nvmeStatus = NVME_STATUS_INVALID_OPCODE;
                  break;
               case SF_SC_INV_FIELD:
                  nvmeStatus = NVME_STATUS_INVALID_FIELD_IN_CDB;
                  break;
               case SF_SC_CMD_ID_CFLT:
                  nvmeStatus = NVME_STATUS_PROTOCOL_ERROR;
                  break;
               case SF_SC_DATA_TX_ERR:
                  nvmeStatus = NVME_STATUS_IO_ERROR;
                  break;
               case SF_SC_CMD_ABORT_NP:
                  nvmeStatus = NVME_STATUS_DEVICE_MISSING;
                  break;
               case SF_SC_INT_DEV_ERR:
                  nvmeStatus = NVME_STATUS_MEDIUM_ERROR;
                  break;
               case SF_SC_CMD_ABORT_NSQ:
                  nvmeStatus = NVME_STATUS_QUIESCED;
                  break;
               case SF_SC_CMD_ABORT_FUSE_FAIL:
                  nvmeStatus = NVME_STATUS_PROTOCOL_ERROR;
                  break;
               case SF_SC_CMD_ABORT_FUSE_MISSING:
                  nvmeStatus = NVME_STATUS_PROTOCOL_ERROR;
                  break;
               case SF_SC_INV_NS_FMT:
                  nvmeStatus = NVME_STATUS_INVALID_NS_OR_FORMAT;
                  break;
               case SF_SC_INV_LBA:
                  nvmeStatus = NVME_STATUS_LBA_OUT_OF_RANGE;
                  break;
               case SF_SC_INV_CAP:
                  nvmeStatus = NVME_STATUS_CAPACITY_EXCEEDED;
                  break;
               case SF_SC_NS_NOT_READY:
                  nvmeStatus = NVME_STATUS_NS_NOT_READY;
                  break;
               default:
                  nvmeStatus = NVME_STATUS_FAILURE;
                  break;
            }
         }
         break;
      case SF_SCT_CMD_SPC_ERR:
         {
            switch (cqEntry->SC) {
               case SC_CMD_SPC_ERR_INV_CPL_Q:
               case SC_CMD_SPC_ERR_INV_Q_ID:
               case SC_CMD_SPC_ERR_EXCEED_Q_SIZE:
               case SC_CMD_SPC_ERR_EXCEED_ABORT_LMT:
               case SC_CMD_SPC_ERR_ABORT_CMD_NOT_FOUND:
               case SC_CMD_SPC_ERR_EXCEED_ASYNC_ENT_LMT:
               case SC_CMD_SPC_ERR_INV_FIRMWARE_SLOT:
               case SC_CMD_SPC_ERR_INV_FIRMWARE_IMAGE:
               case SC_CMD_SPC_ERR_INV_INT_VECTOR:
               case SC_CMD_SPC_ERR_INV_LOG_PAGE:
               case SC_CMD_SPC_ERR_INV_FORMAT:
                  /**
                   * The above are for ADMIN errors;
                   */
                  nvmeStatus = NVME_STATUS_PROTOCOL_ERROR;
                  break;
               case SC_CMD_SPC_FW_APP_REQ_CONVENT_RESET:
               case SC_CMD_SPC_FW_APP_REQ_SUBSYS_RESET:
                  nvmeStatus = NVME_STATUS_SUCCESS;
                  break;
               case SC_CMD_SPC_ERR_ATTR_CFLT:
                  nvmeStatus = NVME_STATUS_CONFLICT_ATTRIBUTES;
                  break;
               case SC_CMD_SPC_ERR_INV_PROT_INFO:
                  nvmeStatus = NVME_STATUS_INVALID_PI;
                  break;
               default:
                  nvmeStatus = NVME_STATUS_FAILURE;
                  break;
            }
         }
         break;
      case SF_SCT_MEDIA_ERR:
         {
            switch (cqEntry->SC) {
               case SC_MEDIA_ERR_WRITE_FLT:
                  nvmeStatus = NVME_STATUS_IO_WRITE_ERROR;
                  break;
               case SC_MEDIA_ERR_UNREC_RD_ERR:
                  nvmeStatus = NVME_STATUS_IO_READ_ERROR;
                  break;
               case SC_MEDIA_ERR_ETE_GUARD_CHK:
               case SC_MEDIA_ERR_ETE_APP_TAG_CHK:
               case SC_MEDIA_ERR_ETE_REF_TAG_CHK:
               case SC_MEDIA_ERR_CMP_FAIL:
               default:
                  nvmeStatus = NVME_STATUS_MEDIUM_ERROR;
                  break;
            }
         }
         break;
      case SF_SCT_VENDOR_SPC:
         nvmeStatus = NVME_STATUS_FAILURE;
         break;
      default:
         nvmeStatus = NVME_STATUS_FAILURE;
         break;
   }

   if (nvmeStatus != NVME_STATUS_SUCCESS) {
      VPRINT("Command failed: 0x%x, %s.", nvmeStatus,
             NvmeCore_StatusToString(nvmeStatus));
   }

#if NVME_DEBUG
   if (nvme_dbg & NVME_DEBUG_DUMP_CPL) {
      NvmeDebug_DumpCpl(cqEntry);
   }
#endif

   return nvmeStatus;
}


/**
 * nvmeCoreProcessCq - main function to be called by ISR
 *
 * @param  [IN]   qinfo   completion queue
 *
 * @param  [IN]   isDumpHandler   Coredump poll handler indicator
 *
 * @note    qinfo->lock shall be held by the caller.
 */
void
nvmeCoreProcessCq(struct NvmeQueueInfo *qinfo, int isDumpHandler)
{
   vmk_uint16 head, phase, sqHead;
   struct cq_entry *cqEntry;
   struct NvmeCmdInfo *cmdInfo;
   Nvme_CtrlrState state;

   head = qinfo->head;
   phase = qinfo->phase;
   sqHead = qinfo->subQueue->head;

   while (1) {

      cqEntry = &qinfo->compq[head];

      /**
       * Completed all outstanding commands in this round, bail out.
       */
      if (cqEntry->phaseTag != phase) {
         break;
      }

#if NVME_DEBUG
      if (nvme_dbg & NVME_DEBUG_DUMP_CPL) {
         NvmeDebug_DumpCpl(cqEntry);
      }
#endif

      /**
       * Validate command ID in cqEntry
       */
      if ((!cqEntry->cmdID) || (cqEntry->cmdID > qinfo->idCount)) {
         EPRINT("Invalid command id: %d.", cqEntry->cmdID);
         VMK_ASSERT(0);
         goto next_entry;
      }

      cmdInfo = &qinfo->cmdList[cqEntry->cmdID - 1];
      sqHead = (vmk_uint16)cqEntry->sqHdPtr;

      /**
       * Validate that the command is still active.
       */
      if (cmdInfo->status != NVME_CMD_STATUS_ACTIVE) {
         EPRINT("Inactive command %p, [%d]", cmdInfo, cmdInfo->cmdId);
         VMK_ASSERT(0);
         goto next_entry;
      }

#if NVME_DEBUG_INJECT_TIMEOUT
      if (qinfo->id && NvmeDebug_ErrorCounterHit(&qinfo->ctrlr->errCounters[NVME_DEBUG_ERROR_TIMEOUT])) {
         vmk_ScsiCommand *vmkCmdTmo = NvmeCore_CmdInfoToScsiCmd(cmdInfo);

         if (vmkCmdTmo) {
            IPRINT("Faking io cmd timeout in completion, "
                   "cmdInfo:%p [%d] cmdBase:%p vmkCmd:%p [%Xh] "
                   "I:%p SN:0x%lx ",
                   cmdInfo, cmdInfo->cmdId, cmdInfo->cmdBase,
                   vmkCmdTmo, vmkCmdTmo->cdb[0],
                   vmkCmdTmo->cmdId.initiator,
                   vmkCmdTmo->cmdId.serialNumber);
         } else {
            IPRINT("Faking admin cmd timeout in completion, "
                   "cmdInfo:%p [%d] cmdBase:%p",
                   cmdInfo, cmdInfo->cmdId, cmdInfo->cmdBase);
         }

         goto next_entry;
      }
#endif

      /**
       * Cache SC, SCT, M, and DNR fields (mask out Phase) from the completion
       * entry for future reference
       */
      cmdInfo->cmdStatus = NvmeCore_GetStatus(cqEntry);

      /**
       * Most of the time we need to cache the completion queue entry in the
       * original cmdInfo, so that the caller submitting the command can get a
       * reply.
       *
       * This could potentially introduce performance problems (due to the copy)
       * of cq entry, maybe in the future we can try to avoid this.
       */
      Nvme_Memcpy64(&cmdInfo->cqEntry, cqEntry,
                    sizeof(cmdInfo->cqEntry)/sizeof(vmk_uint64));

      /**
       * Decode and log errors, if there are any.
       *
       * We only log errors here, error recovery is done by completion routines
       * of each type of command.
       */
      if (VMK_UNLIKELY(cmdInfo->cmdStatus)) {
         nvmeCoreLogError(cmdInfo);
      }

      /**
       * TODO: do error injection testing here.
       */


      /**
       * Now dispatch the command to its corresponding completion routine.
       */
      if (isDumpHandler) {
         cmdInfo->isDumpCmd = 1;
      }

      if (cmdInfo->done) {
         cmdInfo->done(qinfo, cmdInfo);

         #if (NVME_ENABLE_IO_STATS == 1)
            STATS_Increment(qinfo->ctrlr->statsData.TotalCompletions);
         #endif
      } else {
         vmk_ScsiCommand *vmkCmd;

         GET_VMK_SCSI_CMD(cmdInfo->cmdPtr, vmkCmd);

         EPRINT("skipping cmd %p [%d] base %p vmkCmd %p, " "no completion handler.",
                cmdInfo, cmdInfo->cmdId, cmdInfo->cmdBase, vmkCmd);
         NvmeCore_PutCmdInfo(qinfo, cmdInfo);
         VMK_ASSERT(0);
      }

next_entry:

      if (++head >= qinfo->qsize) {
         cqEntry = qinfo->compq;
         head = 0;
         phase = !phase;
      }
   }

   /**
    * Now we are out of the main loop.
    */

   state = NvmeState_GetCtrlrState(qinfo->ctrlr, VMK_FALSE);

   if (VMK_UNLIKELY((head == qinfo->head) && (phase == qinfo->phase))) {
      /**
       * No command was processed in this invocation of completion.
       */
      sqHead = qinfo->subQueue->head;
   } else {
      qinfo->head = head;
      qinfo->phase = phase;

      /**
       * If current state is INRESET or QUIESCED, then HwCtrlr has been stopped. We can't
       * write the doorbell of CQ.
       */

      if (state != NVME_CTRLR_STATE_INRESET && state != NVME_CTRLR_STATE_QUIESCED) {
         Nvme_Writel(head, qinfo->doorbell);
      }
   }

   /**
    * Adjust submission queue info based on the sqHead we've got
    */
   {
      struct NvmeSubQueueInfo *sqInfo = qinfo->subQueue;
      vmk_SpinlockLock(sqInfo->lock);
      if (sqHead <= sqInfo->tail) {
         sqInfo->entries = sqInfo->qsize - ((sqInfo->tail - sqHead) + 1);
      } else {
         sqInfo->entries = (sqHead - sqInfo->tail) - 1;
      }

      DPRINT_Q("Sub Queue Entries [%d] tail %d, head %d.",
         sqInfo->entries, sqInfo->tail, sqHead);

      sqInfo->head = sqHead;
      vmk_SpinlockUnlock(sqInfo->lock);
   }
}


/******************************************************************************
 * NVMe Command Submission Routines
 *****************************************************************************/


/**
 * NvmeCore_PutCmdInfo     - refer to header file.
 */
void
NvmeCore_PutCmdInfo(struct NvmeQueueInfo *qinfo, struct NvmeCmdInfo *cmdInfo)
{
   cmdInfo->cmdBase = NULL;
   cmdInfo->done    = NULL;
   cmdInfo->cleanup = NULL;
   vmk_ListRemove(&cmdInfo->list);
   vmk_ListInsert(&cmdInfo->list, vmk_ListAtRear(&qinfo->cmdFree));
   qinfo->nrAct --;
   DPRINT_CMD("Put Cmd Info [%d] %p back to queue [%d], nrAct: %d.",
               cmdInfo->cmdId, cmdInfo, qinfo->id, qinfo->nrAct);
}


/**
 * NvmeCore_GetCmdInfo     - refer to header file.
 *
 */
struct NvmeCmdInfo *
NvmeCore_GetCmdInfo(struct NvmeQueueInfo *qinfo)
{
   struct NvmeCmdInfo *cmdInfo;

   if (VMK_UNLIKELY(vmk_ListIsEmpty(&qinfo->cmdFree))) {
      DPRINT_NS("Queue [%d] Command List Empty.", qinfo->id);
      return (NULL);
   }

   if (VMK_UNLIKELY(NvmeCore_IsQueueSuspended(qinfo))) {
      DPRINT_NS("Queue [%d] Suspended.", qinfo->id);
      return (NULL);
   }

   cmdInfo = VMK_LIST_ENTRY(vmk_ListFirst(&qinfo->cmdFree),
                            struct NvmeCmdInfo, list);
   vmk_ListRemove(&cmdInfo->list);
   vmk_ListInsert(&cmdInfo->list, vmk_ListAtRear(&qinfo->cmdActive));
   qinfo->nrAct ++;

   DPRINT_CMD("Queue [%d] Cmd Info [%d] %p.",
      qinfo->id, cmdInfo->cmdId, cmdInfo);

   return cmdInfo;
}


/**
 * nvmeCoreProcessAbortedCommand - helper for processing aborted commands.
 */
static inline void
nvmeCoreProcessAbortedCommand(struct NvmeQueueInfo *qinfo,
                              struct NvmeCmdInfo *cmdInfo)
{
   VPRINT("aborted cmd %p [%d] opCode:0x%x in queue %d.",
          cmdInfo, cmdInfo->cmdId, cmdInfo->nvmeCmd.header.opCode,
          qinfo->id);
}


/**
 * nvmeCoreCompleteCommandWait - completion callback for sleep wait synchronous
 *                               commands.
 */
static void
nvmeCoreCompleteCommandWait(struct NvmeQueueInfo *qinfo,
                            struct NvmeCmdInfo *cmdInfo)
{
   if (VMK_UNLIKELY(cmdInfo->type == ABORT_CONTEXT)) {
      nvmeCoreProcessAbortedCommand(qinfo, cmdInfo);
   } else {
      cmdInfo->status = NVME_CMD_STATUS_DONE;
      if (cmdInfo->doneData) {
         Nvme_Memcpy64(cmdInfo->doneData, &cmdInfo->cqEntry,
                       sizeof(struct cq_entry)/sizeof(vmk_uint64));
      }
      vmk_WorldWakeup((vmk_WorldEventID) cmdInfo);
   }

   if (cmdInfo->cleanup) {
      cmdInfo->cleanup(qinfo, cmdInfo);
   }

   NvmeCore_PutCmdInfo(qinfo, cmdInfo);
   qinfo->timeout[cmdInfo->timeoutId] --;
}


/**
 * nvmeCoreCompleteCommandPoll - completion callback for busy wait synchronous
 *                               commands.
 */
static void
nvmeCoreCompleteCommandPoll(struct NvmeQueueInfo *qinfo,
                            struct NvmeCmdInfo *cmdInfo)
{
   if (VMK_UNLIKELY(cmdInfo->type == ABORT_CONTEXT)) {
      nvmeCoreProcessAbortedCommand(qinfo, cmdInfo);
   } else {
      /**
       * Another thread is polling for this.
       */
      if (cmdInfo->doneData) {
         Nvme_Memcpy64(cmdInfo->doneData, &cmdInfo->cqEntry,
                       sizeof(struct cq_entry)/sizeof(vmk_uint64));
      }
      cmdInfo->status = NVME_CMD_STATUS_DONE;
   }

   if (cmdInfo->cleanup) {
      cmdInfo->cleanup(qinfo, cmdInfo);
   }

   NvmeCore_PutCmdInfo(qinfo, cmdInfo);
   qinfo->timeout[cmdInfo->timeoutId] --;
}


#if ENABLE_REISSUE
/**
 * NvmeCore_ReissueCommand - refer to the header file
 */
Nvme_Status NvmeCore_ReissueCommand(struct NvmeQueueInfo *qinfo, struct NvmeCmdInfo *cmdInfo)
{
   vmk_uint32               tail;
   struct NvmeSubQueueInfo *sqInfo = qinfo->subQueue;
   vmk_ScsiCommand *vmkCmd;

   GET_VMK_SCSI_CMD(cmdInfo->cmdPtr, vmkCmd);

   VPRINT ("enter. vmkCmd = %p", vmkCmd);

   vmk_SpinlockLock(sqInfo->lock);

   tail            = sqInfo->tail;

   VMK_ASSERT (cmdInfo->status == NVME_CMD_STATUS_ACTIVE);
   VMK_ASSERT (cmdInfo->done   !=  NULL);

   if (VMK_UNLIKELY(sqInfo->entries <= 0)) {
      EPRINT("Submission queue is full %p [%d]",
                         sqInfo, sqInfo->id);
      vmk_SpinlockUnlock(sqInfo->lock);
      return NVME_STATUS_QFULL;
   }

   Nvme_Memcpy64(&sqInfo->subq[sqInfo->tail], &cmdInfo->nvmeCmd,
      sizeof(cmdInfo->nvmeCmd)/sizeof(vmk_uint64));

#if NVME_DEBUG
   if (nvme_dbg & NVME_DEBUG_DUMP_CMD) {
      NvmeDebug_DumpCmd(&cmdInfo->nvmeCmd);
   }
#endif
   cmdInfo->cmdRetries--;

   tail ++;
   if (tail >= sqInfo->qsize) {
      tail = 0;
   }

   Nvme_Writel(tail, sqInfo->doorbell);
   sqInfo->tail = tail;
   sqInfo->entries --;

   vmk_SpinlockUnlock(sqInfo->lock);

   return NVME_STATUS_SUCCESS;
}
#endif



/**
 * NvmeCore_SubmitCommandAsync - refer to the header file
 */
Nvme_Status
NvmeCore_SubmitCommandAsync(struct NvmeQueueInfo *qinfo,
                            struct NvmeCmdInfo *cmdInfo,
                            NvmeCore_CompleteCommandCb cb)
{
   vmk_uint32               tail;
   struct NvmeSubQueueInfo *sqInfo = qinfo->subQueue;

   vmk_SpinlockLock(sqInfo->lock);

   tail            = sqInfo->tail;
   cmdInfo->status = NVME_CMD_STATUS_ACTIVE;
   cmdInfo->done   = cb;

   if (VMK_UNLIKELY(sqInfo->entries <= 0)) {
      EPRINT("Submission queue is full %p [%d]", sqInfo, sqInfo->id);
      vmk_SpinlockUnlock(sqInfo->lock);
      return NVME_STATUS_QFULL;
   }

   if (VMK_UNLIKELY(NvmeCore_IsQueueSuspended(qinfo))) {
      EPRINT("Failed to submit command %p[%d] to queue %d, suspended.",
             cmdInfo, cmdInfo->cmdId, qinfo->id);
      vmk_SpinlockUnlock(sqInfo->lock);
      return NVME_STATUS_IN_RESET;
   }

   Nvme_Memcpy64(&sqInfo->subq[sqInfo->tail], &cmdInfo->nvmeCmd,
      sizeof(cmdInfo->nvmeCmd)/sizeof(vmk_uint64));

#if NVME_DEBUG
   if (nvme_dbg & NVME_DEBUG_DUMP_CMD) {
      NvmeDebug_DumpCmd(&cmdInfo->nvmeCmd);
   }
#endif

#if NVME_DEBUG_INJECT_TIMEOUT
      if (qinfo->id && NvmeDebug_ErrorCounterHit(&qinfo->ctrlr->errCounters[NVME_DEBUG_ERROR_TIMEOUT])) {
         vmk_ScsiCommand *vmkCmdTmo = NvmeCore_CmdInfoToScsiCmd(cmdInfo);

         if (vmkCmdTmo) {
            IPRINT("Faking io cmd timeout in submission, "
                   "cmdInfo:%p [%d] cmdBase:%p vmkCmd:%p [%Xh] "
                   "I:%p SN:0x%lx ",
                   cmdInfo, cmdInfo->cmdId, cmdInfo->cmdBase,
                   vmkCmdTmo, vmkCmdTmo->cdb[0],
                   vmkCmdTmo->cmdId.initiator,
                   vmkCmdTmo->cmdId.serialNumber);
         } else {
            IPRINT("Faking admin cmd timeout in submission, "
                   "cmdInfo:%p [%d] cmdBase:%p",
                   cmdInfo, cmdInfo->cmdId, cmdInfo->cmdBase);
         }

         vmk_SpinlockUnlock(sqInfo->lock);
         return NVME_STATUS_SUCCESS;
      }
#endif

   tail ++;
   if (tail >= sqInfo->qsize) {
      tail = 0;
   }

   Nvme_Writel(tail, sqInfo->doorbell);
   sqInfo->tail = tail;
   sqInfo->entries --;

   vmk_SpinlockUnlock(sqInfo->lock);

   return NVME_STATUS_SUCCESS;
}


/**
 * NvmeCore_SubmitCommandWait - refer to the header file
 */
Nvme_Status
NvmeCore_SubmitCommandWait(struct NvmeQueueInfo *qinfo,
                           struct NvmeCmdInfo *cmdInfo,
                           struct cq_entry *cqEntry,
                           int timeoutUs)
{
   Nvme_Status      nvmeStatus;
   VMK_ReturnStatus vmkStatus;
   vmk_uint64       timeout;

   /**
    * Completion handler should copy completion entry to doneData
    */
   cmdInfo->doneData = cqEntry;
   nvmeStatus = NvmeCore_SubmitCommandAsync(qinfo, cmdInfo,
                                            nvmeCoreCompleteCommandWait);
   if (!SUCCEEDED(nvmeStatus)) {
      /*
       * By the time reach here, command is not submitted into hardware.
       * Do cleanup work for cmdInfo:
       * 1. Holding lock to avoid race condition with completion world,
       *    which maybe access cmdInfo.
       * 2. Setting cmdInfo->type to ABORT_CONTEXT, so that cleanup callback
       *    would cleanup uioDmaEntry.
       * 3. Put cmdInfo back to free list.
       * */
      LOCK_FUNC(qinfo);
      cmdInfo->type = ABORT_CONTEXT;
      if (cmdInfo->cleanup) {
         cmdInfo->cleanup(qinfo, cmdInfo);
      }
      NvmeCore_PutCmdInfo(qinfo, cmdInfo);
      qinfo->timeout[cmdInfo->timeoutId] --;
      UNLOCK_FUNC(qinfo);
      DPRINT_CMD("nvmeStatus = %d", nvmeStatus);
      return nvmeStatus;
   }

   /**
    * Wait for the command to be completed. the command should be waken up
    * during ISR.
    *
    * Note: Spurious wakeups are possible, so we need to check cmdInfo->status
    *       after wakeup as VMK_OK, to make sure that we have really completed
    *       the command. If not, we should go back to wait again.
    *
    * TODO: Checking cmdInfo->status here has a tiny risk when command has been
    *       completed and put into free list of command queue.
    */
   timeout = OsLib_GetTimerUs() + timeoutUs;
   do {
      vmkStatus = vmk_WorldWait((vmk_WorldEventID)cmdInfo, VMK_LOCK_INVALID,
                                 timeoutUs / 1000, __FUNCTION__);
      DPRINT_CMD("sync cmd %p [%d] on queue %d, wait:0x%x cmd:%d.",
                    cmdInfo, cmdInfo->cmdId, qinfo->id, vmkStatus,
                    cmdInfo->status);
   } while (vmkStatus == VMK_OK &&
            cmdInfo->status == NVME_CMD_STATUS_ACTIVE &&
            OsLib_TimeAfter(OsLib_GetTimerUs(), timeout));

   /**
    * Holding lock to avoid race condition with completion world,
    * which would access cmdInfo.
    */
   LOCK_FUNC(qinfo);
   if (cmdInfo->status == NVME_CMD_STATUS_DONE) {
      /**
       * By the time we reach here, we should have the command completed
       * successfully.
       */
      nvmeStatus = NVME_STATUS_SUCCESS;
   } else {
      nvmeStatus = NVME_STATUS_ABORTED;
      WPRINT("command %p failed, putting to abort queue.",
                       cmdInfo);
      cmdInfo->type = ABORT_CONTEXT;
   }
   UNLOCK_FUNC(qinfo);

   return nvmeStatus;
}


/**
 * NvmeCore_SubmitCommandPoll - refer to the header file
 */
Nvme_Status
NvmeCore_SubmitCommandPoll(struct NvmeQueueInfo *qinfo,
                           struct NvmeCmdInfo *cmdInfo,
                           struct cq_entry *cqEntry,
                           int timeoutUs)
{
   Nvme_Status nvmeStatus;
   int timeout;

   cmdInfo->status = NVME_CMD_STATUS_ACTIVE;
   cmdInfo->doneData = cqEntry;
   nvmeStatus = NvmeCore_SubmitCommandAsync(qinfo, cmdInfo,
                                            nvmeCoreCompleteCommandPoll);
   if (!SUCCEEDED(nvmeStatus)) {
      LOCK_FUNC(qinfo);
      NvmeCore_PutCmdInfo(qinfo, cmdInfo);
      UNLOCK_FUNC(qinfo);
      return nvmeStatus;
   }

   /**
    * Poll the completion status.
    */
   timeout = 0;
   while ((cmdInfo->status != NVME_CMD_STATUS_DONE) &&
          (timeout < timeoutUs)) {
      vmk_DelayUsecs(DELAY_INTERVAL);
      timeout += DELAY_INTERVAL;
   }

   if (cmdInfo->status == NVME_CMD_STATUS_DONE) {
      nvmeStatus = cmdInfo->cmdStatus;
   } else {
      nvmeStatus = NVME_STATUS_TIMEOUT;
      WPRINT("command %p failed, putting to abort queue.",
                       cmdInfo);
      cmdInfo->type = ABORT_CONTEXT;
   }

   return nvmeStatus;
}





/******************************************************************************
 * NVMe Queue Management Routines
 *****************************************************************************/

/**
 * NvmeCore_ProcessQueueCompletions - refer to header file.
 */
void
NvmeCore_ProcessQueueCompletions(struct NvmeQueueInfo *qinfo)
{
   /**
    * Call process cq twice, to make sure that all the completed commands in
    * the cq are processed, regardless the phase bit status.
    */
   nvmeCoreProcessCq(qinfo, 0);
   nvmeCoreProcessCq(qinfo, 0);
}


/**
 * NvmeCore_SuspendQueue - refer to header file.
 */
Nvme_Status
NvmeCore_SuspendQueue(struct NvmeQueueInfo *qinfo, vmk_uint32 newTimeoutId)
{
   VPRINT("qinfo %p [%d], nr_req %d, nr_act %d", qinfo, qinfo->id,
          qinfo->nrReq, qinfo->nrAct);

   if (NvmeCore_IsQueueSuspended(qinfo)) {
      /**
       * Queue has already been completed.
       */
      WPRINT("trying to suspend an inactive queue %d.", qinfo->id);
      return NVME_STATUS_BAD_PARAM;
   }

   NvmeCore_DisableQueueIntr(qinfo);

   LOCK_FUNC(qinfo);
   qinfo->timeoutId = newTimeoutId;
   DPRINT_CMD("qinfo %p, timeout[%d]= %d", qinfo, newTimeoutId,
           qinfo->timeout[newTimeoutId]);
   qinfo->flags |= QUEUE_SUSPEND;

#if 0
#if NVME_DEBUG
    if (nvme_dbg & NVME_DEBUG_DUMP_Q)
      nvme_dump_list(qinfo);
#endif
    schedule_work_on(qinfo->cpu, &qinfo->wq_time);
#endif
   UNLOCK_FUNC(qinfo);

   return NVME_STATUS_SUCCESS;

}


/**
 * NvmeCore_ResumeQueue - refer to header file
 */
Nvme_Status
NvmeCore_ResumeQueue(struct NvmeQueueInfo *qinfo)
{
   VPRINT("qinfo %p [%d], nr_req %d, nr_act %d", qinfo, qinfo->id,
          qinfo->nrReq, qinfo->nrAct);

   if (!NvmeCore_IsQueueSuspended(qinfo)) {
      /**
       * Queue is already active
       */
      WPRINT("trying to resume an active queue %d.", qinfo->id);
      return NVME_STATUS_BAD_PARAM;
   }

   LOCK_FUNC(qinfo);

   if (qinfo->flags & QUEUE_SUSPEND) {
      qinfo->flags &= ~QUEUE_SUSPEND;
#if 0
      if (qinfo->cpu_cnt > 1) {
         if ((bio = bio_list_pop(&qinfo->sq_cong))) {
            DPRINT_Q("****** bio %p", bio);
            ns = bio->bi_bdev->bd_disk->private_data;
            if (0 == nvme_submit_request(qinfo, ns, bio, 0)) {
            qinfo->unlock_func(&qinfo->lock, &qinfo->lock_flags);
            continue;
            }
            DPRINT_CMD("failed submit nr_req %d, nr_act %d queued %p",
            qinfo->nr_req, qinfo->nr_act, bio);
            bio_list_add_head(&qinfo->sq_cong, bio);
         }
      }
      schedule_work_on(qinfo->cpu, &qinfo->wq_work);
#endif
   }

   UNLOCK_FUNC(qinfo);

   NvmeCore_EnableQueueIntr(qinfo);

   return NVME_STATUS_SUCCESS;
}


/**
 * NvmeCore_IsQueueSuspended - refer to header file
 */
vmk_Bool
NvmeCore_IsQueueSuspended(struct NvmeQueueInfo *qinfo)
{
   /**
    * TODO: use atomic values for queue flags
    */
   return (qinfo->flags & QUEUE_SUSPEND) ? VMK_TRUE : VMK_FALSE;
}


/**
 * NvmeCore_ResetQueue - refer to header file
 */
Nvme_Status
NvmeCore_ResetQueue(struct NvmeQueueInfo *qinfo)
{
   struct NvmeSubQueueInfo *sqInfo = qinfo->subQueue;
#if ENABLE_REISSUE == 0
   struct NvmeCmdInfo      *cmdInfo;
   int i;
#endif

   if (!NvmeCore_IsQueueSuspended(qinfo)) {
      EPRINT("trying to reset active queue %d.", qinfo->id);
      VMK_ASSERT(0);
      return NVME_STATUS_BUSY;
   }

   IPRINT("resetting queue %d.", qinfo->id);

   LOCK_FUNC(qinfo);

   /* Reset completion queue */
   qinfo->head = 0;
   qinfo->tail = 0;
   qinfo->phase = 1;
   qinfo->timeoutId = -1;
   Nvme_Memset64(qinfo->compq, 0LL,
         (sizeof(* qinfo->compq)/sizeof(vmk_uint64)) * qinfo->qsize);

   /* Reset submission queue */
   sqInfo->head = 0;
   sqInfo->tail = 0;
   sqInfo->entries = sqInfo->qsize - 1;
   Nvme_Memset64(sqInfo->subq, 0LL,
         (sizeof(* sqInfo->subq)/sizeof(vmk_uint64)) * sqInfo->qsize);
#if ENABLE_REISSUE == 0
   /* Reset cmd list */
   vmk_ListInit(&qinfo->cmdFree);
   VMK_ASSERT(vmk_ListIsEmpty(&qinfo->cmdActive));
   VMK_ASSERT(qinfo->nrAct == 0);
   vmk_ListInit(&qinfo->cmdActive);
   cmdInfo = qinfo->cmdList;

   for (i = 0; i < qinfo->idCount; i++) {
      cmdInfo->cmdId = i + 1;        /** 0 is reserved */
      vmk_ListInsert(&cmdInfo->list, vmk_ListAtRear(&qinfo->cmdFree));
      cmdInfo++;
   }
#endif

   UNLOCK_FUNC(qinfo);

   return NVME_STATUS_SUCCESS;
}


/**
 * NvmeCore_FlushQueue - refer to header file
 */
Nvme_Status NvmeCore_FlushQueue(struct NvmeQueueInfo *qinfo, struct NvmeNsInfo* ns, vmk_int32 newId, Nvme_Status status, vmk_Bool doReissue)
{
   struct NvmeCmdInfo *cmdInfo;
   vmk_ListLinks *itemPtr, *nextPtr;

   /**
    * We can only flush queue when a queue has been suspended.
    */
   if (!NvmeCore_IsQueueSuspended(qinfo)) {
      EPRINT("trying to flush active queue %d.", qinfo->id);
      VMK_ASSERT(0);
      return NVME_STATUS_BUSY;
   }

   /**
    * First process any completed commands
    */
   NvmeCore_ProcessQueueCompletions(qinfo);

   /**
    * Then run through the list of active commands. The remaining commands have
    * been submitted to hardware, but not yet completed. Those commands should
    * be aborted.
    *
    * TODO: we can put the remaining commands to congestion queue for later
    *       re-issue.
    */
   DPRINT_CMD("qinfo %p [%d], nr_req %d, nr_act %d", qinfo, qinfo->id,
               qinfo->nrReq, qinfo->nrAct);

   VMK_LIST_FORALL_SAFE(&qinfo->cmdActive, itemPtr, nextPtr) {
      vmk_ScsiCommand *vmkCmd;
      cmdInfo = VMK_LIST_ENTRY(itemPtr, struct NvmeCmdInfo, list);

      GET_VMK_SCSI_CMD(cmdInfo->cmdPtr, vmkCmd);

      DPRINT_CMD("qinfo %p [%d], cmd_info %p, base %p [%d] vmkCmd %p", qinfo,
              qinfo->id, cmdInfo,
              cmdInfo->cmdBase, cmdInfo->cmdCount, vmkCmd);

#if ENABLE_REISSUE
      if  (doReissue) {
         /**
          * Do not return to the SCSI stack, commands that are not targeted to
          * the namespace passed as an argument.
          */
         if (ns && (cmdInfo->ns == ns)) {
            goto abort_cmd;
         }
         /**
          * Do not return to the SCSI stack, commands that timed out.
          */
         if ((newId >= 0) && (cmdInfo->timeoutId == newId)) {
            if (cmdInfo->cmdRetries <= 0) {
               goto abort_cmd;
            }
         }
         continue;
      }
abort_cmd:
#endif
      cmdInfo->cmdStatus = status;
      if (cmdInfo->done) {
         DPRINT_CMD("aborting cmd %p [%d], status %d %s.", cmdInfo, cmdInfo->cmdId, status,
                 NvmeCore_StatusToString(status));
         cmdInfo->done(qinfo, cmdInfo);
      } else {
         vmk_ScsiCommand *vmkCmd;

         GET_VMK_SCSI_CMD(cmdInfo->cmdPtr, vmkCmd);

         VPRINT("skipping cmd %p [%d] base %p vmkCmd %p, "
                "no completion handler.",
                cmdInfo, cmdInfo->cmdId, cmdInfo->cmdBase,
                vmkCmd);
         VMK_ASSERT(0);
      }
   }
#if ENABLE_REISSUE == 0
   /**
    * By far, all the active commands should have either been completed
    * successfully or been aborted.
    */
   VMK_ASSERT(qinfo->nrAct == 0);
#endif
   /*
    * At the end of this function, there will be some active commands
    * that will be reissued later when queues are re-created.
    */
   DPRINT_Q("Reissue %d commands from qid=%d", qinfo->nrAct, qinfo->id);
   return NVME_STATUS_SUCCESS;
}


/**
 * NvmeCore_CmdInfoToScsiCmd - refer to header file
 */
vmk_ScsiCommand *
NvmeCore_CmdInfoToScsiCmd(struct NvmeCmdInfo *cmdInfo)
{
   vmk_ScsiCommand *vmkCmd;

   if (cmdInfo->cmdPtr) {
      GET_VMK_SCSI_CMD(cmdInfo->cmdPtr, vmkCmd);
      return vmkCmd;
   } else if (cmdInfo->cmdBase && cmdInfo->cmdBase->cmdPtr) {
      GET_VMK_SCSI_CMD(cmdInfo->cmdBase->cmdPtr, vmkCmd);
      return vmkCmd;
   } else {
      return NULL;
   }
}


/*
 * NvmeCore_IsCtrlrRemoved - refer to header file.
 */
inline vmk_Bool
NvmeCore_IsCtrlrRemoved(struct NvmeCtrlr *ctrlr)
{
   return (Nvme_Readl(ctrlr->regs + NVME_CSTS) == 0xffffffff)
          ? VMK_TRUE : VMK_FALSE;
}

