/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (c) 2012-2013  Integrated Device Technology, Inc.
 * All rights reserved.
 *******************************************************************************/

/*
 * @file: nvme_module.c --
 *
 *    Entry point for native nvme driver.
 */

#include "oslib.h"
//#include "../../common/kernel/nvme_private.h"


extern int nvme_log_level;

/**
 * Global, static data that holds module/driver wide resources.
 */
Nvme_DriverResource __driverResource;


/**
 * Create the default heap of the module, and associate heap with the module.
 *
 * This will update the module's global resource data.
 *
 * @return VMK_OK: heap creation successful
 * @return VMK_EXISTS: driver's heap has already been created
 * @return Others: errors returned by vmk_HeapCreate
 */
static VMK_ReturnStatus
HeapCreate()
{
   VMK_ReturnStatus vmkStatus;
   vmk_HeapCreateProps props;

   /* Ensures that this function is not called twice. */
   VMK_ASSERT(NVME_DRIVER_RES_HEAP_ID == VMK_INVALID_HEAP_ID);
   if (NVME_DRIVER_RES_HEAP_ID != VMK_INVALID_HEAP_ID) {
      return VMK_EXISTS;
   }

   props.type = VMK_HEAP_TYPE_SIMPLE;
   props.module = vmk_ModuleCurrentID;
   props.initial = NVME_DRIVER_PROPS_HEAP_INITIAL;
   props.max = NVME_DRIVER_PROPS_HEAP_MAX;
   props.creationTimeoutMS = VMK_TIMEOUT_UNLIMITED_MS;
   vmk_NameInitialize(&props.name, NVME_DRIVER_PROPS_HEAP_NAME);

   vmkStatus = vmk_HeapCreate(&props, &(NVME_DRIVER_RES_HEAP_ID));
   if (vmkStatus != VMK_OK) {
      return vmkStatus;
   }

   vmk_ModuleSetHeapID(vmk_ModuleCurrentID, NVME_DRIVER_RES_HEAP_ID);

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
   VMK_ASSERT(NVME_DRIVER_RES_HEAP_ID != VMK_INVALID_HEAP_ID);
   if (NVME_DRIVER_RES_HEAP_ID == VMK_INVALID_HEAP_ID) {
      return;
   }

   vmk_ModuleSetHeapID(vmk_ModuleCurrentID, VMK_INVALID_HEAP_ID);
   vmk_HeapDestroy(NVME_DRIVER_RES_HEAP_ID);
   NVME_DRIVER_RES_HEAP_ID = VMK_INVALID_HEAP_ID;
}


/**
 * Create reate log handle
 *
 * This will update the module's global resource data.
 *
 * @param logLevel default log level of the handle
 *
 * @return VMK_OK log handle created successfully
 * @return VMK_BAD_PARAM invalid log level
 * @return VMK_EXISTS log handle already created
 */
static VMK_ReturnStatus
LogHandleCreate(int logLevel)
{
   VMK_ReturnStatus vmkStatus;
   vmk_LogProperties props;

   if (logLevel >= NVME_LOG_LEVEL_LAST) {
      return VMK_BAD_PARAM;
   }

   if (NVME_DRIVER_RES_LOG_HANDLE != VMK_INVALID_LOG_HANDLE) {
      return VMK_EXISTS;
   }

   props.module = vmk_ModuleCurrentID;
   props.heap = NVME_DRIVER_RES_HEAP_ID;
   props.defaultLevel = logLevel;
   props.throttle = NULL;
   vmk_NameInitialize(&props.name, NVME_DRIVER_PROPS_LOG_NAME);

   vmkStatus = vmk_LogRegister(&props, &(NVME_DRIVER_RES_LOG_HANDLE));
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
   VMK_ASSERT(NVME_DRIVER_RES_LOG_HANDLE != VMK_INVALID_LOG_HANDLE);
   if (NVME_DRIVER_RES_LOG_HANDLE == VMK_INVALID_LOG_HANDLE) {
      return;
   }

   vmk_LogUnregister(NVME_DRIVER_RES_LOG_HANDLE);
   NVME_DRIVER_RES_LOG_HANDLE = VMK_INVALID_LOG_HANDLE;
}


/**
 * Create memory pool
 *
 * This will update the module's global resource data.
 *
 * @return VMK_OK log handle created successfully
 * @return VMK_EXISTS memory pool already created.
 * @return Others: errors returned by vmk_MemPoolCreate
 */
static VMK_ReturnStatus
MemPoolCreate()
{
   vmk_MemPoolProps props;

   if (NVME_DRIVER_RES_MEMPOOL != VMK_MEMPOOL_INVALID) {
      return VMK_EXISTS;
   }

   props.module = vmk_ModuleCurrentID;
   props.parentMemPool = VMK_MEMPOOL_INVALID;
   props.memPoolType = VMK_MEM_POOL_LEAF;
   props.resourceProps.reservation = NVME_DRIVER_PROPS_MPOOL_RESV;
   props.resourceProps.limit = NVME_DRIVER_PROPS_MPOOL_LIMIT;
   vmk_NameInitialize(&props.name, NVME_DRIVER_PROPS_MPOOL_NAME);

   return vmk_MemPoolCreate(&props, &(NVME_DRIVER_RES_MEMPOOL));
}


/**
 * Destroy memory pool
 *
 * This will update the module's global resource data.
 *
 * @return VMK_OK log handle created successfully
 * @return Others: errors returned by vmk_MemPoolDestroy
 */
static VMK_ReturnStatus
MemPoolDestroy()
{
   return vmk_MemPoolDestroy(NVME_DRIVER_RES_MEMPOOL);
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

   Nvme_LogNoHandle("Loading driver %s.", NVME_DRIVER_IDENT);

   Nvme_ValidateModuleParams();

   /* Always initialize heap in the first place. */
   vmkStatus = HeapCreate();
   if (vmkStatus != VMK_OK) {
      Nvme_LogNoHandle("failed to create driver heap, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   /* Initialize log components, and set default log level based on
    * module parameter.
    */
   vmkStatus = LogHandleCreate(nvme_log_level);
   if (vmkStatus != VMK_OK) {
      Nvme_LogNoHandle("failed to create log handle, 0x%x.", vmkStatus);
      goto destroy_heap;
   }

   /* Initialize mem pool. mem pool is used for allocating large
    * physically contiguous memory.
    */
   vmkStatus = MemPoolCreate();
   if (vmkStatus != VMK_OK) {
      EPRINT("failed to create mem pool, 0x%x.", vmkStatus);
      goto destroy_log;
   }

   /** Initialize global management handle */
   vmkStatus = NvmeMgmt_GlobalInitialize();
   if (vmkStatus != VMK_OK) {
      EPRINT("failed to initialize global management interface, 0x%x.", vmkStatus);
      goto destroy_mpool;
   }

   /** Initialize global lock */
   vmkStatus = OsLib_LockCreateNoRank(NVME_GLOBAL_LOCK_NAME,
      &NVME_DRIVER_RES_LOCK);
   if (vmkStatus != VMK_OK) {
      EPRINT("failed to initialize global lock, 0x%x.", vmkStatus);
      goto destroy_mgmt;
   }

   /** Initialize adapter list */
   vmk_ListInit(&NVME_DRIVER_RES_ADAPTERLIST);

   /* TODO: Do we have other global resources to initialize here? */

   /* Finally, register driver */
   vmkStatus = NvmeDriver_Register();
   if (vmkStatus != VMK_OK) {
      EPRINT("failed to register driver, 0x%x.", vmkStatus);
      goto destroy_lock;
   }

   IPRINT("module initialized successfully.");

   return 0;

destroy_lock:
   OsLib_LockDestroy(&NVME_DRIVER_RES_LOCK);

destroy_mgmt:
   NvmeMgmt_GlobalDestroy();

destroy_mpool:
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
   NvmeDriver_Unregister();
   OsLib_LockDestroy(&NVME_DRIVER_RES_LOCK);
   NvmeMgmt_GlobalDestroy();
   MemPoolDestroy();
   LogHandleDestroy();

   /* TODO: Do we have other global resources to clean up? */

   /* Lastly, destroy driver heap. */
   HeapDestroy();

   Nvme_LogNoHandle("Driver %s cleaned up successfully.", NVME_DRIVER_IDENT);
}
