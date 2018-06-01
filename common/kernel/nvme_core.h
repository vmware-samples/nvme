/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (c) 2012-2013  Integrated Device Technology, Inc.
 * All rights reserved.
 *******************************************************************************/

#ifndef _NVME_CORE_H_
#define _NVME_CORE_H_

#include "vmkapi.h"
#include "../../common/kernel/nvme.h"
#include "../../common/kernel/oslib_common.h"


/******************************************************************************
 * BEGIN OF PARAMS
 *****************************************************************************/

#define DELAY_INTERVAL 10

/******************************************************************************
 * END OF PARAMS
 *****************************************************************************/


/**
 * Nvme_Status - status code for operations on the NVMe core
 */
typedef enum Nvme_Status {
   /** No error */
   NVME_STATUS_SUCCESS,
   /** Error: device removed. */
   NVME_STATUS_DEVICE_MISSING,
   /** Error: device not ready. */
   NVME_STATUS_NOT_READY,
   /** Error: device is going through reset */
   NVME_STATUS_IN_RESET,
   /** Error: device is shut down. */
   NVME_STATUS_QUIESCED,
   /** Error: device has encountered a fatal error and cannot recover. */
   NVME_STATUS_FATAL_ERROR,
   /** Error: medium error */
   NVME_STATUS_MEDIUM_ERROR,
   /** Error: queue full */
   NVME_STATUS_QFULL,
   /** Error: device is busy */
   NVME_STATUS_BUSY,
   /** NVM Error: invalid operation code */
   NVME_STATUS_INVALID_OPCODE,
   /** NVM Error: invalid field in the command */
   NVME_STATUS_INVALID_FIELD_IN_CDB,
   /** NVM Error: invalid namespace or format */
   NVME_STATUS_INVALID_NS_OR_FORMAT,
   /** NVM Error: namespace not ready */
   NVME_STATUS_NS_NOT_READY,
   /** Error: namespace is offline */
   NVME_STATUS_NS_OFFLINE,
   /** NVM Error: I/O error */
   NVME_STATUS_IO_ERROR,
   /** NVM Error: I/O write error */
   NVME_STATUS_IO_WRITE_ERROR,
   /** NVM Error: I/O read error */
   NVME_STATUS_IO_READ_ERROR,
   /** NVM Error: command aborted */
   NVME_STATUS_ABORTED,
   /** Error: command timed out */
   NVME_STATUS_TIMEOUT,
   /** NVM Error: command reset */
   NVME_STATUS_RESET,
   /** Command will be completed asynchronously */
   NVME_STATUS_WOULD_BLOCK,
   /** Error: underrun condition */
   NVME_STATUS_UNDERRUN,
   /** Error: overrun condition */
   NVME_STATUS_OVERRUN,
   /** Error: LBA out of range */
   NVME_STATUS_LBA_OUT_OF_RANGE,
   /** Error: Capacity exceeded */
   NVME_STATUS_CAPACITY_EXCEEDED,
   /** NVM Error: Conflict attributes */
   NVME_STATUS_CONFLICT_ATTRIBUTES,
   /** NVM Error: Invalid protection information */
   NVME_STATUS_INVALID_PI,
   /** Error: nvme protocol error */
   NVME_STATUS_PROTOCOL_ERROR,
   /** Error: bad parameter */
   NVME_STATUS_BAD_PARAM,
   /** Error: general failure */
   NVME_STATUS_FAILURE,
   /* Error: read-only */
   NVME_STATUS_WRITE_PROTECT,
   /* Error: over temp. */
   NVME_STATUS_OVERTEMP,
   /* Error: guard check failure */
   NVME_STATUS_GUARD_CHECK_ERROR,
   /* Error: application tag check failure */
   NVME_STATUS_APP_CHECK_ERROR,
   /* Error: reference tag check failure */
   NVME_STATUS_REF_CHECK_ERROR,
   /* Error: parameter list length error */
   NVME_STATUS_PARAM_LIST_LENGTH_ERROR,
   /** Guard */
   NVME_STATUS_LAST,
} Nvme_Status;


#define SUCCEEDED(nvmeStatus)  ((nvmeStatus) == 0)
#ifndef FAILURE
#define FAILURE(nvmeStatus)  (!SUCCEEDED((nvmeStatus)))
#endif


/**
 * NvmeCore_StatusToString - translate Nvme_Status to string.
 */
const char *
NvmeCore_StatusToString(Nvme_Status nvmeStatus);


/**
 * The command has been deferred to error handling thread and will be returned
 * by error handling thread.
 */
#define DELAYED_RETURN(nvmeStatus)  (((nvmeStatus) == NVME_STATUS_TIMEOUT) || \
                                     ((nvmeStatus) == NVME_STATUS_ABORTED))

typedef enum Nvme_CmdStatus {
   NVME_CMD_STATUS_FREE,
   NVME_CMD_STATUS_ACTIVE,
   NVME_CMD_STATUS_DONE,
   NVME_CMD_STATUS_FREE_ON_COMPLETE,
} Nvme_CmdStatus;


/**
 * Some forward declares, should be removed.
 */
struct NvmeCtrlr;
struct NvmeQueueInfo;
struct NvmeSubQueueInfo;
struct NvmeCmdInfo;
struct NvmeNsInfo;


/**
 * NvmeCore_CompleteCommandCb - callback to be invoked when a NVM command is
 *                              completed by hardware.
 */
typedef void (*NvmeCore_CompleteCommandCb)(struct NvmeQueueInfo *qinfo,
                                           struct NvmeCmdInfo *cmdInfo);


/**
 * NvmeCore_CleanupCommandCb - callback to cleanup resources associated with
 *                             a command.
 */
typedef void (*NvmeCore_CleanupCommandCb)(struct NvmeQueueInfo *qinfo,
                                          struct NvmeCmdInfo *cmdInfo);


/**
 * NvmeCore_SubmitCommandAsync - submit an NVM command to the controller, and
 *                               return without waiting for completion.
 *
 * @param [IN]  qinfo    queue to submit the command (completion queue)
 * @param [IN]  cmdInfo  command to be submitted
 * @param [IN]  cb       callback function to be invoked at completion
 *
 * @return  NVME_STATUS_SUCCESS  if completed successfully
 */
Nvme_Status
NvmeCore_SubmitCommandAsync(struct NvmeQueueInfo *qinfo,
                            struct NvmeCmdInfo *cmdInfo,
                            NvmeCore_CompleteCommandCb cb);

/**
 * NvmeCore_SubmitCommandWait - submit an NVM command to the controller, and
 *                              sleep wait for its completion.
 *
 * @param [IN]  qinfo      queue to submit the command (completion queue)
 * @param [IN]  cmdInfo    command to be submitted
 * @param [IN]  timeoutUs  time (in microseconds) to timeout.
 *
 * @return  NVME_STATUS_SUCCESS  if completed successfully
 *          NVME_STATUS_TIMEOUT  if timed out
 *          NVME_STATUS_ABORTED  if command is aborted.
 *          others               if the command is completed with failures.
 *
 * @note    if return code is NVME_STATUS_TIMEOUT or NVME_STATUS_ABORTED, then
 *          the caller should NOT free cmdInfo, because the command may still
 *          be outstanding in the hardware and may be returned later. The caller
 *          should let the core to free the command during error recovery.
 */
Nvme_Status
NvmeCore_SubmitCommandWait(struct NvmeQueueInfo *qinfo,
                           struct NvmeCmdInfo *cmdInfo,
                           struct cq_entry *cqEntry,
                           int timeoutUs);

/**
 * This is temporary.
 */
void
nvmeCoreProcessCq(struct NvmeQueueInfo *qinfo, int isDumpHandler);


/**
 * NvmeCore_PutCmdInfo - Return a command info to a queue.
 *
 * @param [IN]  qinfo      pointer to a completion queue
 * @param [IN]  cmdInfo    pointer to the command info to be returned.
 *
 * @note It is assumed that queue lock is held by caller.
 */
void
NvmeCore_PutCmdInfo(struct NvmeQueueInfo *qinfo, struct NvmeCmdInfo *cmdInfo);


/**
 * NvmeCore_GetCmdInfo - Get a command info from a queue.
 *
 * @param [IN]  qinfo      pointer to a completion queue
 *
 * @return  pointer to the command info allocated, or NULL if queue is full.
 *
 * @note It is assumed that queue lock is held by caller.
 */
struct NvmeCmdInfo *
NvmeCore_GetCmdInfo(struct NvmeQueueInfo *qinfo);


/**
 * NvmeCore_GetStatus - Generate a status code from the NVME completion queue
 *                      entry.
 *
 * @param [IN]  cqEntry    pointer to a completion queue entry.
 *
 * @return  status code
 */
Nvme_Status
NvmeCore_GetStatus(struct cq_entry *cqEntry);


vmk_Bool
NvmeCore_IsNsOnline(struct NvmeNsInfo *ns);


/**
 * NvmeCore_SetNsOnline - set online/offline status on a namespace.
 *
 * @param [IN]  ns         pointer to the namespace info
 * @param [IN]  isOnline   VMK_TRUE for online a namespace; VMK_FALSE for
 *                         offline a namespace.
 */
Nvme_Status
NvmeCore_SetNsOnline(struct NvmeNsInfo *ns, vmk_Bool isOnline);

/**
 * Validate whether a namespace is compatible with vSphere
 *
 * @param [in] ns pointer to the namespace info
 *
 * @return VMK_OK if namespace is supported, error code if namespace is not supported.
 */
VMK_ReturnStatus
NvmeCore_ValidateNs(struct NvmeNsInfo *ns);

/**
 * NvmeCore_SetCtrlrOnline - set online/offline status on all namespaces of a
 *                           controller.
 *
 * @param [IN]  ctrlr      pointer to the controller
 * @param [IN]  isOnline   VMK_TRUE for online all namespaces; VMK_FALSE for
 *                         offline all namespaces.
 *
 * @note  this function will also try a SCSI paths status update after namespace
 *        status is updated.
 */
Nvme_Status
NvmeCore_SetCtrlrOnline(struct NvmeCtrlr *ctrlr, vmk_Bool isOnline);

/**
 * NvmeCore_SetNamespaceOnline - set online/offline status on specific namespace of a
 *                               controller.
 *
 * @param [IN]  ctrlr      pointer to the controller
 * @param [IN]  isOnline   VMK_TRUE for online namespace; VMK_FALSE for
 *                         offline namespace.
 * @param [IN]  nsid       namespace ID
 *
 * @note  this function will also try a SCSI paths status update after namespace
 *        status is updated.
 */
Nvme_Status
NvmeCore_SetNamespaceOnline(struct NvmeCtrlr *ctrlr, vmk_Bool isOnline, int nsid);

VMK_ReturnStatus NvmeQueue_RequestIrq(struct NvmeQueueInfo* qInfo);
VMK_ReturnStatus NvmeQueue_FreeIrq(struct NvmeQueueInfo* qInfo);
/**
 * NvmeCore_DisableQueueIntr - disable interrupt on a queue.
 *
 * This is only valid when using MSIX interrupts.
 *
 * @param [IN]  qinfo      pointer to the completion queue
 *
 * @return NVME_STATUS_SUCCESS
 *
 * @note   must NOT be called with spin lock held.
 */
Nvme_Status
NvmeCore_DisableQueueIntr(struct NvmeQueueInfo *qinfo);


/**
 * NvmeCore_EnableQueueIntr - enable interrupt on a queue.
 *
 * This is only valid when using MSIX interrupts.
 *
 * @param [IN]  qinfo      pointer to the completion queue
 *
 * @return NVME_STATUS_SUCCESS
 *
 * @note   must NOT be called with spin lock held.
 */
Nvme_Status
NvmeCore_EnableQueueIntr(struct NvmeQueueInfo *qinfo);


/**
 * NvmeCore_ProcessCompletions - process completed commands on a completion
 *                               queue.
 *
 * @param [IN]  qinfo      pointer to the completion queue
 *
 * @note   must be called with qinfo->lock held.
 */
void
NvmeCore_ProcessQueueCompletions(struct NvmeQueueInfo *qinfo);


/**
 * NvmeCore_IsQueueSuspended - check if a queue has been suspended.
 *
 * @param [IN]  qinfo      pointer to the completion queue
 *
 * @return  VMK_TRUE       if queue is suspended
 *          VMK_FALSE      if queue is active
 */
vmk_Bool
NvmeCore_IsQueueSuspended(struct NvmeQueueInfo *qinfo);


/**
 * NvmeCore_SuspendQueue - suspend a queue
 *
 * @param [IN]  qinfo      pointer to the completion queue
 *
 * @return  NVME_STATUS_SUCCESS    if successful
 * @return  NVME_STATUS_BAD_PARAM  if queue has already been suspended
 */
Nvme_Status
NvmeCore_SuspendQueue(struct NvmeQueueInfo *qinfo);


/**
 * NvmeCore_ResumeQueue - resume q queue
 *
 * @param [IN]  qinfo      pointer to the completion queue
 *
 * @return  NVME_STATUS_SUCCESS    if successful
 * @return  NVME_STATUS_BAD_PARAM  if queue is active
 */
Nvme_Status
NvmeCore_ResumeQueue(struct NvmeQueueInfo *qinfo);


/**
 * NvmeCore_ResetQueue - reset q queue to its initial state
 *
 * @param [IN]  qinfo      pointer to the IO completion queue
 *
 * @return  NVME_STATUS_SUCCESS    if suscessful
 *
 */
Nvme_Status
NvmeCore_ResetQueue(struct NvmeQueueInfo *qinfo);

/**
 * NvmeCore_FlushQueue - flush all oustanding commands on a queue
 *
 * This function is called during error recovery to both process completions
 * of completed commands, and abort/reset oustanding commands in the hardware.
 *
 * @param [IN]  qinfo      pointer to the completion queue
 * @param [IN]  ns         pointer to namespace information 
 * @param [IN]  newId      timeslot identifier of commands that timed out. 
 * @param [IN]  status     status code to be set for not-completed oustanding
 *                         commands
 * @param [IN] doReissue   inidcates if we want to retry commands that timed out.
 *
 * @return  NVME_STATUS_SUCCESS
 *
 * @note   must be called with qinfo->lock held.
 * @note   The argument ns specifies to only flush requests with matching ns when retries are requested. 
 */
Nvme_Status
NvmeCore_FlushQueue(struct NvmeQueueInfo *qinfo, struct NvmeNsInfo* ns, vmk_int32 newId, Nvme_Status status, vmk_Bool doReissue);


/**
 * NvmeCore_CmdInfoToScsiCmd - get SCSI command instance from NvmeCmdInfo
 *
 * @param [IN]  cmdInfo    NvmeCmdInfo instance
 *
 * @return   vmk_ScsiCommand*   if the cmd instance has a valid SCSI
 *                              command attached
 *           NULL               if the cmd instance does not have a valid SCSI
 *                              command attached, mostly an internal command or
 *                              admin command.
 */
vmk_ScsiCommand *
NvmeCore_CmdInfoToScsiCmd(struct NvmeCmdInfo *cmdInfo);


/*
 * NvmeCore_IsCtrlrRemoved - whether the ctrlr is hot removed.
 */
inline vmk_Bool
NvmeCore_IsCtrlrRemoved(struct NvmeCtrlr *ctrlr);

#if ENABLE_REISSUE
Nvme_Status NvmeCore_ReissueCommand(struct NvmeQueueInfo *qinfo, struct NvmeCmdInfo *cmdInfo);
#endif


#endif /* _NVME_CORE_H_ */
