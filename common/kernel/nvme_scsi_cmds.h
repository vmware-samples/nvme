/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (c) 2012-2013  Integrated Device Technology, Inc.
 * All rights reserved.
 *******************************************************************************/

#ifndef _SCSI_CMDS_H
#define _SCSI_CMDS_H


/**
 * NvmeScsi_SetReturnStatus - set scsi command completion code based on NVMe
 *                            completion code.
 *
 * This function should populate the following fields in vmk_ScsiCommand after
 * return:
 *
 * - status.device
 * - status.host
 * - status.plugin
 * - senseData
 *
 * Note that bytesXferred is NOT set inside this function. The caller should
 * take the responsibility to set vmkCmd->bytesXferred.
 *
 * @param [IN]  vmkCmd     pointer to the vmk_ScsiCommand to be completed
 * @param [IN]  nvmeStatus NVMe status code to be translated
 *
 * @return      VMK_OK     if the nvme status code translates to a completion
 *                         status code;
 *              others     if the command shall not be returned back to the
 *                         storage stack. Usually this indicates that the host
 *                         fails to queue the command and return error code from
 *                         vmk_ScsiAdpater::command handler directly.
 */
VMK_ReturnStatus
NvmeScsiCmd_SetReturnStatus(void *cmdPtr, Nvme_Status nvmeStatus);

VMK_ReturnStatus scsiProcessCommand(void *clientData, void *cmdPtr, void *deviceData);

VMK_ReturnStatus
ScsiDiscover(void *clientData, vmk_ScanAction action, int channel, int targetId,
   int lunId, void **deviceData);

VMK_ReturnStatus
ScsiCheckTarget(void *clientData, int channel, int targetId);

VMK_ReturnStatus
ScsiDumpCommand(void *clientData, vmk_ScsiCommand *vmkCmd, void *deviceData);

void
ScsiDumpQueue(void *clientData);

void
ScsiDumpPollHandler(void *clientData);

VMK_ReturnStatus
ScsiIoctl(void *clientData, void *deviceData, vmk_uint32 fileFlags,
   vmk_uint32 cmd, vmk_VA userArgsPtr, vmk_IoctlCallerSize callerSize,
   vmk_int32 *drvEr);

int
ScsiQueryDeviceQueueDepth(void *clientData, void *deviceData);

VMK_ReturnStatus
ScsiProcInfo(void *clientData, char *buf, vmk_ByteCountSmall offset,
   vmk_ByteCountSmall count, vmk_ByteCountSmall *nbytes, int isWrite);

int
ScsiModifyDeviceQueueDepth(void *clientData, int qDepth, void *deviceData);

VMK_ReturnStatus
ScsiTaskMgmt(void *clientData, vmk_ScsiTaskMgmt *taskMgmt, void *deviceData);

#endif /* _SCSI_CMDS_H */
