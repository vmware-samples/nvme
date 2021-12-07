/*****************************************************************************
 * Copyright (c) 2016-2021 VMware, Inc. All rights reserved.
 * -- VMware Confidential
 *****************************************************************************/

/*
 * @file: nvme_pcie_adapter.c --
 *
 *   NVME adapter and controller interface implementation of native nvme_pcie driver
 */

#include "nvme_pcie_int.h"

extern int nvmePCIEDma4KSwitch;
extern vmk_uint32 nvmePCIEFakeAdminQSize;
extern int nvmePCIEMsiEnbaled;

static VMK_ReturnStatus RequestIoQueues(NVMEPCIEController *ctrlr,
                                        vmk_uint32 *nrIoQueues);

/**
 * startAdapter callback of adapter ops
 */
static VMK_ReturnStatus
StartAdapter(vmk_NvmeAdapter adapter)
{
   NVMEPCIEController *ctrlr = vmk_NvmeGetAdapterDriverData(adapter);
   return NVMEPCIEControllerInit(ctrlr);
}

/**
 * stopAdapter callback of adapter ops
 */
static VMK_ReturnStatus
StopAdapter(vmk_NvmeAdapter adapter)
{
   NVMEPCIEController *ctrlr = vmk_NvmeGetAdapterDriverData(adapter);
   return NVMEPCIEControllerDestroy(ctrlr);
}

/**
 * queryAdapter callback of adapter ops
 */
static VMK_ReturnStatus
QueryAdapter(vmk_NvmeAdapter adapter,
             vmk_NvmeAdapterQueryID id,
             vmk_NvmeAdapterQueryParams *params)
{
   NVMEPCIEController *ctrlr = vmk_NvmeGetAdapterDriverData(adapter);
   const vmk_NvmeIdentifyController *identData = NULL;
   VMK_ReturnStatus vmkStatus = VMK_OK;
   vmk_uint8 *uid;
   int i;

   switch (id) {
      case VMK_NVME_ADAPTER_QUERY_ADAPTER_UID:
         uid = *params->uidParams.uid;

         if (ctrlr->osRes.vmkController == NULL) {
            return VMK_NOT_READY;
         }
         identData = vmk_NvmeGetControllerIdentifyData(ctrlr->osRes.vmkController);
         if (identData == NULL) {
            return VMK_NOT_READY;
         }
         if (vmkStatus == VMK_OK) {
            if (identData->subnqn[0] == 'n') {
               vmk_Snprintf((char *)params->uidParams.uid,
                            VMK_NVME_ADAPTER_UID_LEN,
                            "%s",
                            identData->subnqn);
            } else {
               vmk_Snprintf((char *)params->uidParams.uid,
                            VMK_NVME_ADAPTER_UID_LEN,
                            "%s%x%x%.20s%.40s",
                            VMK_NVME_NSS,
                            identData->vid,
                            identData->ssvid,
                            identData->sn,
                            identData->mn);
               i = VMK_NVME_ADAPTER_UID_LEN - 1;;
               while (i >= 0) {
                  if (uid[i] == '\0' || uid[i] == ' ') {
                     uid[i] = '\0';
                  } else {
                     break;
                  }
                  i --;
               }
               while (i >= 0) {
                  if (uid[i] == ' ') {
                     uid[i] = '_';
                  }
                  i --;
               }
            }
         }
         return vmkStatus;
      default:
         return VMK_NOT_SUPPORTED;
   }
}

/**
 * notifyAdapterIOAllowed callback of adapter ops
 */
static void
NotifyAdapterIOAllowed(vmk_NvmeAdapter adapter,
                       vmk_Bool ioAllowed)
{
   NVMEPCIEController *ctrlr = vmk_NvmeGetAdapterDriverData(adapter);

   IPRINT(ctrlr, "IOAllowed: %d.", ioAllowed);

#if NVME_PCIE_STORAGE_POLL
   if (ioAllowed && ctrlr->pollEnabled) {
      NVMEPCIEStoragePollSetup(ctrlr);
   }
#endif
}

/**
 * Adapter ops used to register vmk_NvmeAdapter
 */
vmk_NvmeAdapterOps nvmePCIEAdapterOps = {
   .startAdapter = StartAdapter,
   .stopAdapter  = StopAdapter,
   .queryAdapter = QueryAdapter,
   .notifyAdapterIOAllowed = NotifyAdapterIOAllowed,
};

void Workaround4HW(NVMEPCIEController *ctrlr, vmk_NvmeRegisterID regID, vmk_uint32* regValue)
{
   vmk_uint16 sqsize, cqsize;
   if (VMK_UNLIKELY(NVME_PCIE_WKR_ALL_AWS == ctrlr->workaround)) {
      /*
       * AQA on Arm a1 returns queue size 2, which is insufficient.
       * AQA on AWS m5.xlarge or r5.metal is variable. It can't be predicated.
       */
      if (VMK_UNLIKELY(VMK_NVME_REG_AQA == regID)) {
         if (VMK_LIKELY(nvmePCIEFakeAdminQSize == 0)) {
            WPRINT(ctrlr, "Raw AQA=0x%x, fake AQA=0x000f000f", *regValue);
            *regValue = 0xf000f;
         } else {
            WPRINT(ctrlr, "Raw AQA=0x%x, fake SQ,CQ size=%x", *regValue, nvmePCIEFakeAdminQSize);
            *regValue = ((nvmePCIEFakeAdminQSize << 16) | nvmePCIEFakeAdminQSize);
         }
      }
   } else {
      if (VMK_UNLIKELY(nvmePCIEFakeAdminQSize && (VMK_NVME_REG_AQA == regID))) {
         sqsize = (*regValue & 0xffff);
         cqsize = (*regValue) >> 16;
         if (cqsize >= nvmePCIEFakeAdminQSize &&  sqsize >= nvmePCIEFakeAdminQSize)  {
            WPRINT(ctrlr, "Raw AQA=0x%x, fake SQ,CQ size=%x", *regValue, nvmePCIEFakeAdminQSize);
            *regValue = ((nvmePCIEFakeAdminQSize << 16) | nvmePCIEFakeAdminQSize);
         }
      }
   }
}

/**
 * readRegister callback of controller ops
 */
static VMK_ReturnStatus
ReadRegister32(vmk_NvmeController controller,
               vmk_NvmeRegisterID regID,
               vmk_uint32 *regValue)
{
   NVMEPCIEController *ctrlr = vmk_NvmeGetControllerDriverData(controller);
   if (VMK_UNLIKELY(ctrlr->isRemoved)) {
      return VMK_PERM_DEV_LOSS;
   }
   *regValue = NVMEPCIEReadl(ctrlr->regs + regID);
   /*do workaround for some special device.*/
   Workaround4HW(ctrlr, regID, regValue);
   DPRINT_CTRLR(ctrlr, "regID: 0x%x regValue: 0x%x", regID, *regValue);
   return VMK_OK;
}

/**
 * readRegister64 callback of controller ops
 */
static VMK_ReturnStatus
ReadRegister64(vmk_NvmeController controller,
               vmk_NvmeRegisterID regID,
               vmk_uint64 *regValue)
{
   NVMEPCIEController *ctrlr = vmk_NvmeGetControllerDriverData(controller);
   if (VMK_UNLIKELY(ctrlr->isRemoved)) {
      return VMK_PERM_DEV_LOSS;
   }
   *regValue = NVMEPCIEReadq(ctrlr->regs + regID);
   DPRINT_CTRLR(ctrlr, "regID: 0x%x regValue: 0x%lx", regID, *regValue);
   return VMK_OK;
}

/**
 * writeRegister callback of controller ops
 */
static VMK_ReturnStatus
WriteRegister32(vmk_NvmeController controller,
                vmk_NvmeRegisterID regID,
                vmk_uint32 regValue)
{
   NVMEPCIEController *ctrlr = vmk_NvmeGetControllerDriverData(controller);
   if (VMK_UNLIKELY(ctrlr->isRemoved)) {
      return VMK_PERM_DEV_LOSS;
   }
   NVMEPCIEWritel(regValue, (ctrlr->regs + regID));
   DPRINT_CTRLR(ctrlr, "regID: 0x%x regValue: 0x%x", regID, regValue);
   return VMK_OK;
}

/**
 * command callback of controller ops
 */
static VMK_ReturnStatus
NvmeCommand(vmk_NvmeController controller,
           vmk_NvmeCommand *vmkCmd,
           vmk_NvmeQueueID qid)
{
   NVMEPCIEController *ctrlr = vmk_NvmeGetControllerDriverData(controller);
   return NVMEPCIESubmitAsyncCommand(ctrlr, vmkCmd, qid);
}

/**
 * configAdminQueue callback of controller ops
 */
static VMK_ReturnStatus
ConfigAdminQueue(vmk_NvmeController controller)
{
   NVMEPCIEController *ctrlr = vmk_NvmeGetControllerDriverData(controller);
   NVMEPCIEQueueInfo *qinfo = &ctrlr->queueList[0];
   vmk_NvmeRegAqa aqa;

   if (ctrlr->isRemoved) {
      return VMK_PERM_DEV_LOSS;
   }

   aqa.asqs = qinfo->sqInfo->qsize - 1;
   aqa.acqs = qinfo->cqInfo->qsize - 1;
   NVMEPCIEWritel(*(vmk_uint32 *)&aqa, (ctrlr->regs + VMK_NVME_REG_AQA));
   NVMEPCIEWriteq(qinfo->cqInfo->compqPhy, (ctrlr->regs + VMK_NVME_REG_ACQ));
   NVMEPCIEWriteq(qinfo->sqInfo->subqPhy, (ctrlr->regs + VMK_NVME_REG_ASQ));

   return VMK_OK;
}

static inline VMK_ReturnStatus
ReallocIntr(NVMEPCIEController *ctrlr, int intrNum)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;
   NVMEPCIEQueueInfo *adminq = &ctrlr->queueList[0];

   if (!nvmePCIEMsiEnbaled) {
      NVMEPCIESuspendQueue(adminq);
      NVMEPCIEIntrUnregister(ctrlr->osRes.intrArray[0], adminq);
      NVMEPCIEIntrFree(ctrlr);

      vmkStatus = NVMEPCIEIntrAlloc(ctrlr, VMK_PCI_INTERRUPT_TYPE_MSIX, intrNum);
      if (vmkStatus != VMK_OK) {
         EPRINT(ctrlr, "Failed to allocate MSIX %d interrupt cookies", intrNum);
         return VMK_OK;
      }

      vmkStatus = NVMEPCIEIntrRegister(ctrlr->osRes.device,
                                       ctrlr->osRes.intrArray[0],
                                       adminq,
                                       NVMEPCIEGetCtrlrName(ctrlr),
                                       NVMEPCIEQueueIntrAck,
                                       NVMEPCIEQueueIntrHandler);
      if (vmkStatus != VMK_OK) {
         EPRINT(ctrlr, "Failed to register interrupt for admin queue, 0x%x.", vmkStatus);
      }
   }

   NVMEPCIEResumeQueue(adminq);

   return VMK_OK;
}

/**
 * setNumberIOQueues callback of controller ops
 */
static VMK_ReturnStatus
SetNumberIOQueues(vmk_NvmeController controller,
                  vmk_uint32 numQueuesDesired,
                  vmk_uint32 *numQueuesAllocated)
{
   VMK_ReturnStatus vmkStatus;
   NVMEPCIEController *ctrlr = vmk_NvmeGetControllerDriverData(controller);
   int nrIoQueues = numQueuesDesired;
   ctrlr->maxIoQueues = 0;

   if (nrIoQueues > NVME_PCIE_MAX_IO_QUEUES) {
      WPRINT(ctrlr,
             "Required IO queue number %d exceeds driver limitation %d, "
             "reset as driver limitation: %d.",
             nrIoQueues,
             NVME_PCIE_MAX_IO_QUEUES,
             NVME_PCIE_MAX_IO_QUEUES);
      nrIoQueues = NVME_PCIE_MAX_IO_QUEUES;
   }

   // Customize for AWS EBS device, refer to PR #2126797.
   if (NVMEPCIEIsEBSCustomDevice(ctrlr)) {
      nrIoQueues = 1;
   }

   // Only reallocate intr in controller init or IO queue number is changed in reset.
   if (!nvmePCIEMsiEnbaled) {
      if (ctrlr->osRes.numIntrs == 1 || ctrlr->osRes.numIntrs != 1 + nrIoQueues) {
         vmkStatus = ReallocIntr(ctrlr, 1 + nrIoQueues);
         if (vmkStatus != VMK_OK) {
            EPRINT(ctrlr, "Failed to re-allocate %d interrupt cookie.", 1 + nrIoQueues);
            return vmkStatus;
         }
      }

      nrIoQueues = ctrlr->osRes.numIntrs - 1;
   } else {
      nrIoQueues = 1;
   }

   vmkStatus = RequestIoQueues(ctrlr, &nrIoQueues);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to allocate hardware IO queues.");
      return vmkStatus;
   }
   *numQueuesAllocated = nrIoQueues;
   ctrlr->maxIoQueues = nrIoQueues;

   return vmkStatus;
}

/**
 * createIOQueue callback of controller ops
 */
static VMK_ReturnStatus
CreateIOQueue(vmk_NvmeController controller,
              vmk_NvmeQueueID qid,
              vmk_uint16 qsize)
{
   NVMEPCIEController *ctrlr = vmk_NvmeGetControllerDriverData(controller);
   return NVMEPCIEQueueCreate(ctrlr, qid, qsize);
}

/**
 * deleteIOQueue callback of controller ops
 */
static VMK_ReturnStatus
DeleteIOQueue(vmk_NvmeController controller,
              vmk_NvmeQueueID qid,
              vmk_NvmeQueueDeleteReason reason)
{
   NVMEPCIEController *ctrlr = vmk_NvmeGetControllerDriverData(controller);
   switch (reason) {
      case VMK_NVME_DELETE_QUEUE_FOR_RESET:
         return NVMEPCIEQueueDestroy(ctrlr, qid, VMK_NVME_STATUS_VMW_IN_RESET);
      case VMK_NVME_DELETE_QUEUE_FOR_SHUTDOWN:
         return NVMEPCIEQueueDestroy(ctrlr, qid, VMK_NVME_STATUS_VMW_QUIESCED);
      default:
         WPRINT(ctrlr, "unsupported queue delete reason: %d", reason);
         return VMK_BAD_PARAM;
   }

}

/**
 * stopQueue callback of controller ops
 */
static VMK_ReturnStatus
ResetAdminQueue(vmk_NvmeController controller)
{
   VMK_ReturnStatus status = VMK_OK;
   NVMEPCIEController *ctrlr = vmk_NvmeGetControllerDriverData(controller);
   NVMEPCIEQueueInfo *qinfo = &ctrlr->queueList[0];
   NVMEPCIEStopQueue(qinfo, VMK_NVME_STATUS_VMW_IN_RESET);

   /** Don't start admin queue if controller has been hot removed. */
   if (!ctrlr->isRemoved) {
      status = NVMEPCIEStartQueue(qinfo);
   }

   return status;
}

/**
 * stopQueue callback of controller ops
 */
static void
PollHandler(vmk_NvmeController controller)
{
   NVMEPCIEController *ctrlr = vmk_NvmeGetControllerDriverData(controller);
   NVMEPCIEQueueInfo *qinfo;
   int i;

   for (i = 1; i<= ctrlr->numIoQueues; i++){
      qinfo = &ctrlr->queueList[i];
      vmk_SpinlockLock(qinfo->cqInfo->lock);
      NVMEPCIEProcessCq(qinfo);
      vmk_SpinlockUnlock(qinfo->cqInfo->lock);
   }

   return;
}

static vmk_uint32
GetStripeSize(vmk_NvmeController controller)
{
   NVMEPCIEController *ctrlr = vmk_NvmeGetControllerDriverData(controller);
   const vmk_NvmeIdentifyController *identData = vmk_NvmeGetControllerIdentifyData(controller);
   vmk_PCIDeviceID *pciId = &ctrlr->osRes.pciId;
   vmk_uint32 stripeSize = 0;
   vmk_NvmeRegCap cap = {0};
   VMK_ReturnStatus vmkStatus;
   vmk_ConfigParamHandle configParam;
   vmk_uint32 forceStripe = 0;

   vmkStatus = vmk_ConfigParamOpen(VMK_CONFIG_GROUP_MISC, "nvmePCIEForceStripe", &configParam);
   if (vmkStatus != VMK_OK) {
      WPRINT(ctrlr, "Failed to open config param, 0x%x", vmkStatus);
   } else {
      vmkStatus = vmk_ConfigParamGetUint(configParam, &forceStripe);
      if (vmkStatus != VMK_OK) {
         WPRINT(ctrlr, "Failed to get config param, 0x%x", vmkStatus);
         forceStripe = 0;
      }
      vmk_ConfigParamClose(configParam);
   }


   // So far, we only know the following Intel devices have stripe limitation.
   if ((pciId->vendorID == 0x8086 && (pciId->deviceID == 0x0953 ||
                                     pciId->deviceID == 0x0a53 ||
                                     pciId->deviceID == 0x0a54 ||
                                     pciId->deviceID == 0x0a55 ||
                                     pciId->deviceID == 0x0b60)) ||
       forceStripe) {
      ReadRegister64(controller, VMK_NVME_REG_CAP, (vmk_uint64 *)&cap);
      if (identData->mdts != 0) {
         stripeSize = (1 << identData->mdts) << (cap.mpsmin + 12);
      }
      IPRINT(ctrlr, "vendorID: 0x%x, deviceID: 0x%x, mdts: 0x%x, vs[3]: 0x%x, stripeSize: 0x%x",
             pciId->vendorID, pciId->deviceID, identData->mdts, identData->vs[3], stripeSize);
   }

   return stripeSize;
}


static vmk_IntrCookie
GetIntrCookie(vmk_NvmeController controller, vmk_NvmeQueueID qid)
{
   NVMEPCIEController *ctrlr = vmk_NvmeGetControllerDriverData(controller);
   if (!nvmePCIEMsiEnbaled) {
      if (ctrlr->osRes.intrType != VMK_PCI_INTERRUPT_TYPE_MSIX ||
          qid >= ctrlr->osRes.numIntrs) {
         return VMK_INVALID_INTRCOOKIE;
      }
   }
   return ctrlr->osRes.intrArray[qid];
}

/**
 * Controller ops used to register vmk_NvmeAdapter
 */
vmk_NvmeControllerOps nvmePCIEControllerOps = {
   .readRegister = ReadRegister32,
   .readRegister64 = ReadRegister64,
   .writeRegister = WriteRegister32,
   .command = NvmeCommand,
   .configAdminQueue = ConfigAdminQueue,
   .setNumberIOQueues = SetNumberIOQueues,
   .createIOQueue = CreateIOQueue,
   .deleteIOQueue = DeleteIOQueue,
   .resetAdminQueue = ResetAdminQueue,
   .pollHandler = PollHandler,
   .getStripeSize = GetStripeSize,
   .getIntrCookie = GetIntrCookie,
};

#ifdef NVME_STATS

/**
 * Get statistics of given queue of given controller..
 *
 * @param[in]  controller  Controller instance.
 * @param[in]  qid         Queue ID. Specify which queue get stats from.
 * @param[in]  cat         Statistics category to get.
 * @param[out] stats       Return statistic wantted.
 *
 * @return VMK_OK on success, error code otherwise
 */
VMK_ReturnStatus
GetStatistics(vmk_NvmeController controller,
             vmk_NvmeQueueID qid,
             vmk_NvmeStatisticsCategory cat,
             vmk_NvmeStatistics* stats)
{
   VMK_ReturnStatus vmkStatus = VMK_FAILURE;
   NVMEPCIEController *ctrlr = vmk_NvmeGetControllerDriverData(controller);
   NVMEPCIEQueueInfo *qinfo = &ctrlr->queueList[qid];

   if (VMK_NVME_STATS_CAT_PCIE) {
      stats->pcie.intrCount = qinfo->stats->intrCount;
      vmkStatus = VMK_OK;
   }
   return vmkStatus;
}

/**
 * Set statistics configuraton for given controller
 *
 * @param[in] controller  Controller instance.
 * @param[in] cat         Statistic category to set.
 * @param[in] config      Statistics to set.
 *
 * @return VMK_OK on success, error code otherwise
 */
VMK_ReturnStatus
SetStatistics(vmk_NvmeController controller,
            vmk_NvmeStatisticsCategory cat,
            vmk_NvmeStatistics* config)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;
   NVMEPCIEController *ctrlr = vmk_NvmeGetControllerDriverData(controller);

   ctrlr->statsEnabled = config->config.enabled;
   return vmkStatus;
}

/**
 * Adapter's capability
 */
vmk_NvmeAdapterCapOptStats nvmePCIEAdapterCapOptStats = {
   .getStats = GetStatistics,
   .setStats = SetStatistics,
};

#endif

/**
 * Allocate and register vmk_NvmeAdapter
 *
 * @param[in] ctrlr  Controller instance
 *
 * @return VMK_OK on success, error code otherwise
 */
VMK_ReturnStatus
NVMEPCIEAdapterInit(NVMEPCIEController *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   vmk_NvmeAdapter vmkAdapter;
   vmk_NvmeAdapterAllocProps adapterProps;
   vmk_DMAConstraints constraints;
   vmk_DMAEngineProps props;

   /** Create DMA engine for IO */
   constraints.addressMask = VMK_ADDRESS_MASK_64BIT;
   //TODO: Set this value to MDTS in identify controller
   constraints.maxTransfer = NVME_PCIE_MAX_TRANSFER_SIZE;
   /**
    * TODO: Since the sg to prp translation is processed in nvme core,
    * The constraints should be provided by nvme core. Temporally set
    * them with the values used in native nvme driver.
    */
   constraints.sgMaxEntries = NVME_PCIE_SG_MAX_ENTRIES;
   constraints.sgElemMaxSize = 0;
   constraints.sgElemSizeMult = 512;
   constraints.sgElemAlignment = 4;
   constraints.sgElemStraddle = VMK_ADDRESS_MASK_32BIT + 1;

   // Customize for AWS EBS and local device, refer to PR #2126797 & PR #2196444.
   if (NVMEPCIEIsEBSCustomDevice(ctrlr) || NVMEPCIEIsAWSLocalDevice(ctrlr) ||
       NVMEPCIEIsSmallQsize(ctrlr) || nvmePCIEDma4KSwitch) {
      constraints.sgElemSizeMult = VMK_PAGE_SIZE;
      constraints.sgElemAlignment = VMK_PAGE_SIZE;
      WPRINT(ctrlr, "sgElemSizeMult: %d, sgElemAlignment: %d",
                     constraints.sgElemSizeMult, constraints.sgElemAlignment);
   }

   /* fix pr2370756, pr2324145 */
   NVMEPCIEDetectWorkaround(ctrlr);
   WPRINT(ctrlr, "workaround=%d", ctrlr->workaround);
   vmk_NameFormat(&props.name, "%s-IODmaEngine", NVMEPCIEGetCtrlrName(ctrlr));
   props.module = vmk_ModuleCurrentID;
   props.flags = VMK_DMA_ENGINE_FLAGS_COHERENT;
   props.device = ctrlr->osRes.device;
   props.constraints = &constraints;
   props.bounce = NULL;
   vmkStatus = vmk_DMAEngineCreate(&props, &ctrlr->osRes.IODmaEngine);
   if (vmkStatus != VMK_OK) {
      return vmkStatus;
   }

   /** Allocate nvme adapter */
   vmk_Memset(&adapterProps, 0, sizeof(adapterProps));
   adapterProps.moduleID = vmk_ModuleCurrentID;
   adapterProps.heapID = NVME_PCIE_DRIVER_RES_HEAP_ID;
   adapterProps.adapterOps = nvmePCIEAdapterOps;
   adapterProps.dmaEngine = ctrlr->osRes.IODmaEngine;
   adapterProps.driverData = ctrlr;
   adapterProps.transType = VMK_NVME_TRANSPORT_PCIE;

   vmkStatus = vmk_NvmeAllocateAdapter(&adapterProps, &vmkAdapter);
   if (vmkStatus != VMK_OK) {
      vmk_DMAEngineDestroy(ctrlr->osRes.IODmaEngine);
      return vmkStatus;
   }
   VMK_ASSERT(vmkAdapter != NULL);

#if NVME_ABORT == 1
   vmkStatus = vmk_NvmeRegisterAdapterCapability(
                  vmkAdapter,
                  VMK_NVME_ADAPTER_CAP_NVME_ABORT,
                  NULL);
   if (vmkStatus == VMK_OK) {
      IPRINT(ctrlr, "Abort capability is enabled.");
      ctrlr->abortEnabled = VMK_TRUE;
   } else if (vmkStatus == VMK_IS_DISABLED) {
      IPRINT(ctrlr, "Abort capability is not enabled.");
      ctrlr->abortEnabled = VMK_FALSE;
      vmkStatus = VMK_OK;
   } else {
      EPRINT(ctrlr, "Failed to register abort capability,0x%x.", vmkStatus);
      vmk_NvmeFreeAdapter(vmkAdapter);
      vmk_DMAEngineDestroy(ctrlr->osRes.IODmaEngine);
      return vmkStatus;
   }
#endif

#ifdef NVME_STATS
   vmkStatus = vmk_NvmeRegisterAdapterCapability(
                  vmkAdapter,
                  VMK_NVME_ADAPTER_CAP_STATS,
                  (vmk_NvmeAdapterCapOpt*)(&nvmePCIEAdapterCapOptStats));
   ctrlr->statsEnabled = VMK_FALSE;
   if (vmkStatus == VMK_IS_DISABLED || vmkStatus == VMK_OK) {
      vmkStatus = VMK_OK;
   } else {
      EPRINT(ctrlr, "Failed to register nvme-stats capability, 0x%x.", vmkStatus);
      vmk_NvmeFreeAdapter(vmkAdapter);
      vmk_DMAEngineDestroy(ctrlr->osRes.IODmaEngine);
      return vmkStatus;
   }
#endif

   ctrlr->osRes.vmkAdapter = vmkAdapter;
   return VMK_OK;
}

/**
 * Unregister and free vmk_NvmeAdapter
 *
 * @param[in] ctrlr  Controller instance
 *
 * @return VMK_OK on success, error code otherwise
 */
VMK_ReturnStatus
NVMEPCIEAdapterDestroy(NVMEPCIEController *ctrlr)
{
   vmk_NvmeFreeAdapter(ctrlr->osRes.vmkAdapter);
   vmk_DMAEngineDestroy(ctrlr->osRes.IODmaEngine);
   ctrlr->osRes.vmkAdapter = NULL;
   return VMK_OK;
}

/**
 * Allocate and register vmk_NvmeController
 *
 * @param[in] ctrlr  Controller instance
 *
 * @return VMK_OK on success, error code otherwise
 */
VMK_ReturnStatus
NVMEPCIEControllerInit(NVMEPCIEController *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   vmk_NvmeController vmkController;
   vmk_NvmeControllerAllocProps allocProps;
   const vmk_NvmeIdentifyController* identData;

   vmk_Memset(&allocProps, 0, sizeof(allocProps));
   allocProps.moduleID = vmk_ModuleCurrentID;
   allocProps.heapID = NVME_PCIE_DRIVER_RES_HEAP_ID;
   allocProps.transType = VMK_NVME_TRANSPORT_PCIE;
   allocProps.controllerOps = nvmePCIEControllerOps;
   allocProps.driverData = ctrlr;

   vmkStatus = vmk_NvmeAllocateController(&allocProps, &vmkController);
   if (vmkStatus != VMK_OK) {
      return vmkStatus;
   }

   vmkStatus = vmk_NvmeRegisterController(ctrlr->osRes.vmkAdapter, vmkController);
   if (vmkStatus != VMK_OK) {
      vmk_NvmeFreeController(vmkController);
      return vmkStatus;
   }
   identData = vmk_NvmeGetControllerIdentifyData(vmkController);

   ctrlr->osRes.vmkController = vmkController;
   return VMK_OK;
}

/**
 * Unregister and free vmk_NvmeController
 *
 * @param[in] ctrlr  Controller instance
 *
 * @return VMK_OK on success, error code otherwise
 */
VMK_ReturnStatus
NVMEPCIEControllerDestroy(NVMEPCIEController *ctrlr)
{
   vmk_NvmeUnregisterController(ctrlr->osRes.vmkController);
   vmk_NvmeFreeController(ctrlr->osRes.vmkController);
   ctrlr->osRes.vmkController = NULL;
   return VMK_OK;
}

/**
 * Request number of queues for the controller
 *
 * @param[in]    ctrlr       Controller instance
 * @param[inout] nrIoQueues  Number of IO queues
 *
 * @return VMK_OK on success, error code otherwise
 */
static VMK_ReturnStatus
RequestIoQueues(NVMEPCIEController *ctrlr, vmk_uint32 *nrIoQueues)
{
   vmk_NvmeCommand *vmkCmd;
   vmk_uint16 nq= *nrIoQueues;
   vmk_NvmeSetFeaturesCmd *setFeatureCmd;
   vmk_NvmeSetFeaturesRsp *setFeatureRsp;
   VMK_ReturnStatus vmkStatus;

   IPRINT(ctrlr, "Attemp to allocate %d IO queues.", *nrIoQueues);

   vmkCmd = NVMEPCIEAlloc(sizeof(vmk_NvmeCommand), 0);
   if (vmkCmd == NULL) {
      return VMK_NO_MEMORY;
   }
   setFeatureCmd = (vmk_NvmeSetFeaturesCmd *)&vmkCmd->nvmeCmd;
   setFeatureCmd->cdw0.opc= VMK_NVME_ADMIN_CMD_SET_FEATURES;
   setFeatureCmd->cdw10.fid = VMK_NVME_FEATURE_ID_NUM_QUEUE;
   setFeatureCmd->cdw11.nqr.nsqr = nq - 1; /** 0's based value */
   setFeatureCmd->cdw11.nqr.ncqr = nq - 1; /** 0's based value */

   vmkStatus = NVMEPCIESubmitSyncCommand(ctrlr, vmkCmd, 0, NULL, 0, ADMIN_TIMEOUT);

   if (VMK_UNLIKELY(vmkStatus == VMK_TIMEOUT)) {
      return vmkStatus;
   }

   setFeatureRsp = (vmk_NvmeSetFeaturesRsp *)&vmkCmd->cqEntry;
   if (vmkCmd->nvmeStatus == VMK_NVME_STATUS_GC_SUCCESS) {
      if (setFeatureRsp->dw0.nqa.nsqa < nq - 1) {
         nq = setFeatureRsp->dw0.nqa.nsqa + 1;
      }
      if (setFeatureRsp->dw0.nqa.ncqa < nq - 1) {
         nq = setFeatureRsp->dw0.nqa.ncqa + 1;
      }
      *nrIoQueues = nq;
      IPRINT(ctrlr, "Allocated %d IO queues", *nrIoQueues);
   } else {
      EPRINT(ctrlr, "Set feature command failed, 0x%x", vmkCmd->nvmeStatus);
      vmkStatus = VMK_FAILURE;
   }

   NVMEPCIEFree(vmkCmd);
   return vmkStatus;
}
