/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (c) 2012-2013  Integrated Device Technology, Inc.
 * All rights reserved.
 *******************************************************************************/

/*
 * @file: nvme_ctrlr_mgmt.c --
 *
 *    Nvme controller related stuff
 */

#include "oslib.h"
#include "nvme_drv_config.h"
//#include "../../common/kernel/nvme_private.h"
//#include "nvme_mgmt.h"
//#include "nvme_debug.h"
//#include "nvme_os.h"


/**
 * @brief This function Validates user uio data structure.
 *     This function is called by IOCTL function to validate
 *     pass-through uio header and its content.
 *
 * @param[in] ctrlr Pointer NVME device context
 * @param[in] uio kernel address space uio header pointer
 * @param[in] usr_io flag to identify user IO request.
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
static VMK_ReturnStatus
ValidateUio(struct NvmeCtrlr *ctrlr, struct usr_io *uio, vmk_Bool usr_io)
{
   DPRINT_ADMIN("Validating uio %p.", uio);

   if (nvme_dbg & NVME_DEBUG_DUMP_UIO) {
      NvmeDebug_DumpUio(uio);
   }

   if (VMK_UNLIKELY(usr_io &&
      ((uio->cmd.header.opCode >= NVME_VNDR_CMD_IO_CODE_START) &&
         (uio->cmd.header.opCode <= NVME_VNDR_CMD_IO_CODE_END)))) {
      if (!ctrlr->nvmVendCmdCfg) {
         VPRINT("Firmware does not support Vendor Specific.");
         return VMK_NOT_SUPPORTED;
      }
      if ((uio->length < uio->cmd.cmd.vendorSpecific.buffNumDW >> 2) ||
         (uio->meta_length < uio->cmd.cmd.vendorSpecific.metaNumDW >> 2)) {
         VPRINT("length mismatch data %d, meta %d",
                uio->cmd.cmd.vendorSpecific.buffNumDW,
                uio->cmd.cmd.vendorSpecific.metaNumDW);
         return VMK_BAD_PARAM;
      }
   }

   /**
    * Validate data access.
    */
   if (uio->length) {
      if (uio->length > ctrlr->maxXferLen) {
         EPRINT("Request transfer length exceeds maximum allowed %d",
            uio->length);
         return VMK_BAD_PARAM;
      }
   }
   DPRINT_ADMIN("uio %p, addr %lx, len %d Access OK", uio, uio->addr, uio->length);

   /**
    * Validate status buffer access.
    */
   if (uio->meta_length) {
      /**
       * Return VMK_BAD_PARAM before we officially support Metadata.
       */
      return VMK_BAD_PARAM;
#if 0
      if (uio->meta_length > VMK_PAGE_SIZE) {
         EPRINT("Request meta data length exceeds maxmimum allowed %d",
            uio->meta_length);
         return VMK_BAD_PARAM;
      }
#endif
   }

   DPRINT_ADMIN("uio %p, Meta addr 0x%lx, len %d Access OK",
      uio, uio->meta_addr, uio->meta_length);

   return VMK_OK;
}


/**
 * @brief This function checks for list of disallowed user Admin requests.
 *    This function is called by IOCTL function to perform
 *    a check and validate that command opcode does not iterfear with
 *    driver operation.
 *
 * @param[in] ctrlr Pointer NVME device context
 * @param[in] uio Pointer to uio structure in kernel space.
 *
 * @return This function returns VMK_OK if allowed, otherwise Error Code
 *
 * @note: ECN-23 requires us to check for vendor unique request and to
 *    validate data length if supported.
 */
static VMK_ReturnStatus
AllowedAdminCmd(struct NvmeCtrlr *ctrlr, struct usr_io *uio)
{
   struct NvmeNsInfo *ns;
   vmk_ListLinks *itemPtr;

   switch (uio->cmd.header.opCode) {

      case NVM_ADMIN_CMD_DEL_SQ:
      case NVM_ADMIN_CMD_CREATE_SQ:
      case NVM_ADMIN_CMD_DEL_CQ:
      case NVM_ADMIN_CMD_CREATE_CQ:
      case NVM_ADMIN_CMD_ABORT:
      case NVM_ADMIN_CMD_ASYNC_EVENT_REQ:
      {
         DPRINT_ADMIN("Disallowed Admin command 0x%x.",
               uio->cmd.header.opCode);
         return VMK_NOT_SUPPORTED;
      }
      case NVM_ADMIN_CMD_FORMAT_NVM:
      {
         vmk_SpinlockLock(ctrlr->lock);
         VMK_LIST_FORALL(&ctrlr->nsList, itemPtr) {
            ns = VMK_LIST_ENTRY(itemPtr, struct NvmeNsInfo, list);
            DPRINT_ADMIN("ns id %d command nsID %d flags %x refCount %lx",
               ns->id, uio->namespaceID, ns->flags, vmk_AtomicRead64(&ns->refCount));
            if (ns->id == uio->cmd.header.namespaceID ||
               NVME_FULL_NAMESPACE == uio->cmd.header.namespaceID) {
               /* Check for namespace state and disallow format request when it is online.
                * User applications should ensure it is safe to call format command.*/
               if (NvmeCore_IsNsOnline(ns)) {
                  vmk_SpinlockUnlock(ctrlr->lock);
                  DPRINT_ADMIN("Disallowed Admin command Format 0x%x\n",
                     uio->cmd.header.opCode);
                  return VMK_BUSY;
               }
               DPRINT_ADMIN("allowing Admin command Format 0x%x flags %x\n",
                  uio->cmd.header.opCode, ns->flags);
               break;
            }
         }
         vmk_SpinlockUnlock(ctrlr->lock);
         return VMK_OK;
      }
      default:
      {
         if ((uio->cmd.header.opCode & NVME_VNDR_CMD_ADM_CODE_START) ==
            NVME_VNDR_CMD_ADM_CODE_START)
         {
            if (!ctrlr->admVendCmdCfg) {
               DPRINT_ADMIN("Vendor Specific command 0x%x\n", uio->cmd.header.opCode);
               return VMK_OK;
            }
            if ((uio->length <
               uio->cmd.cmd.vendorSpecific.buffNumDW >> 2) ||
               (uio->meta_length < uio->cmd.cmd.vendorSpecific.metaNumDW >> 2)) {
               EPRINT("Vendor Specific data length mismatch.\n");
               return VMK_BAD_PARAM;
            }
         }
         return VMK_OK;
      }
   }
}

/**
 * @brief This function process user Admin request.
 *    This function is called by IOCTL function to perform
 *    a user requested Admin command.
 *
 * @param[in] ctrlr Pointer to NVME device control block
 * @param[in] uio Pointer to uio structure in user address space.
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 *
 * @note: ECN-23 requires us to check for vendor unique request and to
 *    validate data length if supported.
 */
static VMK_ReturnStatus
AdminPassthru(struct NvmeCtrlr *ctrlr, struct usr_io *uio)
{
   VMK_ReturnStatus      vmkStatus;
   Nvme_CtrlrState       state;
   vmk_uint8            *buf = NULL;

   /**
    * Block admin commands if the controller is not in STARTED DEGRADED or OPERATIONAL
    * state.
    */
   state = NvmeState_GetCtrlrState(ctrlr, VMK_TRUE);

   if (state != NVME_CTRLR_STATE_STARTED &&
       state != NVME_CTRLR_STATE_OPERATIONAL &&
       state != NVME_CTRLR_STATE_HEALTH_DEGRADED) {
      return VMK_FAILURE;
   }
   if (ValidateUio(ctrlr, uio, VMK_FALSE)) {
      DPRINT_ADMIN("Failed validation %p.", uio);
      return VMK_FAILURE;
   }

   if ((vmkStatus = AllowedAdminCmd(ctrlr, uio))) {
      return vmkStatus;
   }

   if (uio->length) {
      buf = Nvme_Alloc(uio->length, 0, NVME_ALLOC_ZEROED);
      if (buf == NULL) {
         EPRINT("Failed to allocate buffer memory.");
         return VMK_NO_MEMORY;
      }

      if(uio->direction == XFER_TO_DEV) {
         vmkStatus = vmk_CopyFromUser((vmk_VA)buf, uio->addr, uio->length);
         if (vmkStatus) {
            DPRINT_ADMIN("Failed to copy from user buffer, 0x%x.", vmkStatus);
            goto free_buf;
         }
      }
   }

   vmkStatus = NvmeCtrlrCmd_SendAdmin(ctrlr, &uio->cmd, buf, uio->length, &uio->comp,
                                      uio->timeoutUs);
   if (vmkStatus == VMK_OK) {
      if(uio->direction == XFER_FROM_DEV && uio->length) {
         vmkStatus = vmk_CopyToUser(uio->addr, (vmk_VA)buf, uio->length);
         if (vmkStatus) {
            DPRINT_ADMIN("Failed to copy from user buffer, 0x%x.", vmkStatus);
            goto free_buf;
         }
      }
   }

   uio->status = vmkStatus;

free_buf:
   if (buf != NULL) {
      Nvme_Free(buf);
   }
   return vmkStatus;
}


/**
 * Dump registers
 *
 * @param [in] ctrlr controller instance
 * @param [in] uio pointer to the uio structure
 *
 * @return VMK_OK if completes successfully; otherwise return error code
 */
static VMK_ReturnStatus
DumpRegs(struct NvmeCtrlr *ctrlr, struct usr_io *uio)
{
   int length;
   Nvme_CtrlrState state;

   length = min_t(int, ctrlr->barSize, uio->length);
   uio->meta_length = length;

   state = NvmeState_GetCtrlrState(ctrlr, VMK_TRUE);
   if (state != NVME_CTRLR_STATE_OPERATIONAL) {
      DPRINT_ADMIN("Receive registers dump request while controller is in %s state.",
                   NvmeState_GetCtrlrStateString(state));
      return VMK_NOT_READY;
   }

   return vmk_CopyToUser(uio->addr, ctrlr->regs, length);
}

/**
 * Dump statistics data
 *
 * @param [in] ctrlr controller instance
 * @param [in] uio pointer to the uio structure
 *
 * @return VMK_OK if completes successfully; otherwise return error code
 */
static VMK_ReturnStatus
DumpStatsData(struct NvmeCtrlr *ctrlr, struct usr_io *uio)
{
#if (NVME_ENABLE_STATISTICS == 1)
   int length;

   length = min_t(int, sizeof(STATS_StatisticData), uio->length);
   uio->meta_length = length;

   return vmk_CopyToUser(uio->addr, (vmk_VA)&ctrlr->statsData, length);
#else
   IPRINT("Statistic data collection is disabled");
   return VMK_OK;
#endif
}

static VMK_ReturnStatus
nvmeMgmtSetCtrlrOnline(struct NvmeCtrlr *ctrlr, struct usr_io *uio,
                       vmk_Bool isOnline)
{
   Nvme_Status nvmeStatus;
  
   if (uio->namespaceID == 0)
      nvmeStatus = NvmeCore_SetCtrlrOnline(ctrlr, isOnline);
   else
      nvmeStatus = NvmeCore_SetNamespaceOnline(ctrlr, isOnline, uio->namespaceID);
   uio->status = nvmeStatus;

   if (SUCCEEDED(nvmeStatus)) {
      return VMK_OK;
   } else {
      return VMK_FAILURE;
   }
}

static VMK_ReturnStatus
nvmeMgmtGetNsStatus(struct NvmeCtrlr *ctrlr, struct usr_io *uio)
{
   vmk_ListLinks     *itemPtr, *nextPtr;
   struct NvmeNsInfo *ns = NULL;
   vmk_Bool           nsStatus = 0;

   vmk_SpinlockLock(ctrlr->lock);
   VMK_LIST_FORALL_SAFE(&ctrlr->nsList, itemPtr, nextPtr) {
      ns = VMK_LIST_ENTRY(itemPtr, struct NvmeNsInfo, list);
      if (ns->id == uio->namespaceID) {
         nsStatus = NvmeCore_IsNsOnline(ns);
         break;
      }
   }
   vmk_SpinlockUnlock(ctrlr->lock);

   /* Return Status of namespace via uio->status: Online: VMK_OK; Offline: VMK_FAILURE */
   if (nsStatus) {
      return VMK_OK;
   } else {
      return VMK_FAILURE;
   }

}

static VMK_ReturnStatus
nvmeMgmtUpdateNs(struct NvmeCtrlr *ctrlr, struct usr_io *uio)
{
   VMK_ReturnStatus vmkStatus = VMK_NOT_SUPPORTED;
   struct iden_namespace *ident;
   vmk_ListLinks     *itemPtr, *nextPtr;
   struct NvmeNsInfo *ns;
   vmk_uint32 lba_format;

   ident = Nvme_Alloc(sizeof(*ident), 0, NVME_ALLOC_ZEROED);
   if (ident == NULL) {
      EPRINT("Failed to allocate namespace %d identify data.", uio->namespaceID);
      return VMK_NO_MEMORY;
   }

   vmkStatus = NvmeCtrlrCmd_Identify(ctrlr, uio->namespaceID, (vmk_uint8 *)ident);
   if (vmkStatus != VMK_OK) {
      EPRINT("Failed to get identify namespace %d.", uio->namespaceID);
      goto free_ident;
   }

   vmk_SpinlockLock(ctrlr->lock);
   VMK_LIST_FORALL_SAFE(&ctrlr->nsList, itemPtr, nextPtr) {
      ns = VMK_LIST_ENTRY(itemPtr, struct NvmeNsInfo, list);
      /* Keep the validation criteria consistent with format command since
       * this function is always called after completing format command.*/
      if (ns->id == uio->namespaceID && !NvmeCore_IsNsOnline(ns)) {
         vmk_SpinlockLock(ns->lock);
         lba_format      = *(vmk_uint32 *)&ident->lbaFmtSup[ident->fmtLbaSize & 0x0F];
         ns->blockCount  = ident->size;
         ns->lbaShift    = (lba_format >> 16) & 0x0F;
         ns->feature     = ident->feat;
         ns->metaDataCap = ident->metaDataCap;
         ns->metasize    = lba_format & 0x0FFFF;
         ns->fmtLbaSize  = ident->fmtLbaSize;
         ns->dataProtCap = ident->dataProtCap;
         ns->dataProtSet = ident->dataProtSet;
         ns->eui64       = ident->eui64;
         vmk_SpinlockUnlock(ns->lock);
         vmkStatus = VMK_OK;
         break;
      }
   }
   vmk_SpinlockUnlock(ctrlr->lock);

free_ident:
   Nvme_Free(ident);
   return vmkStatus;
}

static VMK_ReturnStatus
nvmeMgmtGetIntVectNum(struct NvmeCtrlr *ctrlr, struct usr_io *uio)
{
   /* Use uio->length to return numVectors. */
   uio->length = ctrlr->ctrlOsResources.numVectors;
   return VMK_OK;
}

static VMK_ReturnStatus
nvmeMgmtSetTimeout(struct NvmeCtrlr *ctrlr, struct usr_io *uio)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;

#if USE_TIMER
   int newVal, oldVal;
   newVal = uio->length;
   if (newVal < 0 || newVal > NVME_IO_TIMEOUT) {
      vmkStatus = VMK_BAD_PARAM;
      uio->status = vmkStatus;
      return vmkStatus;
   }

   vmk_SpinlockLock(ctrlr->lock);
   oldVal = ctrlr->ioTimeout;
   ctrlr->ioTimeout = newVal;
   if (oldVal == 0 && newVal > 0) {
      NvmeExc_SignalException(ctrlr, NVME_EXCEPTION_TASK_TIMER);
   }
   vmk_SpinlockUnlock(ctrlr->lock);
#else
   DPRINT_MGMT("Timeout checker is disabled.");
   vmkStatus = VMK_NOT_SUPPORTED;
#endif

   uio->status = vmkStatus;
   return vmkStatus;
}

static VMK_ReturnStatus
nvmeMgmtGetTimeout(struct NvmeCtrlr *ctrlr, struct usr_io *uio)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;
#if USE_TIMER
   uio->length = ctrlr->ioTimeout;
#else
   DPRINT_MGMT("Timeout checker is disabled.");
   vmkStatus = VMK_NOT_SUPPORTED;
#endif
   uio->status = vmkStatus;
   return vmkStatus;
}

/**
 * Process ioctl commands
 *
 * @param [in] ctrlr controller instance
 * @param [in] cmd ioctl command
 * @param [in] uio pointer to pass-through command
 *
 * @return VMK_OK if completes successfully; otherwise return error code
 */
VMK_ReturnStatus
NvmeCtrlr_IoctlCommon(struct NvmeCtrlr *ctrlr, vmk_uint32 cmd,
                      struct usr_io *uio)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;

   switch(cmd) {
      case NVME_IOCTL_ADMIN_CMD:
         vmkStatus = AdminPassthru(ctrlr, uio);
         break;
      case NVME_IOCTL_IO_CMD:
         vmkStatus = VMK_NOT_SUPPORTED;
         break;
      case NVME_IOCTL_RESTART:
         vmkStatus = VMK_NOT_SUPPORTED;
         break;
      case NVME_IOCTL_HOTREMOVE:
         vmkStatus = VMK_NOT_SUPPORTED;
         break;
      case NVME_IOCTL_HOTADD:
         vmkStatus = VMK_NOT_SUPPORTED;
         break;
      case NVME_IOCTL_EVENT:
         vmkStatus = VMK_NOT_SUPPORTED;
         break;
      case NVME_IOCTL_SET_CACHE:
         vmkStatus = VMK_NOT_SUPPORTED;
         break;
      case NVME_IOCTL_DUMP_REGS:
         vmkStatus = DumpRegs(ctrlr, uio);
         break;
      case NVME_IOCTL_DUMP_STATS_DATA:
         vmkStatus = DumpStatsData(ctrlr, uio);
         break;
      case NVME_IOCTL_SET_CTRLR_ONLINE:
         vmkStatus = nvmeMgmtSetCtrlrOnline(ctrlr, uio, VMK_TRUE);
         break;
      case NVME_IOCTL_SET_CTRLR_OFFLINE:
         vmkStatus = nvmeMgmtSetCtrlrOnline(ctrlr, uio, VMK_FALSE);
         break;
      case NVME_IOCTL_GET_NS_STATUS:
         vmkStatus = nvmeMgmtGetNsStatus(ctrlr, uio);
         break;
      case NVME_IOCTL_SET_NS_ONLINE:
         vmkStatus = nvmeMgmtSetCtrlrOnline(ctrlr, uio, VMK_TRUE);
         break;
      case NVME_IOCTL_SET_NS_OFFLINE:
         vmkStatus = nvmeMgmtSetCtrlrOnline(ctrlr, uio, VMK_FALSE);
         break;
      case NVME_IOCTL_UPDATE_NS:
         vmkStatus = nvmeMgmtUpdateNs(ctrlr, uio);
         break;
      case NVME_IOCTL_GET_INT_VECT_NUM:
         vmkStatus = nvmeMgmtGetIntVectNum(ctrlr, uio);
         break;
      case NVME_IOCTL_SET_TIMEOUT:
         vmkStatus = nvmeMgmtSetTimeout(ctrlr, uio);
         break;
      case NVME_IOCTL_GET_TIMEOUT:
         vmkStatus = nvmeMgmtGetTimeout(ctrlr, uio);
         break;
      default:
         EPRINT("unknown ioctl command %d.", cmd);
         vmkStatus = VMK_BAD_PARAM;
         break;
   }

   if (!uio->status) {
      uio->status = vmkStatus;
   }
   return vmkStatus;
}
