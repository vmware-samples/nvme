

#include "vmkapi.h"
#include "oslib.h"
#include "nvme_os.h"

#include "nvme_private.h"

/**
 * Create a spinlock with no rank
 *
 */
VMK_ReturnStatus
OsLib_LockCreateNoRank(const char *name, vmk_Lock *lock)
{
   vmk_SpinlockCreateProps props;

   props.moduleID = vmk_ModuleCurrentID;
   props.heapID = NVME_DRIVER_RES_HEAP_ID;
   props.type = VMK_SPINLOCK;
   props.domain = VMK_LOCKDOMAIN_INVALID;
   props.rank = VMK_SPINLOCK_UNRANKED;
   vmk_NameInitialize(&props.name, name);

   return vmk_SpinlockCreate(&props, lock);
}


/**
 * Create a spinlock.
 *
 * All locks created under the same controller share the same lock domain.
 *
 * @param [in] ctrlr controller instance of the lock
 * @param [in] name name of the lock
 * @param [in] rank rank of the lock
 * @param [out] lock pinter to the lock to be created
 */
VMK_ReturnStatus
OsLib_LockCreate(vmk_LockDomainID lockDomain, vmk_LockRank rank,
                 const char *name, vmk_Lock *lock)
{
   vmk_SpinlockCreateProps props;

   props.moduleID = vmk_ModuleCurrentID;
   props.heapID = NVME_DRIVER_RES_HEAP_ID;
   props.type = VMK_SPINLOCK;
   props.domain = lockDomain;
   props.rank = rank;
   vmk_NameInitialize(&props.name, name);

   return vmk_SpinlockCreate(&props, lock);
}


/**
 * OsLib_SemaphoreCreate - create a semaphore
 *
 * @param [IN]  name  name of the semaphore to be created
 * @param [IN]  value initial value
 * @param [OUT] sema  pointer to the semaphore to be created
 */
VMK_ReturnStatus
OsLib_SemaphoreCreate(const char *name, int value, vmk_Semaphore *sema)
{
   return vmk_SemaCreate(sema, vmk_ModuleCurrentID, name, value);
}


/**
 * OsLib_SemaphoreDestroy - destroy a semaphore
 *
 * @param [IN]  sema  pointer to the semaphore to be destroyed
 */
VMK_ReturnStatus
OsLib_SemaphoreDestroy(vmk_Semaphore *sema)
{
   vmk_SemaDestroy(sema);
   *sema = NULL;

   return VMK_OK;
}


/**
 * Destroy spinlock
 *
 * @param [in] ctrlr controller instance
 * @param [in] lock lock to be destroyed
 */
VMK_ReturnStatus
OsLib_LockDestroy(vmk_Lock *lock)
{
   vmk_SpinlockDestroy(*lock);
   *lock = VMK_LOCK_INVALID;

   return VMK_OK;
}


/**
 * Allocate physically contiguous dma memory
 *
 * @param [in] ctrlr controller instance
 * @param [in] size size in bytes to be allocated
 * @param [out] dmaEntry used to save intermediate data that is used during dma free
 */
VMK_ReturnStatus
OsLib_DmaAlloc(struct NvmeCtrlr *ctrlr, vmk_ByteCount size,
               struct NvmeDmaEntry *dmaEntry)
{
   VMK_ReturnStatus vmkStatus;
   vmk_MemPoolAllocProps allocProps;
   vmk_MemPoolAllocRequest allocRequest;
   vmk_MapRequest mapRequest;
   vmk_DMAMapErrorInfo err;

   /* always assume bi-direction in current implementation. */
   dmaEntry->direction = VMK_DMA_DIRECTION_BIDIRECTIONAL;
   dmaEntry->size = size;

   /* first, allocate a physically contiguous region of pages */
   allocProps.physContiguity = VMK_MEM_PHYS_CONTIGUOUS;
   allocProps.physRange = VMK_PHYS_ADDR_ANY;
   allocProps.creationTimeoutMS = VMK_TIMEOUT_UNLIMITED_MS;

   allocRequest.numPages = VMK_UTIL_ROUNDUP(size, VMK_PAGE_SIZE) >> VMK_PAGE_SHIFT;
   allocRequest.numElements = 1;
   allocRequest.mpnRanges = &dmaEntry->mpnRange;

   vmkStatus = vmk_MemPoolAlloc(NVME_DRIVER_RES_MEMPOOL, &allocProps, &allocRequest);
   if (vmkStatus != VMK_OK) {
      Nvme_LogDebug("failed to allocate pages from mem pool, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   /* now, we need to map the pages to virtual addresses */
   mapRequest.mapType = VMK_MAPTYPE_DEFAULT;
   mapRequest.mapAttrs = VMK_MAPATTRS_READWRITE;
   mapRequest.numElements = 1;
   mapRequest.mpnRanges = &dmaEntry->mpnRange;
   mapRequest.reservation = NULL;

   vmkStatus = vmk_Map(vmk_ModuleCurrentID, &mapRequest, &dmaEntry->va);
   if (vmkStatus != VMK_OK) {
      Nvme_LogDebug("failed to map pages, 0x%x.", vmkStatus);
      goto free_pages;
   }

   /* lastly, need to map machine addresses throught IOMMU */
   vmkStatus = vmk_SgAllocWithInit(ctrlr->sgHandle, &dmaEntry->sgIn, (void *)dmaEntry->va, size);
   if (vmkStatus != VMK_OK) {
      Nvme_LogDebug("failed to alloc sg array, 0x%x.", vmkStatus);
      goto unmap;
   }

   VMK_ASSERT(dmaEntry->sgIn->numElems == 1);

   vmkStatus = vmk_DMAMapSg(ctrlr->dmaEngine, dmaEntry->direction,
      ctrlr->sgHandle, dmaEntry->sgIn, &dmaEntry->sgOut, &err);
   if (vmkStatus != VMK_OK) {
      Nvme_LogDebug("failed to map sg array, %s, 0x%x.",
         vmk_DMAMapErrorReasonToString(err.reason),
         vmkStatus);
      goto free_sg;
   }

   dmaEntry->ioa = dmaEntry->sgOut->elem[0].ioAddr;

   return VMK_OK;

free_sg:
   vmk_SgFree(ctrlr->sgHandle, dmaEntry->sgIn);
   dmaEntry->sgIn = NULL;

unmap:
   vmk_Unmap(dmaEntry->va);
   dmaEntry->va = 0;

free_pages:
   /* allocRequest should hold the data for free already */
   vmk_MemPoolFree(&allocRequest);

   return vmkStatus;
}


/**
 * Free dma memory
 *
 * @param [in] ctrlr controller instance
 * @param [in] dmaEntry dma entry to be freed
 */
VMK_ReturnStatus
OsLib_DmaFree(struct NvmeCtrlr *ctrlr, struct NvmeDmaEntry *dmaEntry)
{
   VMK_ReturnStatus vmkStatus;
   vmk_MemPoolAllocRequest allocRequest;
   int errors = 0;

   vmkStatus = vmk_DMAUnmapSg(ctrlr->dmaEngine, dmaEntry->direction,
      ctrlr->sgHandle, dmaEntry->sgOut);
   if (vmkStatus != VMK_OK) {
      Nvme_LogDebug("failed to unmap sg array, 0x%x.", vmkStatus);
      errors ++;
   }
   dmaEntry->sgOut = NULL;

   vmkStatus = vmk_SgFree(ctrlr->sgHandle, dmaEntry->sgIn);
   if (vmkStatus != VMK_OK) {
      Nvme_LogDebug("failed to free sg array, 0x%x.", vmkStatus);
      errors ++;
   }
   dmaEntry->sgIn = NULL;

   vmk_Unmap(dmaEntry->va);

   allocRequest.numPages = VMK_UTIL_ROUNDUP(dmaEntry->size, VMK_PAGE_SIZE) >> VMK_PAGE_SHIFT;
   allocRequest.numElements = 1;
   allocRequest.mpnRanges = &dmaEntry->mpnRange;

   vmkStatus = vmk_MemPoolFree(&allocRequest);
   if (vmkStatus != VMK_OK) {
      Nvme_LogDebug("failed to free mem pages, 0x%x.", vmkStatus);
      errors ++;
   }

   if (!errors) {
      return VMK_OK;
   } else {
      return VMK_FAILURE;
   }
}


/**
 * Register a interrupt handler
 *
 * @param [in] intrCookie interrupt cookie
 * @param [in] handlerData handler data to be passed into interrupt handler
 * @param [in] idx MSI-X interrupt id, 0 for INTx based interrupts
 * @param [in] intrAck interruptAcknowledge callback function
 * @param [in] intrHandler intrHandler callback function
 */
VMK_ReturnStatus
OsLib_IntrRegister(vmk_Device device, vmk_IntrCookie intrCookie,
                   void *handlerData, int idx,
                   vmk_IntrAcknowledge intrAck, vmk_IntrHandler intrHandler)
{
   VMK_ReturnStatus vmkStatus;
   vmk_IntrProps props;

   props.device = device;
   props.acknowledgeInterrupt = intrAck;
   props.handler = intrHandler;
   props.handlerData = handlerData;
   props.attrs = 0;
   vmk_NameFormat(&props.deviceName, "nvmeIntr-%d", idx);

   vmkStatus = vmk_IntrRegister(vmk_ModuleCurrentID,
      intrCookie, &props);

   return vmkStatus;
}


/**
 * Unregister interrupt handler
 *
 * @param [in] intrCookie interrupt cookie
 * @param [in] handlerData handler data to be passed into interrupt handler
 */
VMK_ReturnStatus
OsLib_IntrUnregister(vmk_IntrCookie intrCookie, void *handlerData)
{
   return vmk_IntrUnregister(vmk_ModuleCurrentID, intrCookie, handlerData);
}


void
OsLib_StrToUpper(char *str, int length)
{
   int i;

   for (i = 0; i < length; i++) {
      if (str[i] >= 'a' && str[i] <= 'z') {
         str[i] -= ('a' - 'A');
      }
   }
}
#if NVME_MUL_COMPL_WORLD
int nvme_compl_worlds_num = -1;
VMK_MODPARAM(nvme_compl_worlds_num, int,   \
      "Total number of NVMe completion worlds/queues.");
#endif

/* round robin for all of completion worlds */
vmk_uint32
OsLib_GetQueue(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd)
{
#if NVME_MUL_COMPL_WORLD
   static int qID=0;
   qID = (qID+1)%(ctrlr->numComplWorlds);
   return qID;
#else
   return vmk_ScsiCommandGetCompletionQueue(ctrlr->scsiAdapter, vmkCmd);
#endif
}

vmk_uint32
OsLib_GetMaxNumQueues(void)
{
#if NVME_MUL_COMPL_WORLD
   return nvme_compl_worlds_num;
#else
   return vmk_ScsiGetMaxNumCompletionQueues();
#endif
}

static vmk_atomic64 NumPCPUs = 0;

VMK_ReturnStatus
NVMeStorConstructor(vmk_PCPUID pcpu, void *object, vmk_ByteCountSmall size,  \
      vmk_AddrCookie arg) {
   vmk_AtomicInc64(&NumPCPUs);
   return VMK_OK;
}

/* Get PCPU number by counting constuctor calling number */
vmk_uint32
Oslib_GetPCPUNum(void) {
   vmk_PCPUStorageProps props;
   vmk_PCPUStorageHandle handle = VMK_PCPU_STORAGE_HANDLE_INVALID;

   props.type = VMK_PCPU_STORAGE_TYPE_WRITE_LOCAL;
   props.moduleID = vmk_ModuleCurrentID;
   vmk_NameInitialize(&props.name, "NVMePerPCPUStor");
   props.constructor = NVMeStorConstructor;
   props.destructor = NULL;
   props.size = 4;
   props.align = 0;

   vmk_AtomicWrite64(&NumPCPUs, 0);
   vmk_PCPUStorageCreate(&props, &handle);

   if (VMK_UNLIKELY(handle == VMK_PCPU_STORAGE_HANDLE_INVALID)) {
      /* failed to get memory, so our CPU number can't be trusted. */
      VMK_ASSERT(VMK_FALSE);
      return -1;
   }

   vmk_PCPUStorageDestroy(handle);

   return NumPCPUs;
}
