/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (c) 2012-2013  Integrated Device Technology, Inc.
 * All rights reserved.
 *******************************************************************************/

#ifndef _OSLIB_H_
#define _OSLIB_H_

#include <vmkapi.h>

#include "../common/kernel/nvme_drv_config.h"
#include "../../common/kernel/nvme.h"
#include "../../common/kernel/oslib_common.h"
#include "../../common/kernel/nvme_core.h"
#include "../../common/kernel/nvme_os_common.h"
#include "nvme_os.h"
#include "../common/kernel/nvme_private.h"
#include "../common/kernel/nvme_debug.h"
#include "../common/kernel/nvme_exc.h"
#include "nvme_mgmt.h"

/**
 * Log message with level.
 */
#define Nvme_Log(level, fmt, args...)        \
   do {                                      \
      vmk_LogLevel(VMK_LOG_URGENCY_NORMAL,   \
         NVME_DRIVER_RES_LOG_HANDLE,         \
         level,                              \
         NVME_LOG_PREFIX fmt "\n",           \
         __FUNCTION__,                       \
         __LINE__,                           \
         ##args);                            \
   } while (0)


/**
 * Log debug messages
 */
#define Nvme_LogDebug(fmt, args...) \
   Nvme_Log(NVME_LOG_LEVEL_DEBUG, fmt, ##args)


/**
 * Log verbose messages
 */
#define Nvme_LogVerb(fmt, args...) \
   Nvme_Log(NVME_LOG_LEVEL_VERBOSE, fmt, ##args)


/**
 * Log information messages
 */
#define Nvme_LogInfo(fmt, args...) \
   Nvme_Log(NVME_LOG_LEVEL_INFO, fmt, ##args)


/**
 * Log warning messages
 */
#define Nvme_LogWarning(fmt, args...) \
   Nvme_Log(NVME_LOG_LEVEL_WARNING, fmt, ##args)


/**
 * Log error messages
 */
#define Nvme_LogError(fmt, args...) \
   Nvme_Log(NVME_LOG_LEVEL_ERROR, fmt, ##args)



VMK_ReturnStatus
OsLib_LockCreateNoRank(const char *name, vmk_Lock *lock);

VMK_ReturnStatus
OsLib_LockCreate(struct NvmeCtrlOsResources *, vmk_LockRank rank,
                 const char *name, vmk_Lock *lock);

VMK_ReturnStatus
OsLib_LockDestroy(vmk_Lock *lock);

VMK_ReturnStatus
OsLib_SemaphoreCreate(const char *name, int value, vmk_Semaphore *sema);

VMK_ReturnStatus
OsLib_SemaphoreDestroy(vmk_Semaphore *sema);

VMK_ReturnStatus
OsLib_DmaAlloc(struct NvmeCtrlOsResources *ctrlr, vmk_ByteCount size,
               struct NvmeDmaEntry *dmaEntry, vmk_uint32 timeout);

VMK_ReturnStatus
OsLib_DmaFree(struct NvmeCtrlOsResources *ctrlr, struct NvmeDmaEntry *dmaEntry);

VMK_ReturnStatus
OsLib_IntrRegister(vmk_Device device, vmk_IntrCookie intrCookie,
                   void *handlerData, int idx,
                   vmk_IntrAcknowledge intrAck, vmk_IntrHandler intrHandler);

VMK_ReturnStatus
OsLib_IntrUnregister(vmk_IntrCookie intrCookie, void *handlerData);

vmk_uint32 OsLib_GetMaxNumQueues(void);
vmk_uint32 OsLib_GetQueue(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd);
vmk_uint32 Oslib_GetPCPUNum(void);

void
OsLib_StrToUpper(char *str, int length);


#define OsLib_CopyToUser vmk_CopyToUser


/**
 * Get microseconds since system boot
 */
static inline vmk_uint64
OsLib_GetTimerUs()
{
   return vmk_TimerUnsignedTCToUS(vmk_GetTimerCycles());
}


/**
 * If t1 > t0, returns difference between; otherwise returns 0.
 */
static inline vmk_uint64
OsLib_TimeAfter(vmk_uint64 t0, vmk_uint64 t1)
{
   if (t1 > t0) {
      return (t1 - t0);
   }

   return 0;
}

VMK_ReturnStatus OsLib_DmaInit(struct NvmeCtrlOsResources *ctrlr);
VMK_ReturnStatus OsLib_DmaCleanup(struct NvmeCtrlOsResources *ctrlr);
VMK_ReturnStatus OsLib_LockDomainCreate(struct NvmeCtrlOsResources *ctrlOsResources, const char* ctrlName);
VMK_ReturnStatus
OsLib_LockDomainDestroy(struct NvmeCtrlOsResources *ctrlOsResources);

void OsLib_StrToUpper(char *str, int length);

VMK_ReturnStatus OsLib_SetPathLostByDevice(struct NvmeCtrlOsResources *ctrlOsResources);

/* SCSI layer requirements */

VMK_ReturnStatus ScsiCommand(void *clientData, vmk_ScsiCommand *vmkCmd, void *deviceData);

void
ScsiNotifyIOAllowed(vmk_Device logicalDevice, vmk_Bool ioAllowed);

#if NVME_MUL_COMPL_WORLD
VMK_ReturnStatus OsLib_StartCompletionWorlds(struct NvmeCtrlr *ctrlr);
VMK_ReturnStatus OsLib_EndCompletionWorlds(struct NvmeCtrlr *ctrlr);
void OsLib_IOCompletionEnQueue(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd);
#endif

VMK_ReturnStatus
NvmeScsi_Destroy(struct NvmeCtrlr *ctrlr);

VMK_ReturnStatus
NvmeScsi_Init(struct NvmeCtrlr *ctrlr);

/*
 * Exception handler task management
 */
void OsLib_ShutdownExceptionHandler(struct NvmeCtrlr *ctrlr);
VMK_ReturnStatus OsLib_SetupExceptionHandler(struct NvmeCtrlr *ctrlr);

#if USE_TIMER
VMK_ReturnStatus OsLib_TimerQueueCreate(struct NvmeCtrlr *ctrlr);
VMK_ReturnStatus OsLib_TimerQueueDestroy(struct NvmeCtrlr *ctrlr);
void OsLib_StartIoTimeoutCheckTimer(struct NvmeCtrlr *ctrlr);
void OsLib_StopIoTimeoutCheckTimer(struct NvmeCtrlr *ctrlr);
#endif

#define SCSI_CMD_INVOKE_COMPLETION_CB(scsiCmd) vmk_ScsiSchedCommandCompletion((vmk_ScsiCommand *)scsiCmd)

#define GET_VMK_SCSI_CMD(cmdPtr, vmkCmd) (vmkCmd = (vmk_ScsiCommand *)cmdPtr)

#define ScsiCmdSetSenseData(senseData, vmkcmd, size) vmk_ScsiCmdSetSenseData(senseData, vmkcmd, size);

#define SET_SCSI_SENSE_LEGACY(senseData, cmdPtr, size)

#endif /* _OSLIB_H_ */


