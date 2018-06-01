/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (c) 2012-2013  Integrated Device Technology, Inc.
 * All rights reserved.
 *******************************************************************************/

/*
 * @file: nvme_mgmt_kernel.c --
 *
 *    Driver management interface of native nvme driver, kernel specific
 *
 *    There are two management components in the driver. The global management
 *    handle is registered at module load time, which provides a list of
 *    available NVMe controllers to a client; The per-controller management
 *    handle is registered during controller attachment, which provides
 *    controller specific management callbacks.
 */

#include "oslib.h"


#include "../../common/kernel/nvme_private.h"

/**
 * Register the global management handle
 *
 * This management interface is used to provide the module/driver wide
 * information to the clients, including a list of available NVMe controllers,
 * driver-wide parameters, etc.
 *
 * This is called at module load time.
 */
VMK_ReturnStatus
NvmeMgmt_GlobalInitialize()
{
#if VMKAPIDDK_VERSION >= 600
   vmk_MgmtProps mgmtProps;
#endif
   VPRINT("Initializing global management handle.");

#if VMKAPIDDK_VERSION >= 600
   mgmtProps.modId = vmk_ModuleCurrentID;
   mgmtProps.heapId = NVME_DRIVER_RES_HEAP_ID;
   mgmtProps.sig = &globalSignature;
   mgmtProps.cleanupFn = NULL;
   mgmtProps.sessionAnnounceFn = NULL;
   mgmtProps.sessionCleanupFn = NULL;
   mgmtProps.handleCookie = 0L;

   return vmk_MgmtInit(&mgmtProps,
      &NVME_DRIVER_RES_MGMT_HANDLE);
#else
   return vmk_MgmtInit(vmk_ModuleCurrentID,
      NVME_DRIVER_RES_HEAP_ID,
      &globalSignature,
      NULL,
      0L,
      &NVME_DRIVER_RES_MGMT_HANDLE);
#endif

}


/**
 * Destroy the global management handle.
 */
VMK_ReturnStatus
NvmeMgmt_GlobalDestroy()
{
   VPRINT("Destroying management handle.");

   return vmk_MgmtDestroy(NVME_DRIVER_RES_MGMT_HANDLE);
}


/**
 * Management callback for retriving adapter list
 *
 * @param [in] cookies not used
 * @param [in] envelope not used
 * @param [out] numAdapters number of adapters available in the system
 * @param [out] adapterInfo array of adapter information of size NVME_MAX_ADAPTERS
 *
 * @return VMK_OK if successful.
 */
#if VMKAPIDDK_VERSION >= 600
VMK_ReturnStatus NvmeMgmt_ListAdapters(vmk_MgmtCookies *cookies,
   vmk_MgmtEnvelope *envelope,
#else
VMK_ReturnStatus NvmeMgmt_ListAdapters(vmk_uint64 cookie,
   vmk_uint64 instanceId,
#endif
   vmk_uint32 *numAdapters,
   struct nvmeAdapterInfo *adapterInfo)
{
   int count;
   vmk_ListLinks *itemPtr, *item;
   struct NvmeNsInfo *ns;
   struct NvmeCtrlr *ctrlr;
   const char *vmhbaName;

   DPRINT_TEMP("enter.");

   count = 0;
   vmk_SpinlockLock(NVME_DRIVER_RES_LOCK);

   VMK_LIST_FORALL(&NVME_DRIVER_RES_ADAPTERLIST, itemPtr) {
      ctrlr = VMK_LIST_ENTRY(itemPtr, struct NvmeCtrlr, list);
      if (ctrlr->ctrlOsResources.scsiAdapter) {
         vmhbaName = vmk_ScsiGetAdapterName(ctrlr->ctrlOsResources.scsiAdapter);
      } else {
         vmhbaName = "unknown";
      }
      vmk_StringCopy(adapterInfo[count].name, vmhbaName,
         sizeof(adapterInfo[count].name));

      vmk_StringCopy(adapterInfo[count].signature,
         ctrlr->ctrlOsResources.nvmeSignature.name.string, sizeof(adapterInfo[count].name));

      adapterInfo[count].cookie = (vmk_uint64)ctrlr;

     /**
       * A controller is online in the following circumstances:
       *
       *    1. The controller is in STARTED or OPERATIONAL state, and
       *    2. One of the namespaces in the controller is ONLINE
       */
      adapterInfo[count].status = OFFLINE;

      vmk_SpinlockLock(ctrlr->lock);
      if (NvmeState_GetCtrlrState(ctrlr, VMK_FALSE) == NVME_CTRLR_STATE_STARTED ||
          NvmeState_GetCtrlrState(ctrlr, VMK_FALSE) == NVME_CTRLR_STATE_OPERATIONAL) {
         VMK_LIST_FORALL(&ctrlr->nsList, item) {
            ns = VMK_LIST_ENTRY(item, struct NvmeNsInfo, list);
            if (NvmeCore_IsNsOnline(ns)) {
               adapterInfo[count].status = ONLINE;
               break;
            }
         }
      }
      vmk_SpinlockUnlock(ctrlr->lock);

      VPRINT("Adapter %d: %s is %s.", count, vmhbaName,
             adapterInfo[count].status == ONLINE ? "online" : "offline");

      /**
       * Break out of we've exceeded max adapters
       */
      if (++count >= NVME_MAX_ADAPTERS) {
         break;
      }
   }
   vmk_SpinlockUnlock(NVME_DRIVER_RES_LOCK);

   *numAdapters = count;

   DPRINT_MGMT("%d adapters found.", count);

   return VMK_OK;
}


/**
 * Management callback for setting log level
 *
 * @param [in] cookies not used
 * @param [in] envelope not used
 * @param [out] log level
 * @param [out] debug level
 *
 * @return VMK_OK if successful.
 */
#if VMKAPIDDK_VERSION >= 600
VMK_ReturnStatus NvmeMgmt_SetLogLevel(vmk_MgmtCookies *cookies,
   vmk_MgmtEnvelope *envelope,
#else
VMK_ReturnStatus NvmeMgmt_SetLogLevel(vmk_uint64 cookie,
   vmk_uint64 instanceId,
#endif
   vmk_uint32 *loglevel,
   vmk_uint32 *debuglevel)
{
   nvme_dbg = *debuglevel;
   DPRINT("set nvme_dbg to 0x%x", nvme_dbg);
   vmk_LogSetCurrentLogLevel(NVME_DRIVER_RES_LOG_HANDLE, *loglevel);
   return VMK_OK;
}

/**
 * Initialize management instance, called during AttachDevice
 *
 * We maintain one management interface per controller.
 */
VMK_ReturnStatus
NvmeMgmt_CtrlrInitialize(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus status;
#if VMKAPIDDK_VERSION >= 600
   vmk_MgmtProps mgmtProps;
#endif

   /* Compose mgmt signature*/
   ctrlr->ctrlOsResources.nvmeSignature.version = VMK_REVISION_FROM_NUMBERS(NVME_MGMT_MAJOR, NVME_MGMT_MINOR,
                                                            NVME_MGMT_UPDATE, NVME_MGMT_PATCH);
   ctrlr->ctrlOsResources.nvmeSignature.numCallbacks = NVME_MGMT_CTRLR_NUM_CALLBACKS;
   ctrlr->ctrlOsResources.nvmeSignature.callbacks = nvmeCallbacks;
   vmk_Memcpy(ctrlr->ctrlOsResources.nvmeSignature.vendor.string, NVME_MGMT_VENDOR, sizeof(NVME_MGMT_VENDOR));
   /*
    * Use ctrlr->name to identify each signature per controller,
    * should be like "nvmeMgmt-nvme00030000"
    * */
   vmk_NameFormat(&ctrlr->ctrlOsResources.nvmeSignature.name, "%s-%s", NVME_MGMT_NAME, Nvme_GetCtrlrName(ctrlr));
   IPRINT("signature.name %s", ctrlr->ctrlOsResources.nvmeSignature.name.string);

#if VMKAPIDDK_VERSION >= 600
   /* Initialize smart management handle */
   mgmtProps.modId = vmk_ModuleCurrentID;
   mgmtProps.heapId = NVME_DRIVER_RES_HEAP_ID;
   mgmtProps.sig = &ctrlr->ctrlOsResources.nvmeSignature;
   mgmtProps.cleanupFn = NULL;
   mgmtProps.sessionAnnounceFn = NULL;
   mgmtProps.sessionCleanupFn = NULL;
   mgmtProps.handleCookie = (vmk_uint64) ctrlr;

   status =  vmk_MgmtInit(&mgmtProps,
                         &ctrlr->ctrlOsResources.mgmtHandle);
#else
   status =  vmk_MgmtInit(vmk_ModuleCurrentID,
                          NVME_DRIVER_RES_HEAP_ID,
                          &ctrlr->ctrlOsResources.nvmeSignature,
                          NULL,
                          (vmk_uint64) ctrlr,
                          &ctrlr->ctrlOsResources.mgmtHandle);
#endif
   if(status != VMK_OK) {
      EPRINT("Failed to init smart management handle.");
      return status;
   }

   #if (NVME_ENABLE_STATISTICS == 1)
      NvmeDebug_InitStatisticsData(&ctrlr->statsData);
   #endif

   return VMK_OK;


}


/**
 * Cleanup management interface handle
 */
void
NvmeMgmt_CtrlrDestroy(struct NvmeCtrlr *ctrlr)
{
   vmk_MgmtDestroy(ctrlr->ctrlOsResources.mgmtHandle);
}


/**
 * @brief This function converts an array of vmk_uint8 to a numeric int value
 * @param[in]	src: the source array to be converted
 * @param[in]	srcLen: length of src in bytes
 * @param[out]	dest: the numberic value converted to
 *
 * @return VMK_OK if successfully converted, return VMK_FAILURE if src's length
 *  exceeds the vaule that dest could represent.
 * */
VMK_ReturnStatus
NvmeMgmt_Convert(vmk_uint8 *src, vmk_uint32 srcLen, vmk_uint32 *dest)
{
   vmk_uint32 i = 0;
   /*Since the value type in SMART framework is hard coded as int,destLen should be 4*/
   vmk_uint32 destLen = sizeof(*dest);
   *dest = 0;

   for(i=0; i<srcLen; i++) {
      /*not exceed dest's length*/
      if (i < destLen) {
         *dest += (src[i] << (8*i));
      }
      else {
         /*if the higher bits of src are all zero, covert succeeds, otherwise fails*/
         if (src[i] != 0x00){
            EPRINT("failed at src[%d] is 0x%x, dest is 0x%4x", i, src[i], (*dest));
            *dest = 0;
            return VMK_FAILURE;
         }
      }
   }
   return VMK_OK;
}

/**
 * @brief This fucntion collects the temperature threshold by getting feature of
 * Temperature Threshold.
 * @param[in]	ctrlr, pointer to controller data structure
 * return value: temperature threshold, return 0 if fails.
 */
vmk_uint16
NvmeMgmt_GetTempThreshold(struct NvmeCtrlr *ctrlr)
{
   /*PR#1074274: hard coded here, per SMART framework's requirement*/
   return SMART_TEMPERATURE_DEFAULT_THRESHOLD;
#if 0
   struct NvmeDmaEntry dmaEntry;
   struct cq_entry cqEntry;
   VMK_ReturnStatus vmkStatus;
   vmk_uint16 threshold = 0;
   struct nvme_prp *prp;

   vmk_uint16 feature = 0x04;
   vmkStatus = OsLib_DmaAlloc(&ctrlr->ctrlOsResources, VMK_PAGE_SIZE, &dmaEntry, VMK_TIMEOUT_UNLIMITED_MS);
   if (vmkStatus != VMK_OK) {
      EPRINT("failed to allocate memory!");
      return 0;
   }
   prp = ( struct nvme_prp *)&dmaEntry.ioa;
   vmkStatus = NvmeCtrlrCmd_GetFeature(ctrlr, nsID, feature, 0, prp, &cqEntry);
   if (vmkStatus != VMK_OK) {
      EPRINT("failed to get feature of temperature threshold!");
      goto free_dma;
   }
   threshold = cqEntry.param.cmdSpecific & 0xffff;
   threshold -= 273;//from kelvin to celsius
   IPRINT("threshold is %4x, %8x", threshold, cqEntry.param.cmdSpecific);

free_dma:
   OsLib_DmaFree(&ctrlr->ctrlOsResources, &dmaEntry);
   return threshold;
#endif
}

/**
 * @brief This function parses the information in smart_log to nvmeSmartParamBundle.
 *
 * param[in] ctrlr the nvme controller data structure
 * param[in] smartLog the smart_log format of data to be parsed
 * param[out] bundle the nvmeSmartParamBundle format of data that is parsed
 *
 */
void
NvmeMgmt_ParseLogInfo(struct NvmeCtrlr *ctrlr, struct smart_log *smartLog, struct nvmeSmartParamBundle* bundle)
{
   if (!smartLog->criticalError) {
      bundle->params[NVME_SMART_HEALTH_STATUS].valid.value = 1;
      bundle->params[NVME_SMART_HEALTH_STATUS].value = NVME_SMART_HEALTH_OK;
   }
   else {
      bundle->params[NVME_SMART_HEALTH_STATUS].valid.value = 1;
      bundle->params[NVME_SMART_HEALTH_STATUS].value = NVME_SMART_HEALTH_WARNING;
   }

   /* if bit 01 set to '1', exceed a critical threshold, set to unvalid value
    * an asyn cmd may needed in this case
    */
   if((smartLog->criticalError & 0x02) == 1)
      bundle->params[NVME_SMART_DRIVE_TEMPERATURE].valid.value = 0;//not valid
   else {
      if (NvmeMgmt_Convert(smartLog->temperature, 2, &bundle->params[NVME_SMART_DRIVE_TEMPERATURE].value) == VMK_OK){
         bundle->params[NVME_SMART_DRIVE_TEMPERATURE].value -= 273;//from Kelvin to Celsius
         bundle->params[NVME_SMART_DRIVE_TEMPERATURE].valid.value = 1;//valid value
      }
   }
   bundle->params[NVME_SMART_DRIVE_TEMPERATURE].threshold = NvmeMgmt_GetTempThreshold(ctrlr);
   bundle->params[NVME_SMART_DRIVE_TEMPERATURE].valid.threshold = 1;

   /*These fileds are not able to be paresed due to the limitation of SMART framework */
#if 0
   if (NvmeMgmt_Convert(smartLog->powerOnHours, 16, &bundle->params[NVME_SMART_POWER_ON_HOURS].value) == VMK_OK)
      bundle->params[NVME_SMART_POWER_ON_HOURS].valid.value = 1;
   else
      bundle->params[NVME_SMART_POWER_ON_HOURS].valid.value = 0;

   if(NvmeMgmt_Convert(smartLog->dataUnitsRead, 16, &bundle->params[NVME_SMART_READ_SECTORS_TOT_CT].value) == VMK_OK) {
      bundle->params[NVME_SMART_READ_SECTORS_TOT_CT].value *= 1000;
      bundle->params[NVME_SMART_READ_SECTORS_TOT_CT].valid.value = 1;
   }
   else
      bundle->params[NVME_SMART_READ_SECTORS_TOT_CT].valid.value = 0;

   if(NvmeMgmt_Convert(smartLog->dataUnitsWritten, 16, &bundle->params[NVME_SMART_WRITE_SECTORS_TOT_CT].value) == VMK_OK) {
      bundle->params[NVME_SMART_WRITE_SECTORS_TOT_CT].value *= 1000;
      bundle->params[NVME_SMART_WRITE_SECTORS_TOT_CT].valid.value = 1;
   }
   else
      bundle->params[NVME_SMART_WRITE_SECTORS_TOT_CT].valid.value = 0;

   if(NvmeMgmt_Convert(smartLog->powerCycles, 16, &bundle->params[NVME_SMART_POWER_CYCLE_COUNT].value) == VMK_OK)
      bundle->params[NVME_SMART_POWER_CYCLE_COUNT].valid.value = 1;
   else
      bundle->params[NVME_SMART_POWER_CYCLE_COUNT].valid.value = 0;
   /*Print the logs out, for debugging*/
#endif
#if NVME_DEBUG
   if (nvme_dbg & NVME_DEBUG_DUMP_SMART) {
      IPRINT("dump smartLog");
      NvmeDebug_DumpSmart(smartLog);
   }
#endif

}

/**
 * @brief This function is the callback for smart plugin. It calls
 * NvmeCtrlrCmd_GetLogPage to obtain the SMART/HEATH information.
 *
 * @param[in]  cookies information pointer
 * @param[in]  envelope, used to pass ctrlr address here
 * @param[in]  nsID, namespace ID
 * @param[out] SMART/HEALTH info for user world use.
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
VMK_ReturnStatus
#if VMKAPIDDK_VERSION >= 600
kernelCbSmartGet(vmk_MgmtCookies *cookies, vmk_MgmtEnvelope *envelope,
#else
kernelCbSmartGet(vmk_uint64 cookie, vmk_uint64 instanceId,
#endif
                 vmk_uint32* nsID, struct nvmeSmartParamBundle* bundle)
{
   struct NvmeCtrlr *ctrlr;
   VMK_ReturnStatus vmkStatus = VMK_OK;
   struct smart_log *smartLog = NULL;
   vmk_int32 retryTimes;
   vmk_uint32 nameSpaceId = *nsID;

#if VMKAPIDDK_VERSION >= 600
   ctrlr = (struct NvmeCtrlr *)cookies->handleCookie;
#else
   ctrlr = (struct NvmeCtrlr *)cookie;
#endif

   vmk_Memset(bundle, 0, sizeof(struct nvmeSmartParamBundle));
   DPRINT_MGMT("nameSpaceId  %x, LPA %x \n", nameSpaceId, ctrlr->logPageAttr);

   /* Bit 0 if set to 1 then the controller supports the SMART/Health
    * information log page on a per namespace basis. Otherwise the log
    * page returned is global for all namespaces*/
   if ((!(ctrlr->logPageAttr & 0x01) && (nameSpaceId != NVME_FULL_NAMESPACE))) {
      VPRINT("Invalid namespace ID. nameSpaceId: 0x%x, LPA: 0x%x, force to use global nsId",
             nameSpaceId, ctrlr->logPageAttr);
      nameSpaceId = NVME_FULL_NAMESPACE;
   }

   /* Create buffer to store log page info */
   smartLog = Nvme_Alloc(LOG_PG_SIZE, 0, NVME_ALLOC_ZEROED);
   if (!smartLog){
      EPRINT("Failed to allocate buffer for smart log");
      return VMK_FAILURE;
   }
   
   /* Issue GetLogPage command to acquire log page info, retry if needed */
   for (retryTimes = 0; retryTimes < SMART_MAX_RETRY_TIMES; retryTimes++) {
      vmkStatus = NvmeCtrlrCmd_GetSmartLog(ctrlr, nameSpaceId, smartLog, NULL, VMK_TRUE);
      if (vmkStatus == VMK_OK) {
         NvmeMgmt_ParseLogInfo(ctrlr, smartLog, bundle);
         DPRINT_MGMT("Succeed to get log page");
         goto free_buffer;
      }
      else if (vmkStatus == VMK_TIMEOUT)  {
         /*Wait before retry*/
         vmk_WorldSleep(SMART_TIMEOUT_WAIT);
         DPRINT_MGMT("time out retrtTimes = %d", retryTimes);
      }
      else {
         EPRINT("Failed to get log page,status = 0x%x", vmkStatus);
         goto free_buffer;
     }
   }
   /* Have timed out too many times, fail this request*/
   EPRINT("Failed to get log page due to timeout");

free_buffer:
   Nvme_Free(smartLog);
   return vmkStatus;

}


/**
 * Management callback for ioctls
 *
 * This management interface wraps ioctl-based management operations into
 * VMkernel management interface.
 *
 * @param[in]  cookie information pointer
 * @param[in]  envelope, used to pass ctrlr address here
 * @param[in]  cmd ioctl command
 * @param[inout] usr_io instance that wraps pass-through commands
 *
 * @return This function returns VMK_OK if successful, otherwise Error Code
 */
VMK_ReturnStatus
#if VMKAPIDDK_VERSION >= 600
kernelCbIoctl(vmk_MgmtCookies *cookies,
              vmk_MgmtEnvelope *envelope,
#else
kernelCbIoctl(vmk_uint64 cookie,
              vmk_uint64 instanceId,
#endif
              vmk_uint32 *cmd,
              struct usr_io *uio)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;
   struct NvmeCtrlr *ctrlr;

   VMK_ASSERT(cmd != NULL);
   VMK_ASSERT(uio != NULL);
#if VMKAPIDDK_VERSION >= 600
   VMK_ASSERT(cookies != NULL);
   VMK_ASSERT(cookies->handleCookie != 0L);
#else
   VMK_ASSERT(cookie != 0L);
#endif

#if VMKAPIDDK_VERSION >= 600
   ctrlr = (struct NvmeCtrlr *)cookies->handleCookie;
#else
   ctrlr = (struct NvmeCtrlr *)cookie;
#endif
   VPRINT("Ioctl cmd %d to ctrlr %s ns %d.", *cmd,
          Nvme_GetCtrlrName(ctrlr), uio->namespaceID);

   vmkStatus = NvmeCtrlr_IoctlCommon(ctrlr, *cmd, uio);

   return vmkStatus;
}
#if NVME_DEBUG_INJECT_ERRORS

#if VMKAPIDDK_VERSION >= 600
VMK_ReturnStatus kernelCbErrInject(vmk_MgmtCookies *cookies, vmk_MgmtEnvelope *envelope,
#else
VMK_ReturnStatus kernelCbErrInject(vmk_uint64 cookie, vmk_uint64 instanceId,
#endif
                                   vmk_uint32 *globalFlag, vmk_uint32 *errType,
                                   vmk_uint32 *likelyhood, vmk_uint32 *count)
{
   struct NvmeCtrlr *ctrlr;
   vmk_ListLinks    *itemPtr;

   if (*errType <= NVME_DEBUG_ERROR_NONE || *errType >= NVME_DEBUG_ERROR_LAST) {
      IPRINT("Invalid Error Type");
      return VMK_FAILURE;
   }

   if (*globalFlag == VMK_TRUE) {
      VMK_LIST_FORALL(&NVME_DRIVER_RES_ADAPTERLIST, itemPtr) {
         ctrlr = VMK_LIST_ENTRY(itemPtr, struct NvmeCtrlr, list);
         ctrlr->errCounters[*errType].likelyhood = *likelyhood;
         ctrlr->errCounters[*errType].count      = *count;
      }

   } else {
      #if VMKAPIDDK_VERSION >= 600
         ctrlr = (struct NvmeCtrlr *)cookies->handleCookie;
      #else
         ctrlr = (struct NvmeCtrlr *)cookie;
      #endif

      ctrlr->errCounters[*errType].likelyhood = *likelyhood;
      ctrlr->errCounters[*errType].count      = *count;
   }

   VPRINT("Error injection is now enabled: errType = %d, count =%d, likelyhood =%d", *errType, *count, *likelyhood);


   return VMK_OK;
}
#endif
