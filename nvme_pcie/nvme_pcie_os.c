/*****************************************************************************
 * Copyright (c) 2016, 2018, 2020 VMware, Inc. All rights reserved.
 * -- VMware Confidential
 *****************************************************************************/

/*
 * @file: nvme_pcie_os.c --
 *
 *    Utils of vmkernel for native nvme_pcie driver.
 */

#include "nvme_pcie_int.h"

/**
 * Create a spinlock with no rank
 *
 * @param[in]  name  Name of the spinlock
 * @param[out] lock  Pointer to the initialized spinlock
 *
 * @return VMK_OK Spinlock created successfully
 * @return Others Errors returned by vmk_SpinlockCreate
 */
VMK_ReturnStatus
NVMEPCIELockCreateNoRank(const char *name, vmk_Lock *lock)
{
   vmk_SpinlockCreateProps props;

   props.moduleID = vmk_ModuleCurrentID;
   props.heapID = NVME_PCIE_DRIVER_RES_HEAP_ID;
   props.type = VMK_SPINLOCK;
   props.domain = VMK_LOCKDOMAIN_INVALID;
   props.rank = VMK_SPINLOCK_UNRANKED;
   vmk_NameInitialize(&props.name, name);

   return vmk_SpinlockCreate(&props, lock);
}

/**
 * Create a spinlock with rank
 *
 * @param[in]  domain  Lock domain
 * @param[in]  rank    Rank of the spinlock
 * @param[in]  name    Name of the spinlock
 * @param[out] lock    Pointer to the initialized spinlock
 *
 * @return VMK_OK Spinlock created successfully
 * @return Others Errors returned by vmk_SpinlockCreate
 */
VMK_ReturnStatus
NVMEPCIELockCreate(vmk_LockDomainID domain, vmk_LockRank rank,
                   const char *name, vmk_Lock *lock)
{
   vmk_SpinlockCreateProps props;

   props.moduleID = vmk_ModuleCurrentID;
   props.heapID = NVME_PCIE_DRIVER_RES_HEAP_ID;
   props.type = VMK_SPINLOCK;
   props.domain = domain;
   props.rank = rank;
   vmk_NameInitialize(&props.name, name);

   return vmk_SpinlockCreate(&props, lock);
}

/**
 * Destroy spinlock
 *
 * @param[in] lock  Spinlock to be destroyed
 */
void
NVMEPCIELockDestroy(vmk_Lock *lock)
{
   vmk_SpinlockDestroy(*lock);
   *lock = VMK_LOCK_INVALID;
}

/**
 * Create a lock domain
 *
 * @param[in]  name    Name of the domain
 * @param[out] domain  ID of the created domain
 *
 * @return VMK_OK Lock domain created successfully
 * @return Others Errors returned by vmk_LockDomainCreate
 */
VMK_ReturnStatus
NVMEPCIELockDomainCreate(const char *name, vmk_LockDomainID *domain)
{
   vmk_Name vmkName;

   vmk_NameInitialize(&vmkName, name);
   return vmk_LockDomainCreate(vmk_ModuleCurrentID,
                               NVME_PCIE_DRIVER_RES_HEAP_ID,
                               &vmkName,
                               domain);
}

/**
 * Destroy lock domain
 *
 * @param[in] domain  Domain ID
 */
void
NVMEPCIELockDomainDestroy(vmk_LockDomainID domain)
{
   vmk_LockDomainDestroy(domain);
   domain = VMK_LOCKDOMAIN_INVALID;
}

/**
 * Allocate and map physically contiguous dma memory
 *
 * @param[in]  ctrlrOsRes  Controller OS resources
 * @param[in]  size        Size in bytes to be allocated
 * @param[out] dmaEntry    Dma entry to be allocated
 * @param[in]  timeout     Time to wait for memory allocation
 *
 * @return VMK_OK on success, error code otherwise
 */
VMK_ReturnStatus
NVMEPCIEDmaAlloc(NVMEPCIECtrlrOsResources *ctrlrOsRes, vmk_ByteCount size,
                 NVMEPCIEDmaEntry *dmaEntry, vmk_uint32 timeout)
{
   VMK_ReturnStatus vmkStatus;
   vmk_MemPoolAllocProps allocProps;
   vmk_MemPoolAllocRequest allocRequest;
   vmk_MapRequest mapRequest;
   vmk_DMAMapErrorInfo err;

   /** Always assume bi-direction in current implementation. */
   dmaEntry->direction = VMK_DMA_DIRECTION_BIDIRECTIONAL;
   dmaEntry->size = size;

   /** First, allocate a physically contiguous region of pages. */
   allocProps.physContiguity = VMK_MEM_PHYS_CONTIGUOUS;
   allocProps.physRange = VMK_PHYS_ADDR_ANY;
   allocProps.creationTimeoutMS = timeout;

   allocRequest.numPages = VMK_UTIL_ROUNDUP(size, VMK_PAGE_SIZE) >> VMK_PAGE_SHIFT;
   allocRequest.numElements = 1;
   allocRequest.mpnRanges = &dmaEntry->mpnRange;

   vmkStatus = vmk_MemPoolAlloc(NVME_PCIE_DRIVER_RES_MEMPOOL,
                                &allocProps, &allocRequest);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Failed to allocate pages from mem pool, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   /* Now, we need to map the pages to virtual addresses. */
   mapRequest.mapType = VMK_MAPTYPE_DEFAULT;
   mapRequest.mapAttrs = VMK_MAPATTRS_READWRITE;
   mapRequest.numElements = 1;
   mapRequest.mpnRanges = &dmaEntry->mpnRange;
   mapRequest.reservation = NULL;

   vmkStatus = vmk_Map(vmk_ModuleCurrentID, &mapRequest, &dmaEntry->va);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Failed to map pages, 0x%x.", vmkStatus);
      goto free_pages;
   }

   /** Clear the memory */
   vmk_Memset((void *)dmaEntry->va, 0, VMK_UTIL_ROUNDUP(size, VMK_PAGE_SIZE));

   /** Lastly, need to map machine addresses through IOMMU */
   vmkStatus = vmk_SgAllocWithInit(ctrlrOsRes->sgHandle, &dmaEntry->sgIn,
                                   (void *)dmaEntry->va, size);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Failed to alloc sg array, 0x%x.", vmkStatus);
      goto unmap;
   }

   VMK_ASSERT(dmaEntry->sgIn->numElems == 1);

   vmkStatus = vmk_DMAMapSg(ctrlrOsRes->dmaEngine, dmaEntry->direction,
      ctrlrOsRes->sgHandle, dmaEntry->sgIn, &dmaEntry->sgOut, &err);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Failed to map sg array, %s, 0x%x.",
         vmk_DMAMapErrorReasonToString(err.reason),
         vmkStatus);
      goto free_sg;
   }

   dmaEntry->ioa = dmaEntry->sgOut->elem[0].ioAddr;

   return VMK_OK;

free_sg:
   vmk_SgFree(ctrlrOsRes->sgHandle, dmaEntry->sgIn);
   dmaEntry->sgIn = NULL;

unmap:
   vmk_Unmap(dmaEntry->va);
   dmaEntry->va = 0;

free_pages:
   /** allocRequest should hold the data for free already */
   vmk_MemPoolFree(&allocRequest);

   return vmkStatus;
}

/**
 * Unmap and free dma memory
 *
 * @param[in] ctrlrOsRes  Controller OS resources
 * @param[in] dmaEntry    Dma entry to be freed
 *
 * @return VMK_OK DMA memory freed successfully
 * @return VMK_FAILURE Failed to unmap or free dma memory
 */
VMK_ReturnStatus
NVMEPCIEDmaFree(NVMEPCIECtrlrOsResources *ctrlrOsRes, NVMEPCIEDmaEntry *dmaEntry)
{
   VMK_ReturnStatus vmkStatus;
   vmk_MemPoolAllocRequest allocRequest;
   int errors = 0;
   vmk_ByteCount size = dmaEntry->size;

   vmkStatus = vmk_DMAUnmapSg(ctrlrOsRes->dmaEngine, dmaEntry->direction,
                              ctrlrOsRes->sgHandle, dmaEntry->sgOut);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Failed to unmap sg array, 0x%x.", vmkStatus);
      errors ++;
   }
   dmaEntry->sgOut = NULL;

   vmkStatus = vmk_SgFree(ctrlrOsRes->sgHandle, dmaEntry->sgIn);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Failed to free sg array, 0x%x.", vmkStatus);
      errors ++;
   }
   dmaEntry->sgIn = NULL;

   vmk_Unmap(dmaEntry->va);

   allocRequest.numPages = VMK_UTIL_ROUNDUP(size, VMK_PAGE_SIZE) >> VMK_PAGE_SHIFT;
   allocRequest.numElements = 1;
   allocRequest.mpnRanges = &dmaEntry->mpnRange;

   vmkStatus = vmk_MemPoolFree(&allocRequest);
   if (vmkStatus != VMK_OK) {
      MOD_EPRINT("Failed to free mem pages, 0x%x.", vmkStatus);
      errors ++;
   }

   if (!errors) {
      return VMK_OK;
   } else {
      return VMK_FAILURE;
   }
}

/**
 * Allocate interrupt cookies
 *
 * This will update controller's os resources
 *
 * @param [in] ctrlr       Controller instance
 * @param [in] type        Interrupt type
 * @param [in] numDesired  Desired interrupt numbers
 *
 * @return VMK_OK on success, error code otherwise
 */
VMK_ReturnStatus
NVMEPCIEIntrAlloc(NVMEPCIEController *ctrlr,
                 vmk_PCIInterruptType type,
                 vmk_uint32 numDesired)
{
   VMK_ReturnStatus vmkStatus;
   /** Minimum number of interrupts */
   vmk_uint32 numRequired = 1;
   /** Actual number of interrupts allocated */
   vmk_uint32 numAllocated = 0;

   if (ctrlr->osRes.intrType != VMK_PCI_INTERRUPT_TYPE_NONE) {
      EPRINT(ctrlr, "Interrupts have been already allocated.");
      return VMK_BAD_PARAM;
   }

   switch (type) {
      case VMK_PCI_INTERRUPT_TYPE_LEGACY:
      case VMK_PCI_INTERRUPT_TYPE_MSI:
         if (numDesired != 1) {
            EPRINT(ctrlr, "Invalid interrupt numbers %d requested.", numDesired);
            return VMK_BAD_PARAM;
         }
         break;
      case VMK_PCI_INTERRUPT_TYPE_MSIX:
         if (numDesired < numRequired) {
            EPRINT(ctrlr, "Invalid interrupt numbers %d requested.", numDesired);
            return VMK_BAD_PARAM;
         }
         break;
      default:
         EPRINT(ctrlr, "Invalid interrupt type %d.", type);
         return VMK_BAD_PARAM;
   }

   ctrlr->osRes.intrArray = NVMEPCIEAlloc(sizeof(vmk_IntrCookie) * numDesired, 0);
   if (ctrlr->osRes.intrArray == NULL) {
      return VMK_NO_MEMORY;
   }

   vmkStatus = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
      ctrlr->osRes.pciDevice, type, numDesired, numRequired,
      NULL, ctrlr->osRes.intrArray, &numAllocated);

   if (vmkStatus == VMK_OK) {
      ctrlr->osRes.intrType = type;
      ctrlr->osRes.numIntrs = numAllocated;
   } else {
      ctrlr->osRes.intrType = VMK_PCI_INTERRUPT_TYPE_NONE;
      ctrlr->osRes.numIntrs = 0;
      NVMEPCIEFree(ctrlr->osRes.intrArray);
      ctrlr->osRes.intrArray = NULL;
   }

   return vmkStatus;
}

/**
 * Free interrupt cookies
 *
 * This will update controller's os resources
 *
 * @param [in] ctrlr  Controller instance
 */
void
NVMEPCIEIntrFree(NVMEPCIEController *ctrlr)
{
   if (ctrlr->osRes.intrType == VMK_PCI_INTERRUPT_TYPE_NONE) {
      return;
   }

   vmk_PCIFreeIntrCookie(vmk_ModuleCurrentID, ctrlr->osRes.pciDevice);

   NVMEPCIEFree(ctrlr->osRes.intrArray);
   ctrlr->osRes.intrArray = NULL;
   ctrlr->osRes.intrType = VMK_PCI_INTERRUPT_TYPE_NONE;
   ctrlr->osRes.numIntrs = 0;
   return;
}

/**
 * Register a interrupt handler
 *
 * @param[in] device       Device registering the interrupt
 * @param[in] intrCookie   Interrupt cookie
 * @param[in] handlerData  Interrupt handler client data
 * @param[in] name         Name of the device registering the interrupt
 * @param[in] intrAck      Interrupt acknowledge callback function
 * @param[in] intrHandler  Interrupt handler callback function
 *
 * @return VMK_OK Interrupt registered successfully
 * @return Others Errors returned by vmk_IntrRegister
 */
VMK_ReturnStatus
NVMEPCIEIntrRegister(vmk_Device device,
                     vmk_IntrCookie intrCookie,
                     void *handlerData,
                     const char *name,
                     vmk_IntrAcknowledge intrAck,
                     vmk_IntrHandler intrHandler)
{
   VMK_ReturnStatus vmkStatus;
   vmk_IntrProps props;

   props.device = device;
   props.acknowledgeInterrupt = intrAck;
   props.handler = intrHandler;
   props.handlerData = handlerData;
   props.attrs = 0;
   vmk_NameInitialize(&props.deviceName, name);

   vmkStatus = vmk_IntrRegister(vmk_ModuleCurrentID, intrCookie, &props);

   return vmkStatus;
}


/**
 * Unregister interrupt handler
 *
 * @param[in] intrCookie   Interrupt cookie
 * @param[in] handlerData  Interrupt handler client data
 *
 * @return VMK_OK Interrupt unregistered successfully
 * @return Others Errors returned by vmk_IntrUnregister
 */
VMK_ReturnStatus
NVMEPCIEIntrUnregister(vmk_IntrCookie intrCookie, void *handlerData)
{
   return vmk_IntrUnregister(vmk_ModuleCurrentID, intrCookie, handlerData);
}
