
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
NvmeScsiCmd_SetReturnStatus(vmk_ScsiCommand *vmkCmd, Nvme_Status nvmeStatus);

/**
 * Complete SCSI command to the stack.
 *
 * @param [in]  vmkCmd vmk_ScsiCommand to be completed
 */
static inline void
NvmeScsiCmd_CompleteCommand(vmk_ScsiCommand *vmkCmd)
{
   vmk_ScsiSchedCommandCompletion(vmkCmd);
}


#endif /* _SCSI_CMDS_H */
