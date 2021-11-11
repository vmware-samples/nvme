/*****************************************************************************
 * Copyright (c) 2016-2020 VMware, Inc. All rights reserved.
 * -- VMware Confidential
 *****************************************************************************/

/*
 * @file: nvme_pcie_driver.c --
 *
 *   Driver interface of native nvme_pcie driver
 */

#include "nvme_pcie_int.h"

/** PCI CMD register offset */
#define NVME_PCIE_REG_CMD 0x4
/** Bus Master Enable (BME) mask in PCI CMD register */
#define NVME_PCIE_REG_CMD_BME 0x4

int nvmePCIEAdminQueueSize = 256;

static VMK_ReturnStatus PciInit(NVMEPCIEController *ctrlr);
static VMK_ReturnStatus PciCleanup(NVMEPCIEController *ctrlr);
static VMK_ReturnStatus DmaInit(NVMEPCIEController *ctrlr);
static VMK_ReturnStatus DmaCleanup(NVMEPCIEController *ctrlr);
static VMK_ReturnStatus SetupAdminQueue(NVMEPCIEController *ctrlr);
static void DestroyAdminQueue(NVMEPCIEController *ctrlr);
extern void NVMEPCIESuspendQueue(NVMEPCIEQueueInfo *qinfo);
extern void NVMEPCIEFlushQueue(NVMEPCIEQueueInfo *qinfo, vmk_NvmeStatus status);
/**
 * Wait for CSTS.RDY to become expected value
 *
 * @param[in]  ctrlr   Controller instance
 * @param[in]  wait    Time (in seconds) to wait
 * @param[in]  csts    Controller status
 * @param[in]  ready   Expected CSTS.RDY value
 * @param[out] result  VMK_OK on success, error code otherwise
 */
#define NVMEPCIEWaitCtrlrReady(ctrlr, wait, csts, ready, result) \
{                                                                \
   vmk_uint32 maxWait = wait * 10;                               \
   result = VMK_OK;                                              \
   do {                                                          \
      result = vmk_WorldSleep(100 * 1000); /* sleep 100ms */     \
      csts = NVMEPCIEReadl(ctrlr->regs + VMK_NVME_REG_CSTS);     \
      if (((vmk_NvmeRegCsts *)&csts)->rdy == ready) {            \
         break;                                                  \
      }                                                          \
      if (result != VMK_OK) { /* some error happend */           \
         break;                                                  \
      }                                                          \
      if (! --maxWait) {                                         \
         result = VMK_TIMEOUT;                                   \
         break;                                                  \
      }                                                          \
   } while (maxWait);                                            \
   DPRINT_CTRLR(ctrlr, "csts 0x%x, maxWait: %d, result: 0x%x.",  \
      csts, maxWait, result);                                    \
}

/**
 * Stop the controller by clearing CC.EN
 *
 * @param[in] ctrlr  Controller instance
 *
 * @return VMK_OK on success, error code otherwise
 */
VMK_ReturnStatus
NVMEPCIEHwStop(NVMEPCIEController *ctrlr)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;
   vmk_uint32 hwTimeout;
   vmk_uint64 cap;
   vmk_uint32 csts = 0;
   vmk_uint32 cc;

   cap = NVMEPCIEReadq(ctrlr->regs + VMK_NVME_REG_CAP);
   DPRINT_CTRLR(ctrlr, "Controller capabilities: 0x%016lx.", cap);
   hwTimeout = ((vmk_NvmeRegCap *)&cap)->to;
   hwTimeout = (hwTimeout + 1) >> 1;
   DPRINT_CTRLR(ctrlr, "Controller timeout %d seconds.", hwTimeout);

   /** Clear CC.EN */
   cc = NVMEPCIEReadl(ctrlr->regs + VMK_NVME_REG_CC);
   if (((vmk_NvmeRegCc *)&cc)->en) {
      ((vmk_NvmeRegCc *)&cc)->en = 0;
      NVMEPCIEWritel(cc, (ctrlr->regs + VMK_NVME_REG_CC));
   }
   NVMEPCIEWaitCtrlrReady(ctrlr, hwTimeout, csts, 0, vmkStatus);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Controller reset clear enable failure status 0x%x, %s",
             csts, vmk_StatusToString(vmkStatus));
   }

   return vmkStatus;
}

VMK_ReturnStatus
NVMEPCIEGetCtrlrCap(NVMEPCIEController *ctrlr)
{
   vmk_uint64 cap;

   cap = NVMEPCIEReadq(ctrlr->regs + VMK_NVME_REG_CAP);
   ctrlr->dstrd = ((vmk_NvmeRegCap *)&cap)->dstrd;
   if (ctrlr->dstrd != 0) {
      IPRINT(ctrlr, "Controller doorbell stride %d", ctrlr->dstrd);
   }

   return VMK_OK;
}

/**
 * attachDevice callback of driver ops
 */
static VMK_ReturnStatus
AttachDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus;
   NVMEPCIEController *ctrlr = NULL;
   char domainName[VMK_MISC_NAME_MAX];

   MOD_IPRINT("Called with %p.", device);
   /** Allocate the nvme pcie controller object */
   ctrlr = NVMEPCIEAlloc(sizeof *ctrlr, VMK_L1_CACHELINE_SIZE);
   if (ctrlr == NULL) {
      MOD_EPRINT("Failed to allocate nvme pcie controller object.");
      return VMK_NO_MEMORY;
   }

   ctrlr->osRes.device = device;

   /** Initialize PCI resources */
   vmkStatus = PciInit(ctrlr);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Failed to initialize pci resources, %s.",
                 vmk_StatusToString(vmkStatus));
      goto free_ctrlr;
   }

   /** Generate a unique name for this controller */
   vmk_NameFormat(&ctrlr->name, "nvme_pcie%02d%02d%02d%02d", ctrlr->osRes.sbdf.seg,
                  ctrlr->osRes.sbdf.bus, ctrlr->osRes.sbdf.dev, ctrlr->osRes.sbdf.fn);

   NVMEPCIEGetCtrlrCap(ctrlr);

   /** Initialize DMA facilities (dma engine, sg handle, etc.) */
   vmkStatus = DmaInit(ctrlr);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to initialize dma facilities, %s.",
             vmk_StatusToString(vmkStatus));
      goto cleanup_pci;
   }

   /** Initialize lock domain */
   vmk_StringFormat(domainName, sizeof(domainName), NULL,
                    "nvmePCIELockDom-%s",
                    NVMEPCIEGetCtrlrName(ctrlr));
   vmkStatus = NVMEPCIELockDomainCreate(domainName, &ctrlr->osRes.lockDomain);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to create lock Domain %s, %s.",
             domainName, vmk_StatusToString(vmkStatus));
      goto cleanup_dma;
   }

   /** Setup queue list */
   ctrlr->queueList = NVMEPCIEAlloc(sizeof(NVMEPCIEQueueInfo) * (NVME_PCIE_MAX_IO_QUEUES + 1), 0);
   if (ctrlr->queueList == NULL) {
      EPRINT(ctrlr, "Failed to allocate queue list.");
      vmkStatus = VMK_NO_MEMORY;
      goto cleanup_lockdomain;
   }

   /** Setup admin queue */
   vmkStatus = SetupAdminQueue(ctrlr);
   if (vmkStatus != VMK_OK) {
      goto free_queuelist;
   }

   /** Attach the controller instance to the device handle */
   vmkStatus = vmk_DeviceSetAttachedDriverData(device, ctrlr);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to set attached driver's data, %s.",
             vmk_StatusToString(vmkStatus));
      goto destroy_adminq;
   }

   /** Add this controller to the global list */
   vmk_SpinlockLock(NVME_PCIE_DRIVER_RES_LOCK);
   vmk_ListInsert(&ctrlr->list, vmk_ListAtRear(&NVME_PCIE_DRIVER_RES_CONTROLLER_LIST));
   vmk_SpinlockUnlock(NVME_PCIE_DRIVER_RES_LOCK);

   IPRINT(ctrlr, "Device %p attached.", device);
   return VMK_OK;

destroy_adminq:
   DestroyAdminQueue(ctrlr);
free_queuelist:
   NVMEPCIEFree(ctrlr->queueList);
cleanup_lockdomain:
   NVMEPCIELockDomainDestroy(ctrlr->osRes.lockDomain);
cleanup_dma:
   DmaCleanup(ctrlr);
cleanup_pci:
   PciCleanup(ctrlr);
free_ctrlr:
   NVMEPCIEFree(ctrlr);

   return vmkStatus;
}

/**
 * removeDevice callback of device ops
 */
static VMK_ReturnStatus
RemoveDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus;
   vmk_NvmeAdapter vmkAdapter;
   NVMEPCIEController *ctrlr;

   vmkStatus = vmk_DeviceGetRegistrationData(device,
                                             (vmk_AddrCookie *)&vmkAdapter);
   if (vmkStatus != VMK_OK || vmkAdapter == NULL) {
      MOD_EPRINT("failed to get logical device data, 0x%x.", vmkStatus);
      return VMK_BAD_PARAM;
   }

   ctrlr = (NVMEPCIEController *)vmk_NvmeGetAdapterDriverData(vmkAdapter);
   vmkStatus = vmk_DeviceUnregister(device);
   NVMEPCIEAdapterDestroy(ctrlr);
   ctrlr->osRes.logicalDevice = NULL;

   IPRINT(ctrlr, "Device %p removed.", device);

   return VMK_OK;
}

/**
 * device ops of the logical device (logical nvme device)
 */
static vmk_DeviceOps nvmePCIEDeviceOps = {
   .removeDevice = RemoveDevice,
};

/**
 * scanDevice callback of driver ops
 */
static VMK_ReturnStatus
ScanDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus;
   NVMEPCIEController *ctrlr;
   vmk_DeviceProps deviceProps;
   vmk_DeviceID deviceId;
   vmk_Name busName;
   vmk_BusType busType;

   MOD_IPRINT("Called with %p.", device);
   vmkStatus = vmk_DeviceGetAttachedDriverData(device,
                                               (vmk_AddrCookie *)&ctrlr);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Failed to get controller instance, %s.",
             vmk_StatusToString(vmkStatus));
      return vmkStatus;
   }

   /** Allocate and initialize vmk_NvmeAdapter */
   vmkStatus = NVMEPCIEAdapterInit(ctrlr);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to init nvme adapter, %s.",
             vmk_StatusToString(vmkStatus));
      return vmkStatus;
   }

   /* Create the logical device */
   vmk_NameInitialize(&busName, VMK_LOGICAL_BUS_NAME);
   vmk_BusTypeFind(&busName, &busType);
   vmkStatus = vmk_LogicalCreateBusAddress(NVME_PCIE_DRIVER_RES_DRIVER_HANDLE,
                                           device,
                                           0,
                                           &deviceId.busAddress,
                                           &deviceId.busAddressLen);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "failed to create logical bus address, 0x%x.", vmkStatus);
      goto destroy_adapter;
   }

   deviceId.busType = busType;
   deviceId.busIdentifier = VMK_NVME_DRIVER_BUS_ID;
   deviceId.busIdentifierLen = vmk_Strnlen(deviceId.busIdentifier,
                                           VMK_MISC_NAME_MAX);

   deviceProps.registeringDriver = NVME_PCIE_DRIVER_RES_DRIVER_HANDLE;
   deviceProps.deviceID = &deviceId;
   deviceProps.deviceOps = &nvmePCIEDeviceOps;
   deviceProps.registeringDriverData.ptr = ctrlr;
   deviceProps.registrationData.ptr = ctrlr->osRes.vmkAdapter;
   vmkStatus = vmk_DeviceRegister(&deviceProps,
                                  device,
                                  &ctrlr->osRes.logicalDevice);
   vmk_LogicalFreeBusAddress(NVME_PCIE_DRIVER_RES_DRIVER_HANDLE,
                             deviceId.busAddress);
   vmk_BusTypeRelease(deviceId.busType);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "failed to register logical device, 0x%x.", vmkStatus);
      goto destroy_adapter;
   }

   IPRINT(ctrlr, "Device %p scanned.", device);
   return VMK_OK;

destroy_adapter:
   NVMEPCIEAdapterDestroy(ctrlr);
   return vmkStatus;
}

/**
 * detachDevice callback of driver ops
 */
static VMK_ReturnStatus
DetachDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus;
   NVMEPCIEController *ctrlr;

   MOD_IPRINT("Called with %p.", device);
   vmkStatus = vmk_DeviceGetAttachedDriverData(device, (vmk_AddrCookie *)&ctrlr);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Failed to get controller instance, %s.", vmk_StatusToString(vmkStatus));
      return vmkStatus;
   }

   /** Remove the controller from the global list */
   vmk_SpinlockLock(NVME_PCIE_DRIVER_RES_LOCK);
   vmk_ListRemove(&ctrlr->list);
   vmk_SpinlockUnlock(NVME_PCIE_DRIVER_RES_LOCK);

   VMK_ASSERT(ctrlr->numIoQueues == 0);

   DestroyAdminQueue(ctrlr);
   NVMEPCIEFree(ctrlr->queueList);
   NVMEPCIELockDomainDestroy(ctrlr->osRes.lockDomain);
   DmaCleanup(ctrlr);
   PciCleanup(ctrlr);
   NVMEPCIEFree(ctrlr);

   MOD_IPRINT("Device %p detached.", device);
   return VMK_OK;
}

/**
 * quiesceDevice callback of driver ops
 */
static VMK_ReturnStatus
QuiesceDevice(vmk_Device device)
{
   NVMEPCIEController *ctrlr;
   VMK_ReturnStatus vmkStatus;
   NVMEPCIEQueueInfo *qinfo;
   vmk_NvmeQueueID qid;

   MOD_IPRINT("Called with %p", device);
   /* fix pr2314038. PSA expects there is not outgoing PSA command
    * before PSA gets detachDevice call, or PSA's detachDevice will return with
    * error.so it needs vmknvme or driver complete outgoing PSA comamnd actively.
    * in hotplug scenario, vmknvme needs a interface used to only flush IO queue
    * and doesn't access any nvme hardware resource. now nvme_pcie doesn't export
    * this kind of interface. so it needs to flush IO queue from nvme_pcie side.
    */
   vmkStatus = vmk_DeviceGetAttachedDriverData(device, (vmk_AddrCookie *)&ctrlr);
   if (VMK_OK != vmkStatus) {
      MOD_EPRINT("Failed to get controller instance, %s.",
                 vmk_StatusToString(vmkStatus));
      return vmkStatus;
   }
   if (VMK_TRUE == ctrlr->isRemoved) {
      // for hotplug case, flush IO queue actively
      MOD_IPRINT(" %d io queues to be flushed", ctrlr->numIoQueues);
      for (qid = 0; qid < ctrlr->numIoQueues; qid ++) {
         qinfo = &ctrlr->queueList[qid + 1];
         NVMEPCIESuspendQueue(qinfo);
         NVMEPCIEFlushQueue(qinfo, VMK_NVME_STATUS_VMW_QUIESCED);
      }
   }
   return VMK_OK;
}

/**
 * startDevice callback of driver ops
 */
static VMK_ReturnStatus
StartDevice(vmk_Device device)
{
   //TODO: start the controller in driver.
   MOD_IPRINT("Called with %p", device);
   return VMK_OK;
}

/**
 * forgetDevice callback of driver ops
 */
static void
ForgetDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus;
   NVMEPCIEController *ctrlr;

   MOD_IPRINT("Called with %p.", device);
   vmkStatus = vmk_DeviceGetAttachedDriverData(device, (vmk_AddrCookie *)&ctrlr);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Failed to get controller instance, %s.", vmk_StatusToString(vmkStatus));
      return;
   }

   ctrlr->isRemoved = VMK_TRUE;
   IPRINT(ctrlr, "Device %p forgotten.", device);
}

vmk_DriverOps nvmePCIEDriverOps = {
   .attachDevice = AttachDevice,
   .scanDevice = ScanDevice,
   .detachDevice = DetachDevice,
   .quiesceDevice = QuiesceDevice,
   .startDevice = StartDevice,
   .forgetDevice = ForgetDevice,
};

/**
 * Register driver
 *
 * This will update the module's global resource data.
 *
 * @return VMK_OK Driver registered successfully
 * @return VMK_EXISTS Driver already registered
 * @return Others Errors returned by vmk_DriverRegister
 */
VMK_ReturnStatus NVMEPCIEDriverRegister()
{
   vmk_DriverProps props;

   VMK_ASSERT(NVME_PCIE_DRIVER_RES_DRIVER_HANDLE == VMK_DRIVER_NONE);
   if (NVME_PCIE_DRIVER_RES_DRIVER_HANDLE != VMK_DRIVER_NONE) {
      return VMK_EXISTS;
   }

   props.moduleID = vmk_ModuleCurrentID;
   props.ops = &nvmePCIEDriverOps;
   props.privateData.ptr = NULL;
   vmk_NameInitialize(&props.name, NVME_PCIE_DRIVER_PROPS_DRIVER_NAME);

   return vmk_DriverRegister(&props, &(NVME_PCIE_DRIVER_RES_DRIVER_HANDLE));
}

/**
 * Unregister driver
 *
 * This will update the module's global resource data.
 */
void NVMEPCIEDriverUnregister()
{
   VMK_ASSERT(NVME_PCIE_DRIVER_RES_DRIVER_HANDLE != VMK_DRIVER_NONE);
   vmk_DriverUnregister(NVME_PCIE_DRIVER_RES_DRIVER_HANDLE);
   NVME_PCIE_DRIVER_RES_DRIVER_HANDLE = VMK_DRIVER_NONE;
}

/**
 * Enable bus-mastering for the device. See PR #1303185.
 *
 * @param[in] dev  PCI Device instance
 *
 * @return VMK_OK on success, error code otherwise
 */
static VMK_ReturnStatus
PciSetBusMaster(vmk_PCIDevice dev)
{
   vmk_uint32 pciCmd;
   VMK_ReturnStatus vmkStatus;
   vmkStatus = vmk_PCIReadConfig(vmk_ModuleCurrentID, dev,
                                 VMK_PCI_CONFIG_ACCESS_16,
                                 NVME_PCIE_REG_CMD, &pciCmd);

   if (vmkStatus != VMK_OK) {
       MOD_EPRINT("Unable to read PCI Command register, %s",
              vmk_StatusToString(vmkStatus));
       return vmkStatus;
    }

   pciCmd |= NVME_PCIE_REG_CMD_BME;

   vmkStatus = vmk_PCIWriteConfig(vmk_ModuleCurrentID, dev,
                                  VMK_PCI_CONFIG_ACCESS_16,
                                  NVME_PCIE_REG_CMD, pciCmd);

   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Unable to write PCI Command register, %s",
             vmk_StatusToString(vmkStatus));
      return vmkStatus;
   }

   MOD_IPRINT("Enabled bus-mastering on device.");
   return vmkStatus;
}

/**
 * Initialize pci layer resources
 *
 * @param[in] ctrlr  Controller instance
 *
 * @return VMK_OK on success, error code otherwise
 */
static VMK_ReturnStatus
PciInit(NVMEPCIEController *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   int bar;
   vmk_PCIResource pciRes[VMK_PCI_NUM_BARS];

   /** Get PCI device handle */
   vmkStatus = vmk_DeviceGetRegistrationData(ctrlr->osRes.device,
                                             (vmk_AddrCookie *)&ctrlr->osRes.pciDevice);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Invalid PCI device, %s.", vmk_StatusToString(vmkStatus));
      return vmkStatus;
   }

   /** Get PCI device's identifier */
   vmkStatus = vmk_PCIQueryDeviceID(ctrlr->osRes.pciDevice, &ctrlr->osRes.pciId);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Unable to get PCI device ID, %s.", vmk_StatusToString(vmkStatus));
      return vmkStatus;
   }

   /** Get PCI device's address */
   vmkStatus = vmk_PCIQueryDeviceAddr(ctrlr->osRes.pciDevice, &ctrlr->osRes.sbdf);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Unable to get PCI device address, %s.", vmk_StatusToString(vmkStatus));
      return vmkStatus;
   }

   /** Select and map PCI bar */
   vmkStatus = vmk_PCIQueryIOResources(ctrlr->osRes.pciDevice, VMK_PCI_NUM_BARS, pciRes);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Unable to get PCI device BARs information, %s.", vmk_StatusToString(vmkStatus));
      return vmkStatus;
   }

   for (bar = 0; bar < VMK_PCI_NUM_BARS; bar++) {
      if (((pciRes[bar].flags & VMK_PCI_BAR_FLAGS_IO) == 0) &&
         (pciRes[bar].size > 4096)) {
         MOD_IPRINT("Selected bar %d.", bar);
         ctrlr->bar = bar;
         ctrlr->barSize = pciRes[bar].size;
         break;
      }
   }
   if (bar == VMK_PCI_NUM_BARS) {
      MOD_EPRINT("Unable to find valid bar.");
      return VMK_NO_RESOURCES;
   }

   vmkStatus = vmk_PCIMapIOResource(vmk_ModuleCurrentID, ctrlr->osRes.pciDevice, ctrlr->bar,
                                    &ctrlr->osRes.pciResv, &ctrlr->regs);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Unable to map pci bar %d, %s", ctrlr->bar, vmk_StatusToString(vmkStatus));
      return vmkStatus;
   }

   /** Enable bus master */
   vmkStatus = PciSetBusMaster(ctrlr->osRes.pciDevice);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Unable to set bus-mastering on device, %s.", vmk_StatusToString(vmkStatus));
      return vmkStatus;
   }

   return VMK_OK;
}

/**
 * Undo all resource allocations done by PciInit
 *
 * @param[in] ctrlr  Controller instance
 *
 * @return VMK_OK on success, error code otherwise
 */
static VMK_ReturnStatus
PciCleanup(NVMEPCIEController *ctrlr)
{
   VMK_ReturnStatus vmkStatus;

   vmkStatus = vmk_PCIUnmapIOResource(vmk_ModuleCurrentID, ctrlr->osRes.pciDevice, ctrlr->bar);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Unable to unmap pci io resource, %s.", vmk_StatusToString(vmkStatus));
      /* Need to fall through */
   }

   ctrlr->regs = 0L;
   ctrlr->bar = VMK_PCI_NUM_BARS;  /* This should indicate an invalid bar */

   return vmkStatus;
}

/**
 * Initialize driver's DMA engine and Scatter-Gather Handle
 *
 * This DMA engine is for allocating DMA buffers for submission/completion
 * queues, etc., which is suitable for allocating large physically contiguous
 * buffers. IOs should use a separate DMA engine which has more constraints
 * than this engine.
 *
 * @param[in] ctrlr  Controller instance
 *
 * @return VMK_OK on success, error code otherwise
 */
static VMK_ReturnStatus
DmaInit(NVMEPCIEController *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   vmk_DMAEngineProps props;
   vmk_DMAConstraints constraints;

   /** Create dma engine */
   constraints.addressMask = VMK_ADDRESS_MASK_64BIT;
   constraints.maxTransfer = 32 * VMK_PAGE_SIZE;
   constraints.sgMaxEntries = 32;
   constraints.sgElemMaxSize = 0;
   constraints.sgElemSizeMult = 0;
   constraints.sgElemAlignment = VMK_PAGE_SIZE;
   constraints.sgElemStraddle = 0;

   props.module = vmk_ModuleCurrentID;
   props.flags = VMK_DMA_ENGINE_FLAGS_COHERENT;
   props.device = ctrlr->osRes.device;
   props.bounce = NULL;
   props.constraints = &constraints;
   vmk_NameInitialize(&props.name, "nvmePCIEDmaEngine");

   vmkStatus = vmk_DMAEngineCreate(&props, &ctrlr->osRes.dmaEngine);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "unable to create dma engine, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   /** Create SG handle */
   vmkStatus = vmk_SgCreateOpsHandle(NVME_PCIE_DRIVER_RES_HEAP_ID,
                                     &ctrlr->osRes.sgHandle,
                                     NULL, NULL);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "unable to create sg ops handle, 0x%x.", vmkStatus);
      goto destroy_dma;
   }

   return VMK_OK;

destroy_dma:
   vmk_DMAEngineDestroy(ctrlr->osRes.dmaEngine);
   ctrlr->osRes.dmaEngine = VMK_DMA_ENGINE_INVALID;

   return vmkStatus;
}

/**
 * Cleanup dma engine and SG handle.
 *
 * @param[in] ctrlr  Controller instance
 *
 * @return VMK_OK on success, error code otherwise
 */
static VMK_ReturnStatus
DmaCleanup(NVMEPCIEController *ctrlr)
{
   VMK_ReturnStatus vmkStatus;

   vmkStatus = vmk_SgDestroyOpsHandle(ctrlr->osRes.sgHandle);
   ctrlr->osRes.sgHandle = NULL;

   vmkStatus = vmk_DMAEngineDestroy(ctrlr->osRes.dmaEngine);
   ctrlr->osRes.dmaEngine = VMK_DMA_ENGINE_INVALID;

   return vmkStatus;
}

static VMK_ReturnStatus
SetupAdminQueue(NVMEPCIEController *ctrlr)
{
   VMK_ReturnStatus vmkStatus;

   vmkStatus = NVMEPCIEIntrAlloc(ctrlr, VMK_PCI_INTERRUPT_TYPE_MSIX, 1);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to allocate admin queue interrupt, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   vmkStatus = NVMEPCIEQueueCreate(ctrlr, 0, nvmePCIEAdminQueueSize);
   if (vmkStatus != VMK_OK) {
      EPRINT(ctrlr, "Failed to create admin queue, 0x%x.", vmkStatus);
      NVMEPCIEIntrFree(ctrlr);
   }

   return vmkStatus;
}

static void
DestroyAdminQueue(NVMEPCIEController *ctrlr)
{
   NVMEPCIEQueueDestroy(ctrlr, 0, VMK_NVME_STATUS_VMW_QUIESCED);
   NVMEPCIEIntrFree(ctrlr);
   return;
}
