/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (c) 2012-2013  Integrated Device Technology, Inc.
 * All rights reserved.
 *******************************************************************************/

/*
 * @file: nvme_driver.c --
 *
 *    Driver interface of native nvme driver
 */
#include "oslib.h"
#include "../../common/kernel/nvme_exc.h"
#include "../../common/kernel/nvme_drv_config.h"

static VMK_ReturnStatus
PciInit(struct NvmeCtrlr *ctrlr);
static VMK_ReturnStatus
PciCleanup(struct NvmeCtrlr *ctrlr);
static VMK_ReturnStatus
IntxSetup(struct NvmeCtrlr *ctrlr);

/**
 * attachDevice callback of driver ops
 */
static VMK_ReturnStatus
AttachDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;
   struct NvmeCtrlr *ctrlr;

   DPRINT_TEMP("enter.");

#if NVME_DEBUG_INJECT_STATE_DELAYS
   IPRINT("--ATTACH STARTED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   ctrlr = Nvme_Alloc(sizeof *ctrlr,
      VMK_L1_CACHELINE_SIZE,
      NVME_ALLOC_ZEROED);
   if (ctrlr == NULL) {
      return VMK_NO_MEMORY;
   }

   ctrlr->ctrlOsResources.device = device;

   /*
    * Task lists to attach an Nvme device to the driver
    * Some of them are done in nvme_driver and some are done in nvme_ctrlr.
    * nvme_driver should be responsible for any operations that are related to
    *             the OS layer, e.g. PCI bus brought up and bar mapping;
    * nvme_ctrlr  should be reponsible for Nvme controller related stuff, e.g.
    *             reg configurations, admin q, etc.
    *
    * Open questions: where do we put intr stuff?
    *
    * 1. Get PCI device handle
    * 2. PCI bar mapping
    * 3. Interrupt allocation and setup
    * 4. ??
    */

   vmkStatus = NvmeCtrlr_Attach(ctrlr);
   if (vmkStatus != VMK_OK) {
      goto free_ctrlr;
   }

   /* Attach to management instance here */
   vmkStatus = NvmeMgmt_CtrlrInitialize(ctrlr);
   if (vmkStatus != VMK_OK) {
      goto ctrlr_detach;
   }

   /* Attach the controller instance to the device handle */
   vmkStatus = vmk_DeviceSetAttachedDriverData(device, ctrlr);
   if (vmkStatus != VMK_OK) {
      goto mgmt_unreg;
   }

#if ALLOW_IOS_IN_QUIESCED_STATE == 1
/*
 * When this workaround switch is active,  we would be enabling the controller early
 * in AttachDevice instead of StartDevice.
 */
   vmkStatus = NvmeCtrlr_Start(ctrlr);
   if (vmkStatus != VMK_OK) {
      goto mgmt_unreg;
   }

#endif

   /** Add this adaper to the global list */
   vmk_SpinlockLock(NVME_DRIVER_RES_LOCK);
   vmk_ListInsert(&ctrlr->list, vmk_ListAtRear(&NVME_DRIVER_RES_ADAPTERLIST));
   vmk_SpinlockUnlock(NVME_DRIVER_RES_LOCK);

   DPRINT_CTRLR("attached driver data %p.", ctrlr);


#if NVME_DEBUG_INJECT_STATE_DELAYS
   IPRINT("--ATTACH COMPLETED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   return VMK_OK;

mgmt_unreg:
   NvmeMgmt_CtrlrDestroy(ctrlr);

ctrlr_detach:
   NvmeCtrlr_Detach(ctrlr);

free_ctrlr:
   Nvme_Free(ctrlr);
   return vmkStatus;
}


/**
 * removeDevice callback of device ops
 */
static VMK_ReturnStatus
DriverRemoveDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus;
   vmk_ScsiAdapter *adapter;
   struct NvmeCtrlr *ctrlr;

   DPRINT_TEMP("enter");

   vmkStatus = vmk_DeviceGetRegistrationData(device, (vmk_AddrCookie *)&adapter);
   if (vmkStatus != VMK_OK || adapter == NULL) {
      EPRINT("failed to get logical device data, 0x%x.", vmkStatus);
      return VMK_BAD_PARAM;
   }

   ctrlr = (struct NvmeCtrlr *)adapter->clientData;

   vmkStatus = vmk_DeviceUnregister(device);
   IPRINT("removed logical device, 0x%x.", vmkStatus);

   vmkStatus = NvmeScsi_Destroy(ctrlr);
   IPRINT("cleaned up scsi layer, 0x%x.", vmkStatus);

   ctrlr->ctrlOsResources.logicalDevice = NULL;

   return VMK_OK;
}


/**
 * device ops of the logical device (logical SCSI device)
 */
static vmk_DeviceOps __deviceOps = {
   .removeDevice = DriverRemoveDevice,
};


/**
 * scanDevice callback of driver ops
 */
static VMK_ReturnStatus
ScanDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeCtrlr *ctrlr;
   vmk_DeviceProps deviceProps;
   vmk_DeviceID deviceId;
   vmk_Name busName;
   vmk_BusType busType;

   DPRINT_TEMP("enter.");

#if NVME_DEBUG_INJECT_STATE_DELAYS
   IPRINT("--SCAN STARTED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   vmkStatus = vmk_DeviceGetAttachedDriverData(device, (vmk_AddrCookie *)&ctrlr);
   if (vmkStatus != VMK_OK) {
      EPRINT("failed to get controller instance, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   vmkStatus = NvmeScsi_Init(ctrlr);
   if (vmkStatus != VMK_OK) {
      EPRINT("failed to initialize scsi layer, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   /* Create the logical device */
   vmk_NameInitialize(&busName, VMK_LOGICAL_BUS_NAME);
   vmk_BusTypeFind(&busName, &busType);
   vmkStatus = vmk_LogicalCreateBusAddress(NVME_DRIVER_RES_DRIVER_HANDLE, device, 0,
      &deviceId.busAddress, &deviceId.busAddressLen);
   if (vmkStatus != VMK_OK) {
      EPRINT("failed to create logical bus address, 0x%x.", vmkStatus);
      goto out;
   }

   deviceId.busType = busType;
   deviceId.busIdentifier = VMK_SCSI_PSA_DRIVER_BUS_ID;
   deviceId.busIdentifierLen = vmk_Strnlen(deviceId.busIdentifier, VMK_MISC_NAME_MAX);

   deviceProps.registeringDriver = NVME_DRIVER_RES_DRIVER_HANDLE;
   deviceProps.deviceID = &deviceId;
   deviceProps.deviceOps = &__deviceOps;
   deviceProps.registeringDriverData.ptr = ctrlr;
   deviceProps.registrationData.ptr = ctrlr->ctrlOsResources.scsiAdapter;

   vmkStatus = vmk_DeviceRegister(&deviceProps, device, &ctrlr->ctrlOsResources.logicalDevice);
   vmk_LogicalFreeBusAddress(NVME_DRIVER_RES_DRIVER_HANDLE, deviceId.busAddress);
   vmk_BusTypeRelease(deviceId.busType);
   if (vmkStatus != VMK_OK) {
      EPRINT("failed to register logical device, 0x%x.", vmkStatus);
      goto out;
   }

#if NVME_DEBUG_INJECT_STATE_DELAYS
   IPRINT("--SCAN COMPLETED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   return VMK_OK;

out:
   NvmeScsi_Destroy(ctrlr);
   return vmkStatus;
}


/**
 * detachDevice callback of driver ops
 */
static VMK_ReturnStatus
DetachDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeCtrlr *ctrlr;

   DPRINT_TEMP("enter.");

#if NVME_DEBUG_INJECT_STATE_DELAYS
   IPRINT("--DETACH STARTED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   vmkStatus = vmk_DeviceGetAttachedDriverData(device, (vmk_AddrCookie *)&ctrlr);
   if (vmkStatus != VMK_OK) {
      EPRINT("failed to get controller instance, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   /** Remote the adapter from the global list */
   vmk_SpinlockLock(NVME_DRIVER_RES_LOCK);
   vmk_ListRemove(&ctrlr->list);
   vmk_SpinlockUnlock(NVME_DRIVER_RES_LOCK);

   /* Destroy the management handle */
   NvmeMgmt_CtrlrDestroy(ctrlr);

#if ALLOW_IOS_IN_QUIESCED_STATE
/*
 * Defer putting the controller in an idle state until
 * device driver is dettached.
 */
   vmkStatus = NvmeCtrlr_Stop(ctrlr);
#endif

   /* Controller should have been quiesced before destruction.
    * Destruction is handled by nvme_ctrlr, which executes opposite operations
    * from NvmeCtrlr_Attach.
    */
   vmkStatus = NvmeCtrlr_Detach(ctrlr);
   DPRINT_CTRLR("nvme controller %p destructed, 0x%x.", ctrlr, vmkStatus);

   /* Should never reference ctrlr after detach. */
   Nvme_Free(ctrlr);

#if NVME_DEBUG_INJECT_STATE_DELAYS
   IPRINT("--DETACH COMPLETED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   return VMK_OK;
}


/**
 * quiesceDevice callback of driver ops
 */
static VMK_ReturnStatus
QuiesceDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeCtrlr *ctrlr;

   DPRINT_TEMP("enter.");

#if NVME_DEBUG_INJECT_STATE_DELAYS
   IPRINT("--QUIESCE STARTED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   vmkStatus = vmk_DeviceGetAttachedDriverData(device, (vmk_AddrCookie *)&ctrlr);
   if (vmkStatus != VMK_OK) {
      EPRINT("failed to get controller instance, 0x%x.", vmkStatus);
      return vmkStatus;
   }

#if EXC_HANDLER
   vmkStatus = NvmeExc_SignalExceptionAndWait(ctrlr,  NVME_EXCEPTION_QUIESCE,  TASKMGMT_TIMEOUT);
#else
#if ALLOW_IOS_IN_QUIESCED_STATE
   vmkStatus = NvmeCtrlr_Quiesce(ctrlr);
#else
   vmkStatus = NvmeCtrlr_Stop(ctrlr);
#endif
#endif

#if NVME_DEBUG_INJECT_STATE_DELAYS
   IPRINT("--QUIESCE COMPLETED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   return vmkStatus;
}


/**
 * startDevice callback of driver ops
 */
static VMK_ReturnStatus
StartDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeCtrlr *ctrlr;

   DPRINT_TEMP("enter.");

#if NVME_DEBUG_INJECT_STATE_DELAYS
   IPRINT("--START STARTED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   vmkStatus = vmk_DeviceGetAttachedDriverData(device, (vmk_AddrCookie *)&ctrlr);
   if (vmkStatus != VMK_OK) {
      EPRINT("failed to get controller instance, 0x%x.", vmkStatus);
      return vmkStatus;
   }

#if ALLOW_IOS_IN_QUIESCED_STATE == 0
#if EXC_HANDLER
   vmkStatus = NvmeExc_SignalExceptionAndWait(ctrlr,  NVME_EXCEPTION_TASK_START,  TASKMGMT_TIMEOUT);
#else
/*
 * When this workaround switch is active,  we would be enabling the controller early.
 * in AttachDevice instead of StartDevice.
 */
   vmkStatus = NvmeCtrlr_Start(ctrlr);
#endif
#endif

#if NVME_DEBUG_INJECT_STATE_DELAYS
   IPRINT("--START COMPLETED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   return vmkStatus;
}


/**
 * forgetDevice callback of driver ops
 */
static void
ForgetDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeCtrlr *ctrlr;

   DPRINT_TEMP("enter.");

#if NVME_DEBUG_INJECT_STATE_DELAYS
   IPRINT("--FORGET STARTED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   vmkStatus = vmk_DeviceGetAttachedDriverData(device, (vmk_AddrCookie *)&ctrlr);
   if (vmkStatus != VMK_OK) {
      EPRINT("failed to get controller instance, 0x%x.", vmkStatus);
   }
#if EXC_HANDLER
   vmkStatus = NvmeExc_SignalExceptionAndWait(ctrlr,  NVME_EXCEPTION_DEVICE_REMOVED,  TASKMGMT_TIMEOUT);
#else
   NvmeCtrlr_SetMissing(ctrlr);
#endif

#if NVME_DEBUG_INJECT_STATE_DELAYS
   IPRINT("--FORGET COMPLETED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

}


/**
 * driver ops used to register nvme driver.
 */
static vmk_DriverOps __driverOps = {
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
 * @return VMK_OK driver registration successful
 * @return VMK_EXISTS driver already registered
 */
VMK_ReturnStatus
NvmeDriver_Register()
{
   VMK_ReturnStatus vmkStatus;
   vmk_DriverProps props;

   DPRINT_TEMP("enter.");

   VMK_ASSERT(NVME_DRIVER_RES_DRIVER_HANDLE == VMK_DRIVER_NONE);
   if (NVME_DRIVER_RES_DRIVER_HANDLE != VMK_DRIVER_NONE) {
      return VMK_EXISTS;
   }

   props.moduleID = vmk_ModuleCurrentID;
   props.ops = &__driverOps; // defined and exported from nvme_driver.c
   props.privateData.ptr = NULL;
   vmk_NameInitialize(&props.name, NVME_DRIVER_PROPS_DRIVER_NAME);

   vmkStatus = vmk_DriverRegister(&props, &(NVME_DRIVER_RES_DRIVER_HANDLE));

   return vmkStatus;
}


/**
 * Unregister driver.
 *
 * This will update the module's global resource data.
 */
void
NvmeDriver_Unregister()
{
   DPRINT_TEMP("enter.");

   VMK_ASSERT(NVME_DRIVER_RES_DRIVER_HANDLE != VMK_DRIVER_NONE);

   vmk_DriverUnregister(NVME_DRIVER_RES_DRIVER_HANDLE);
   NVME_DRIVER_RES_DRIVER_HANDLE = VMK_DRIVER_NONE;
}

/**
 * Allocate and setup MSI-X interrupt handlers
 *
 * @param [in] ctrlr controller instance
 */
static VMK_ReturnStatus
MsixSetup(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   vmk_uint32 numQueues = 0;
   vmk_uint32 numAllocated;

   /* num io queues is determined by how many completion queues SCSI layer supports */
   /* plus 1 for admin queue */
   numQueues = OsLib_GetMaxNumQueues() + 1;

   ctrlr->ctrlOsResources.intrArray = Nvme_Alloc(sizeof(vmk_IntrCookie) * numQueues, 0, NVME_ALLOC_ZEROED);
   if (ctrlr->ctrlOsResources.intrArray == NULL) {
      ctrlr->ctrlOsResources.msixEnabled = 0;
      return VMK_NO_MEMORY;
   }

   vmkStatus = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
      ctrlr->ctrlOsResources.pciDevice, VMK_PCI_INTERRUPT_TYPE_MSIX,
      numQueues, /* num desired */
      2, /* num required, 1 for admin and 1 for io */
      NULL, /* index array, not needed */
      ctrlr->ctrlOsResources.intrArray,
      &numAllocated);

   if (vmkStatus == VMK_OK) {
      VPRINT("Allocated %d msi-x vectors.", numAllocated);
      ctrlr->numIoQueues = numAllocated - 1; /* minus 1 admin q */
      ctrlr->ctrlOsResources.numVectors = numAllocated;
      ctrlr->ctrlOsResources.msixEnabled = 1;

      return VMK_OK;
   } else {
      Nvme_Free(ctrlr->ctrlOsResources.intrArray);
      ctrlr->ctrlOsResources.intrArray = NULL;
      ctrlr->ctrlOsResources.msixEnabled = 0;
      return vmkStatus;
   }
}


/**
 * Initialize interrupt handler.
 *
 * We will first try MSI-X, if MSI-X allocation is not successful, then
 * fallback to legacy intx.
 *
 * If MSI-X is used, the actual interrupt handler is NOT registered, until
 * qpair construct time.
 *
 * @param [in] ctrlr controller instance
 */
static VMK_ReturnStatus
IntrInit(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;

   /* try msi-x first, if nvme_force_intx is not set. */
   if (!nvme_force_intx) {
      vmkStatus = MsixSetup(ctrlr);
      if (vmkStatus == VMK_OK) {
         IPRINT("using msi-x with %d vectors.", ctrlr->ctrlOsResources.numVectors);
         return VMK_OK;
      }
      /* The device is probably broken or unplugged. Return error directly. */
      if (vmkStatus == VMK_IO_ERROR) {
         goto out;
      }
   }

   /* msi-x setup failed, fallback to intx */
   vmkStatus = IntxSetup(ctrlr);
   if (vmkStatus == VMK_OK) {
      IPRINT("using intx.");
      return VMK_OK;
   }

out:
   EPRINT("Unable to initialize interrupt, 0x%x.", vmkStatus);
   return vmkStatus;
}


/**
 * Cleanup [in] interrupt resources
 */
static VMK_ReturnStatus
IntrCleanup(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;

   /* if using intx, needs to unregister intr handler first. */
   if (!ctrlr->ctrlOsResources.msixEnabled) {
      vmkStatus = OsLib_IntrUnregister(*ctrlr->ctrlOsResources.intrArray, ctrlr);
      DPRINT_INIT("unregistered intr handler for intx, 0x%x.", vmkStatus);
   }

   vmkStatus = vmk_PCIFreeIntrCookie(vmk_ModuleCurrentID, ctrlr->ctrlOsResources.pciDevice);
   DPRINT_INIT("freed intr cookies, 0x%x.", vmkStatus);

   /* lastly, free the intr cookie array */
   Nvme_Free(ctrlr->ctrlOsResources.intrArray);
   ctrlr->ctrlOsResources.intrArray = NULL;
   ctrlr->ctrlOsResources.msixEnabled = 0;
   ctrlr->ctrlOsResources.numVectors = 0;

   return VMK_OK;
}


/**
 * Create slab for scsi unmap command
 */
static VMK_ReturnStatus
CreateScsiUnmapSlab(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;
   vmk_SlabCreateProps unmapSlabProps;
   vmk_Memset(&unmapSlabProps, 0, sizeof(vmk_SlabCreateProps));
   unmapSlabProps.type = VMK_SLAB_TYPE_SIMPLE;
   vmk_NameFormat(&unmapSlabProps.name, "unmap_slab_%s", Nvme_GetCtrlrName(ctrlr));
   unmapSlabProps.module = vmk_ModuleCurrentID;
   unmapSlabProps.objSize = max_t(vmk_ByteCountSmall, sizeof(nvme_ScsiUnmapParameterList), sizeof(struct nvme_dataset_mgmt_data)*NVME_MAX_DSM_RANGE);
   unmapSlabProps.alignment = VMK_L1_CACHELINE_SIZE;
   unmapSlabProps.ctrlOffset = 0;
   unmapSlabProps.minObj = max_scsi_unmap_requests;
   unmapSlabProps.maxObj = max_scsi_unmap_requests*2;

   vmkStatus = vmk_SlabCreate(&unmapSlabProps, &ctrlr->scsiUnmapSlabId);
   if (vmkStatus != VMK_OK) {
      EPRINT("Unable to create slab for scsi unmap. vmkStatus: 0x%x.", vmkStatus);
      return vmkStatus;
   }
   vmk_AtomicWrite64(&ctrlr->activeUnmaps, 0);
   vmk_AtomicWrite64(&ctrlr->maxUnmaps, 0);
   return vmkStatus;
}

/**
 * Attach and bring up controller
 *
 * Allocate controller related resources
 *
 * @param [in] ctrlr controller instance
 */
VMK_ReturnStatus
NvmeCtrlr_Attach(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   char lockName[VMK_MISC_NAME_MAX];

   /**
    * Set initial state.
    *
    * Note: lock is not initialized by here, so do not use locking.
    */
   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_INIT, VMK_FALSE);

   /**
    * Initialize PCI resources first to access controller bars.
    *
    * Note: has to initialize PCI resource first, all the following operations
    *       require BARs to be mapped already.
    */
   vmkStatus = PciInit(ctrlr);
   if (vmkStatus != VMK_OK) {
      return vmkStatus;
   }

   vmkStatus = NvmeCtrlr_ValidateParams(ctrlr);
   if (vmkStatus != VMK_OK) {
      goto cleanup_pci;
   }

   /* Initialize DMA facilities (dma engine, sg handle, etc.) */
   vmkStatus = OsLib_DmaInit(&ctrlr->ctrlOsResources);
   if (vmkStatus != VMK_OK) {
      goto cleanup_pci;
   }

   /* Initialize interrupt */
   vmkStatus = IntrInit(ctrlr);
   if (vmkStatus != VMK_OK) {
      goto cleanup_dma;
   }

   /* Initialize lock domain for locks within this controller */
   vmkStatus = OsLib_LockDomainCreate(&ctrlr->ctrlOsResources,
			Nvme_GetCtrlrName(ctrlr));
   if (vmkStatus != VMK_OK) {
      goto cleanup_intr;
   }

   /* Initialize lock */
   vmk_StringFormat(lockName, sizeof(lockName), NULL, "%s-lock",
                    Nvme_GetCtrlrName(ctrlr));
   vmkStatus = OsLib_LockCreate(&ctrlr->ctrlOsResources, NVME_LOCK_RANK_LOW,
      lockName, &ctrlr->lock);
   if (vmkStatus != VMK_OK) {
      goto cleanup_lockdomain;
   }

   /* Initialize task management mutex */
   vmk_StringFormat(lockName, sizeof(lockName), NULL, "%s-mutex",
                    Nvme_GetCtrlrName(ctrlr));
   vmkStatus = OsLib_SemaphoreCreate(lockName, 1, &ctrlr->taskMgmtMutex);
   if (vmkStatus != VMK_OK) {
      goto cleanup_lock;
   }

#if USE_TIMER
   vmkStatus = OsLib_TimerQueueCreate(ctrlr);
   if (vmkStatus != VMK_OK) {
      goto cleanup_sema;
   }
#endif


#if EXC_HANDLER
  vmkStatus = OsLib_SetupExceptionHandler(ctrlr);
   if (vmkStatus != VMK_OK) {
      EPRINT("The device can not handle exceptions.");
      #if USE_TIMER
         goto cleanup_timer_queue;
      #else
         goto cleanup_sema;
      #endif
   }
#endif

   /*
    * TODO: Initialize and kick off timers and kernel thread
    */
#if 0
   vmkStatus = NvmeCtrlr_CreateLogWorld(ctrlr);
   if (vmkStatus != VMK_OK) {
      goto cleanup_intr;
   }
#endif

   vmkStatus = CreateScsiUnmapSlab(ctrlr);
   if (vmkStatus != VMK_OK) {
      #if EXC_HANDLER
         goto cleanup_exc_handler;
      #elif USE_TIMER
         goto cleanup_timer_queue;
      #else
         goto cleanup_sema;
      #endif
   }

#if NVME_MUL_COMPL_WORLD
   vmkStatus = OsLib_StartCompletionWorlds(ctrlr);
   if (vmkStatus != VMK_OK) {
      EPRINT("Failed to create completion worlds. vmkStatus: 0x%x.",  \
            vmkStatus);
      goto cleanup_unmap_slab;

   }
#endif

   vmkStatus = NvmeCtrlr_AdminQueueSetup(ctrlr);
   if (vmkStatus != VMK_OK) {
#if NVME_MUL_COMPL_WORLD
      goto cleanup_compl_worlds;
#else
      goto cleanup_unmap_slab;
#endif
   }

   #if NVME_DEBUG_INJECT_ERRORS
      NvmeDebug_ErrorInjectInit(ctrlr->errCounters);
   #endif

   /**
    * Initialize all other essential members
    */
   vmk_ListInit(&ctrlr->nsList);

   return VMK_OK;

#if NVME_MUL_COMPL_WORLD
cleanup_compl_worlds:
   OsLib_EndCompletionWorlds(ctrlr);
#endif

cleanup_unmap_slab:
   vmk_SlabDestroy(ctrlr->scsiUnmapSlabId);


#if EXC_HANDLER
cleanup_exc_handler:
   OsLib_ShutdownExceptionHandler(ctrlr);
#endif

#if USE_TIMER
cleanup_timer_queue:
   OsLib_TimerQueueDestroy(ctrlr);
#endif

cleanup_sema:
   OsLib_SemaphoreDestroy(&ctrlr->taskMgmtMutex);

cleanup_lock:
   OsLib_LockDestroy(&ctrlr->lock);

cleanup_lockdomain:
   OsLib_LockDomainDestroy(&ctrlr->ctrlOsResources);

cleanup_intr:
   IntrCleanup(ctrlr);

cleanup_dma:
   OsLib_DmaCleanup(&ctrlr->ctrlOsResources);

cleanup_pci:
   PciCleanup(ctrlr);

   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_FAILED, VMK_FALSE);

   DPRINT_CTRLR("failed to attach controller, 0x%x.", vmkStatus);

   return vmkStatus;
}


/**
 * Setup INTx mode interrupt handler
 *
 * @param [in] ctrlr controller instance
 */
static VMK_ReturnStatus
IntxSetup(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   vmk_uint32 numAllocated;

   ctrlr->ctrlOsResources.intrArray = Nvme_Alloc(sizeof(vmk_IntrCookie), 0, NVME_ALLOC_ZEROED);
   if (ctrlr->ctrlOsResources.intrArray == NULL) {
      return VMK_NO_MEMORY;
   }

   vmkStatus = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
      ctrlr->ctrlOsResources.pciDevice, VMK_PCI_INTERRUPT_TYPE_LEGACY,
      1, 1, NULL, ctrlr->ctrlOsResources.intrArray, &numAllocated);
   if (vmkStatus != VMK_OK) {
      EPRINT("unable to allocate intr cookie, 0x%x.", vmkStatus);
      goto free_intrArray;
   }

   /* should have just 1 intr cookie allocated for intx */
   VMK_ASSERT(numAllocated == 1);

   ctrlr->ctrlOsResources.msixEnabled = 0;
   ctrlr->numIoQueues = 1;
   ctrlr->ctrlOsResources.numVectors = 1; /* 1 intx for both admin and io */

   /* for intx mode, we should register intr handler here rather than
    * at individual queue creation time.
    */
   vmkStatus = OsLib_IntrRegister(ctrlr->ctrlOsResources.device, ctrlr->ctrlOsResources.intrArray[0],
      ctrlr, /* for intx handler, the data is the controller itself */
      0, /* use default id 0 */
      NvmeCtrlr_IntxAck, NvmeCtrlr_IntxHandler);
   if (vmkStatus != VMK_OK) {
      EPRINT("unable to register intr handler, 0x%x.", vmkStatus);
      goto free_intr;
   }

   return VMK_OK;

free_intr:
   vmk_PCIFreeIntrCookie(vmk_ModuleCurrentID, ctrlr->ctrlOsResources.pciDevice);

free_intrArray:
   Nvme_Free(ctrlr->ctrlOsResources.intrArray);
   ctrlr->ctrlOsResources.intrArray = NULL;
   ctrlr->ctrlOsResources.numVectors = 0;
   ctrlr->numIoQueues = 0;

   return vmkStatus;
}

/**
 * NvmeCtrlr_Detach - tear down controller.
 *
 * @param [in] ctrlr controller instance
 */
VMK_ReturnStatus
NvmeCtrlr_Detach(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;

   NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_DETACHED, VMK_TRUE);
#if EXC_HANDLER
   OsLib_ShutdownExceptionHandler(ctrlr);
#endif

#if USE_TIMER
   OsLib_TimerQueueDestroy(ctrlr);
#endif

   vmkStatus = NvmeCtrlr_AdminQueueDestroy(ctrlr);
   DPRINT_INIT("cleaned admin queue, 0x%x.", vmkStatus);

#if NVME_MUL_COMPL_WORLD
   OsLib_EndCompletionWorlds(ctrlr);
   DPRINT_INIT("cleaned IO completion worlds, 0x%x.", vmkStatus);
#endif

   vmkStatus = vmk_SlabDestroy(ctrlr->scsiUnmapSlabId);
   DPRINT_INIT("cleaned scsi unmap slab, 0x%x.", vmkStatus);

   vmkStatus = OsLib_SemaphoreDestroy(&ctrlr->taskMgmtMutex);
   DPRINT_INIT("cleaned task management mutex, 0x%x.", vmkStatus);

   vmkStatus = OsLib_LockDestroy(&ctrlr->lock);
   DPRINT_INIT("cleaned up lock, 0x%x.", vmkStatus);

   vmkStatus = OsLib_LockDomainDestroy(&ctrlr->ctrlOsResources);
   DPRINT_INIT("cleaned up lock domain, 0x%x.", vmkStatus);

   vmkStatus = IntrCleanup(ctrlr);
   DPRINT_INIT("cleaned up intr, 0x%x.", vmkStatus);

   vmkStatus = OsLib_DmaCleanup(&ctrlr->ctrlOsResources);
   DPRINT_INIT("cleaned up dma, 0x%x.", vmkStatus);

   vmkStatus = PciCleanup(ctrlr);
   DPRINT_INIT("cleaned up pci, 0x%x.", vmkStatus);

   return VMK_OK;
}

/**
 * Undo all resource allocations done by PciInit.
 *
 * @param [in] ctrlr controller instance
 */
static VMK_ReturnStatus
PciCleanup(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;

   vmkStatus = vmk_PCIUnmapIOResource(vmk_ModuleCurrentID, ctrlr->ctrlOsResources.pciDevice,
      ctrlr->bar);
   if (vmkStatus != VMK_OK) {
      EPRINT("unable to unmap pci io resource, 0x%x.", vmkStatus);
      /* Need to fall through */
   }

   ctrlr->regs = 0L;
   ctrlr->bar = VMK_PCI_NUM_BARS;  /* This should indicate an invalid bar */

   return VMK_OK;
}


/**
 * Enable bus-mastering for the device. See PR #1303185.
 *
 * @param [in] PCI Device instance
 */
static VMK_ReturnStatus
PciSetMaster(vmk_PCIDevice dev)
{
   vmk_uint32 pciCmd;
   VMK_ReturnStatus vmkStatus;
   vmkStatus = vmk_PCIReadConfig(vmk_ModuleCurrentID, dev,
                                 VMK_PCI_CONFIG_ACCESS_16,
                                 NVME_PCI_CMDREG_OFFSET, &pciCmd);

   if (vmkStatus != VMK_OK) {
       EPRINT("Unable to read PCI Command register (%s)",
              vmk_StatusToString(vmkStatus));
       return vmkStatus;
    }

   pciCmd |= NVME_PCI_CMD_BUSMASTER;

   vmkStatus = vmk_PCIWriteConfig(vmk_ModuleCurrentID, dev,
                                  VMK_PCI_CONFIG_ACCESS_16,
                                  NVME_PCI_CMDREG_OFFSET, pciCmd);

   if (vmkStatus != VMK_OK) {
      EPRINT("Unable to write PCI Command register (%s)",
             vmk_StatusToString(vmkStatus));
      return vmkStatus;
   }

   IPRINT("Enabled bus-mastering on device.");
   return vmkStatus;
}


/**
 * Initialize pci layer resources for a controller
 *
 * @param [in] ctrlr controller instance
 */
static VMK_ReturnStatus
PciInit(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   int bar;
   vmk_PCIResource pciRes[VMK_PCI_NUM_BARS];

   /* First, get pci device handle and id information for reference */
   vmkStatus = vmk_DeviceGetRegistrationData(ctrlr->ctrlOsResources.device,
      (vmk_AddrCookie *)&ctrlr->ctrlOsResources.pciDevice);
   if (vmkStatus != VMK_OK) {
      EPRINT("invalid pci device, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   vmkStatus = vmk_PCIQueryDeviceID(ctrlr->ctrlOsResources.pciDevice, &ctrlr->ctrlOsResources.pciId);
   if (vmkStatus != VMK_OK) {
      EPRINT("unable to get device id, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   vmkStatus = vmk_PCIQueryDeviceAddr(ctrlr->ctrlOsResources.pciDevice, &ctrlr->ctrlOsResources.sbdf);
   if (vmkStatus != VMK_OK) {
      EPRINT("unable to get device address, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   /* Select and map pci bars */
   vmkStatus = vmk_PCIQueryIOResources(ctrlr->ctrlOsResources.pciDevice, VMK_PCI_NUM_BARS, pciRes);
   if (vmkStatus != VMK_OK) {
      EPRINT("unable to query io resource, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   for (bar = 0; bar < VMK_PCI_NUM_BARS; bar++) {
      if (((pciRes[bar].flags & VMK_PCI_BAR_FLAGS_IO) == 0) &&
         (pciRes[bar].size > 4096)) {
         IPRINT("selected bar %d.", bar);
         ctrlr->bar = bar;
         ctrlr->barSize = pciRes[bar].size;
         break;
      }
   }
   if (bar == VMK_PCI_NUM_BARS) {
      EPRINT("unable to find valid bar.");
      return VMK_NO_RESOURCES;
   }

   vmkStatus = vmk_PCIMapIOResource(vmk_ModuleCurrentID,
      ctrlr->ctrlOsResources.pciDevice, ctrlr->bar, &ctrlr->ctrlOsResources.pciResv, &ctrlr->regs);
   if (vmkStatus != VMK_OK) {
      EPRINT("unable to map pci bar %d, 0x%x", ctrlr->bar, vmkStatus);
      return vmkStatus;
   }

   vmkStatus = PciSetMaster(ctrlr->ctrlOsResources.pciDevice);
   if (vmkStatus != VMK_OK) {
      EPRINT("unable to set the bus-mastering on device, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   /* Generate a unique name for the controller */
   vmk_NameFormat(&ctrlr->name, "nvme%02d%02d%02d%02d",
      ctrlr->ctrlOsResources.sbdf.seg, ctrlr->ctrlOsResources.sbdf.bus, ctrlr->ctrlOsResources.sbdf.dev, ctrlr->ctrlOsResources.sbdf.fn);

   /* Everything at PCI layer should have been initialized. */
   return VMK_OK;
}
