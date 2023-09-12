/*****************************************************************************
 * Copyright (c) 2016-2023 VMware, Inc. All rights reserved.
 *****************************************************************************/

/*
 * @file: nvme_pcie_module.c --
 *
 *   Entry point for native nvme_pcie driver.
 */

#include "nvme_pcie_int.h"
#include "nvme_pcie_mgmt.h"

static int nvmePCIELogLevel = NVME_LOG_LEVEL_INFO;
VMK_MODPARAM(nvmePCIELogLevel, int, "NVMe PCIe driver log level");

int nvmePCIEDebugMask = 0;
VMK_MODPARAM(nvmePCIEDebugMask, int, "NVMe PCIe driver debug mask");

int nvmePCIEDma4KSwitch = 0;
VMK_MODPARAM(nvmePCIEDma4KSwitch, int, "NVMe PCIe 4k-alignment DMA");

int nvmePCIEMsiEnbaled = 0;
VMK_MODPARAM(nvmePCIEMsiEnbaled, int, "NVMe PCIe MSI interrupt enable");

vmk_uint32 nvmePCIEFakeAdminQSize = 0;
VMK_MODPARAM(nvmePCIEFakeAdminQSize, uint, "NVMe PCIe fake ADMIN queue size. 0's based");

#if NVME_PCIE_STORAGE_POLL
int nvmePCIEPollAct = 1;
VMK_MODPARAM(nvmePCIEPollAct, int, "NVMe PCIe hybrid poll activate,"
                                   " MSIX interrupt must be enabled."
                                   " Default activated.");

vmk_uint32 nvmePCIEPollOIOThr = 30;
VMK_MODPARAM(nvmePCIEPollOIOThr, uint, "NVMe PCIe hybrid poll OIO threshold of"
                                       " automatic switch from interrupt to"
                                       " poll. Valid if poll activated. Default"
                                       " 30 OIO commands per IO queue.");

vmk_uint64 nvmePCIEPollInterval = 50;
VMK_MODPARAM(nvmePCIEPollInterval, uint, "NVMe PCIe hybrid poll interval"
                                         " between each poll in microseconds."
                                         " Valid if poll activated. Default"
                                         " 50us.");

#if NVME_PCIE_BLOCKSIZE_AWARE
int nvmePCIEBlkSizeAwarePollAct = 1;
VMK_MODPARAM(nvmePCIEBlkSizeAwarePollAct, int, "NVMe PCIe block size aware"
                                               " poll activate. Valid if poll"
                                               " activated. Default activated.");
#endif
#endif

extern int nvmePCIEAdminQueueSize;
/**
 * Global, static data that holds module/driver wide resources
 */
NVMEPCIEDriverResource __nvmePCIEdriverResource = {
   .heapId = VMK_INVALID_HEAP_ID,
   .driverHandle = VMK_DRIVER_NONE,
   .logHandle = VMK_INVALID_LOG_HANDLE,
   .memPool = VMK_MEMPOOL_INVALID,
   .lock = VMK_LOCK_INVALID,
   .kvMgmtHandle = NULL,
};

static VMK_ReturnStatus HeapCreate();
static void HeapDestroy();
static VMK_ReturnStatus LogHandleCreate(int logLevel);
static void LogHandleDestroy();
static VMK_ReturnStatus MemPoolCreate();
static void MemPoolDestroy();

static void NVMEPCIEValidateModuleParameter()
{
   if (nvmePCIEFakeAdminQSize >= nvmePCIEAdminQueueSize) {
      nvmePCIEFakeAdminQSize = (nvmePCIEAdminQueueSize - 1);
      NVMEPCIELogNoHandle("change nvmePCIEFakeAdminQSize to 0x%x",
         nvmePCIEFakeAdminQSize);
   }
}
/**
 * Module entry point
 *
 * Initialize moduel-wide resources and register driver
 */
int
init_module(void)
{
   VMK_ReturnStatus vmkStatus;

   NVMEPCIELogNoHandle("Loading driver %s.", NVME_PCIE_DRIVER_IDENT);
   NVMEPCIEValidateModuleParameter();

   /* Always initialize heap in the first place. */
   vmkStatus = HeapCreate();
   if (vmkStatus != VMK_OK) {
      NVMEPCIELogNoHandle("Failed to create driver heap, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   /* Initialize log components, and set default log level based on
    * module parameter.
    */
   vmkStatus = LogHandleCreate(nvmePCIELogLevel);
   if (vmkStatus != VMK_OK) {
      NVMEPCIELogNoHandle("Failed to create log handle, %s.",
                          vmk_StatusToString(vmkStatus));
      goto destroy_heap;
   }

   /** Initialize mem pool */
   vmkStatus = MemPoolCreate();
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Failed to create mem pool, %s.",
             vmk_StatusToString(vmkStatus));
      goto destroy_log;
   }

   /** Initialize global lock */
   vmkStatus = NVMEPCIELockCreateNoRank(NVME_PCIE_DRIVER_PROPS_LOCK_NAME,
                                        &NVME_PCIE_DRIVER_RES_LOCK);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Failed to create global lock, %s.",
             vmk_StatusToString(vmkStatus));
      goto destroy_mempool;
   }

   /** Initialize management handle */
   vmkStatus = NVMEPCIEGlobalKeyValInit();
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Failed to create mgmt handle, %s.",
                 vmk_StatusToString(vmkStatus));
      goto destroy_lock;
   }

   /** Initialize controller list */
   vmk_ListInit(&NVME_PCIE_DRIVER_RES_CONTROLLER_LIST);

   /** Register driver */
   vmkStatus = NVMEPCIEDriverRegister();
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Failed to register driver, %s.", vmk_StatusToString(vmkStatus));
      goto destroy_mgmt_handle;
   }

   MOD_IPRINT("Module initialized successfully.");

   return 0;

destroy_mgmt_handle:
   NVMEPCIEGlobalKeyValDestroy();

destroy_lock:
   NVMEPCIELockDestroy(&NVME_PCIE_DRIVER_RES_LOCK);

destroy_mempool:
   MemPoolDestroy();

destroy_log:
   LogHandleDestroy();

destroy_heap:
   HeapDestroy();

   return vmkStatus;
}


/**
 * Module exit point
 *
 * Cleanup module-wide resources during module unload
 */
void
cleanup_module(void)
{
   /*
    * Cleanup module resources in reverse order compared to module_init
    */
   NVMEPCIEDriverUnregister();
   NVMEPCIEGlobalKeyValDestroy();
   NVMEPCIELockDestroy(&NVME_PCIE_DRIVER_RES_LOCK);
   MemPoolDestroy();
   LogHandleDestroy();
   HeapDestroy();

   NVMEPCIELogNoHandle("Driver %s cleaned up successfully.", NVME_PCIE_DRIVER_IDENT);
}

/**
 * Create the default heap of the module, and associate heap with the module.
 *
 * This will update the module's global resource data.
 *
 * @return VMK_OK Heap created successfully
 * @return VMK_EXISTS Driver's heap has already been created
 * @return Others Errors returned by vmk_HeapCreate
 */
static VMK_ReturnStatus
HeapCreate()
{
   VMK_ReturnStatus vmkStatus;
   vmk_HeapCreateProps props;
   vmk_ByteCount maxSize;

   vmk_HeapAllocationDescriptor heapAllocDesc[] = {

      /*
       * This allocation is module-wide. Currently we allocate 1 log handle from heap
       *
       * TODO: Update the values and add entries when need to allocate memory from heap
       */
      {
         .size = NVME_PCIE_HEAP_EST,
         .alignment = 0,
         .count = 1
      },
      {
         .size = vmk_LogHeapAllocSize(),
         .alignment = 0,
         .count = 1
      },
      {
         .size = vmk_SpinlockAllocSize(VMK_SPINLOCK),
         .alignment = 0,
         .count = 1
      },
      {
         .size = sizeof(NVMEPCIEController),
         .alignment = VMK_L1_CACHELINE_SIZE,
         .count = NVME_PCIE_MAX_CONTROLLERS
      },
      {
         .size = vmk_LockDomainAllocSize(),
         .alignment = 0,
         .count = NVME_PCIE_MAX_CONTROLLERS
      },
      {
         .size = sizeof(vmk_IntrCookie) * (NVME_PCIE_MAX_IO_QUEUES + 1),
         .alignment = 0,
         .count = NVME_PCIE_MAX_CONTROLLERS
      },
      {
         .size = NVMEPCIEQueueAllocSize() * (NVME_PCIE_MAX_IO_QUEUES + 1),
         .alignment = 0,
         .count = NVME_PCIE_MAX_CONTROLLERS
      },
   };

   /* Ensures that this function is not called twice. */
   if (NVME_PCIE_DRIVER_RES_HEAP_ID != VMK_INVALID_HEAP_ID) {
      return VMK_EXISTS;
   }

   vmkStatus = vmk_HeapDetermineMaxSize(heapAllocDesc,
                                        (sizeof(heapAllocDesc)/sizeof(heapAllocDesc[0])),
                                        &maxSize);
   if(vmkStatus != VMK_OK) {
      return vmkStatus;
   }

   props.type = VMK_HEAP_TYPE_SIMPLE;
   props.module = vmk_ModuleCurrentID;
   props.initial = 0;
   props.max = maxSize;
   props.creationTimeoutMS = VMK_TIMEOUT_UNLIMITED_MS;
   vmk_NameInitialize(&props.name, NVME_PCIE_DRIVER_PROPS_HEAP_NAME);

   vmkStatus = vmk_HeapCreate(&props, &(NVME_PCIE_DRIVER_RES_HEAP_ID));
   if (vmkStatus != VMK_OK) {
      return vmkStatus;
   }

   vmk_ModuleSetHeapID(vmk_ModuleCurrentID, NVME_PCIE_DRIVER_RES_HEAP_ID);

   return VMK_OK;
}


/**
 * Disassociate module default heap from the module and destroy the heap.
 *
 * This will update the module's global resource data.
 */
static void
HeapDestroy()
{
   if (NVME_PCIE_DRIVER_RES_HEAP_ID == VMK_INVALID_HEAP_ID) {
      return;
   }

   vmk_ModuleSetHeapID(vmk_ModuleCurrentID, VMK_INVALID_HEAP_ID);
   vmk_HeapDestroy(NVME_PCIE_DRIVER_RES_HEAP_ID);
   NVME_PCIE_DRIVER_RES_HEAP_ID = VMK_INVALID_HEAP_ID;
}


/**
 * Create log handle
 *
 * This will update the module's global resource data.
 *
 * @param[in] logLevel  default log level of the handle
 *
 * @return VMK_OK Log handle created successfully
 * @return VMK_BAD_PARAM Invalid log level
 * @return VMK_EXISTS Log handle already created
 * @return Others Errors returned by vmk_LogRegister
 */
static VMK_ReturnStatus
LogHandleCreate(int logLevel)
{
   VMK_ReturnStatus vmkStatus;
   vmk_LogProperties props;

   if (logLevel >= NVME_LOG_LEVEL_LAST) {
      return VMK_BAD_PARAM;
   }

   if (NVME_PCIE_DRIVER_RES_LOG_HANDLE != VMK_INVALID_LOG_HANDLE) {
      return VMK_EXISTS;
   }

   props.module = vmk_ModuleCurrentID;
   props.heap = NVME_PCIE_DRIVER_RES_HEAP_ID;
   props.defaultLevel = logLevel;
   props.throttle = NULL;
   vmk_NameInitialize(&props.name, NVME_PCIE_DRIVER_PROPS_LOG_NAME);

   vmkStatus = vmk_LogRegister(&props, &(NVME_PCIE_DRIVER_RES_LOG_HANDLE));
   if (vmkStatus != VMK_OK) {
      return vmkStatus;
   }

   return VMK_OK;
}


/**
 * Destroy log handle
 *
 * This will update the module's global resource data.
 */
static void
LogHandleDestroy()
{
   if (NVME_PCIE_DRIVER_RES_LOG_HANDLE == VMK_INVALID_LOG_HANDLE) {
      return;
   }

   vmk_LogUnregister(NVME_PCIE_DRIVER_RES_LOG_HANDLE);
   NVME_PCIE_DRIVER_RES_LOG_HANDLE = VMK_INVALID_LOG_HANDLE;
}

/**
 * Create memmory pool
 *
 * This will update the module's global resource data.
 *
 * @return VMK_OK Memory pool created successfully
 * @return VMK_EXISTS Memory pool already created
 * @return Others Errors returned by vmk_MemPoolCreate
 */
static VMK_ReturnStatus
MemPoolCreate()
{
   vmk_MemPoolProps props;

   if (NVME_PCIE_DRIVER_RES_MEMPOOL != VMK_MEMPOOL_INVALID) {
      return VMK_EXISTS;
   }

   props.module = vmk_ModuleCurrentID;
   props.parentMemPool = VMK_MEMPOOL_INVALID;
   props.memPoolType = VMK_MEM_POOL_LEAF;
   // TODO: Set reservation and limit size to approriate value
   props.resourceProps.reservation = 0;
   props.resourceProps.limit = 0;
   vmk_NameInitialize(&props.name, NVME_PCIE_DRIVER_PROPS_MEMPOOL_NAME);

   return vmk_MemPoolCreate(&props, &(NVME_PCIE_DRIVER_RES_MEMPOOL));
}

/**
 * Destroy memory pool
 *
 * This will update the module's global resource data.
 */
static void
MemPoolDestroy()
{
   if (NVME_PCIE_DRIVER_RES_MEMPOOL == VMK_MEMPOOL_INVALID) {
      return;
   }

   vmk_MemPoolDestroy(NVME_PCIE_DRIVER_RES_MEMPOOL);
   NVME_PCIE_DRIVER_RES_MEMPOOL = VMK_MEMPOOL_INVALID;
}
