/*****************************************************************************
 * Copyright (c) 2016, 2018, 2020-2022 VMware, Inc. All rights reserved.
 * -- VMware Confidential
 *****************************************************************************/

/*
 * nvme_pcie_os.h --
 *
 *	Utils of vmkernel for nvme_pcie driver.
 */

#ifndef _NVME_PCIE_OS_H_
#define _NVME_PCIE_OS_H_

/*
 * XXX Specify the code modules that are allowed to use this interface.
 * XXX Possible values are
 * XXX    INCLUDE_ALLOW_DISTRIBUTE
 * XXX    INCLUDE_ALLOW_MODULE
 * XXX    INCLUDE_ALLOW_USERLEVEL
 * XXX    INCLUDE_ALLOW_VMCORE
 * XXX    INCLUDE_ALLOW_VMKERNEL
 * XXX    INCLUDE_ALLOW_VMK_MODULE
 * XXX    INCLUDE_ALLOW_VMIROM
 * XXX    INCLUDE_ALLOW_VMMON
 * XXX    INCLUDE_ALLOW_VMX
 */
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_VMKERNEL
//#include "includeCheck.h"

#define NVME_PCIE_STORAGE_POLL 1

#if NVME_PCIE_STORAGE_POLL
#define NVME_PCIE_BLOCKSIZE_AWARE 1
#define NVME_PCIE_SMALL_BLOCKSIZE 32
#endif

/**
 * NVMEPCIEDriverResource - global data structure that holds module wide resources.
 * Should have just one instance in the whole driver module.
 */
typedef struct NVMEPCIEDriverResource {
   /** Heap ID */
   vmk_HeapID heapId;
   /** Driver handle */
   vmk_Driver driverHandle;
   /** Log component ID */
   vmk_LogComponent logHandle;
   /** Memory pool handle */
   vmk_MemPool memPool;
   /** Global lock */
   vmk_Lock lock;
   /** Controller list */
   vmk_ListLinks ctrlrList;
   /** Management handle */
   vmk_MgmtHandle kvMgmtHandle;
} NVMEPCIEDriverResource;

/**
 * Instance of NVMEPCIEDriverResource. Defined as global, static variable
 * in nvme_pcie_module.c and is accessed by other components of the driver.
 */
extern NVMEPCIEDriverResource __nvmePCIEdriverResource;

/**
 * Accessor macros for module-wide resources
 */
#define NVME_PCIE_DRIVER_RES_HEAP_ID (__nvmePCIEdriverResource.heapId)
#define NVME_PCIE_DRIVER_RES_DRIVER_HANDLE (__nvmePCIEdriverResource.driverHandle)
#define NVME_PCIE_DRIVER_RES_LOG_HANDLE (__nvmePCIEdriverResource.logHandle)
#define NVME_PCIE_DRIVER_RES_MEMPOOL (__nvmePCIEdriverResource.memPool)
#define NVME_PCIE_DRIVER_RES_LOCK (__nvmePCIEdriverResource.lock)
#define NVME_PCIE_DRIVER_RES_CONTROLLER_LIST (__nvmePCIEdriverResource.ctrlrList)
#define NVME_PCIE_DRIVER_MGMT_HANDLE (__nvmePCIEdriverResource.kvMgmtHandle)

#define NVME_PCIE_DRIVER_PROPS_HEAP_NAME "nvmePCIEHeap"
#define NVME_PCIE_DRIVER_PROPS_DRIVER_NAME "nvmePCIEDriver"
#define NVME_PCIE_DRIVER_PROPS_LOG_NAME "nvmePCIELogHandle"
#define NVME_PCIE_DRIVER_PROPS_MEMPOOL_NAME "nvmePCIEMemPool"
#define NVME_PCIE_DRIVER_PROPS_LOCK_NAME "nvmePCIELock"

enum NVMEPCIELockRank {
   NVME_LOCK_RANK_INVALID  = 0,
   NVME_LOCK_RANK_LOW,
   NVME_LOCK_RANK_MEDIUM,
   NVME_LOCK_RANK_HIGH,
};

/**
 * OS resources for each controller instance
 */
typedef struct NVMEPCIECtrlrOsResources {
   /** Device handle */
   vmk_Device device;

   /** PCI resources */
   vmk_PCIDevice pciDevice;
   vmk_PCIDeviceID pciId;
   vmk_PCIDeviceAddr sbdf;
   vmk_IOReservation pciResv;

   /** DMA engine */
   vmk_DMAEngine dmaEngine;
   // TODO: Seems this could be removed
   vmk_SgOpsHandle sgHandle;

   /** Interrupt resources */
   vmk_PCIInterruptType intrType;
   vmk_uint32 numIntrs;
   vmk_IntrCookie *intrArray;

   /** Lock domain */
   vmk_LockDomainID lockDomain;

   /** vmk nvme adatper */
   vmk_NvmeAdapter vmkAdapter;
   /** vmk nvme controller */
   vmk_NvmeController vmkController;
   /** DMA engine for IO */
   vmk_DMAEngine IODmaEngine;
   /** NVMe logical device */
   vmk_Device logicalDevice;
} NVMEPCIECtrlrOsResources;

/**
 * Data structure to track DMA buffer allocation
 */
typedef struct NVMEPCIEDmaEntry {
   vmk_VA va;
   vmk_IOA ioa;
   vmk_ByteCount size;
   vmk_DMADirection direction;
   vmk_SgArray *sgIn;
   vmk_SgArray *sgOut;
   vmk_MpnRange mpnRange;
} NVMEPCIEDmaEntry;

/**
 * NVMEPCIEAlloc --
 *
 *    Allocate memory from driver's default heap and zero it out.
 *
 * @param[in] size   Number of bytes to allocate.
 * @param[in] align  Number of bytes the allocation should be
                     aligned on.
 *
 * @return pointer to the allocated memory in the heap
 * @return NULL Allocation failed
 */
static inline void *
NVMEPCIEAlloc(vmk_uint32 size, vmk_uint32 align)
{
   void *ret = NULL;

   /** No alignment required */
   if (align == 0) {
      ret = vmk_HeapAlloc(NVME_PCIE_DRIVER_RES_HEAP_ID, size);
   } else {
      ret = vmk_HeapAlign(NVME_PCIE_DRIVER_RES_HEAP_ID, size, align);
   }

   if (ret != NULL) {
      vmk_Memset(ret, 0, size);
   }

   return ret;
}

/**
 * NVMEPCIEFree --
 *
 *    Free memory allocated from the default heap
 *
 * @param[in] mem  Memory to be freed
 */
static inline void
NVMEPCIEFree(void *mem)
{
   vmk_HeapFree(NVME_PCIE_DRIVER_RES_HEAP_ID, mem);
}

/**
 * Get microseconds since system boot
 */
static inline vmk_uint64
NVMEPCIEGetTimerUs()
{
   return vmk_TimerUnsignedTCToUS(vmk_GetTimerCycles());
}

VMK_ReturnStatus NVMEPCIELockCreateNoRank(const char *name, vmk_Lock *lock);
VMK_ReturnStatus NVMEPCIELockCreate(vmk_LockDomainID domain,
                                    vmk_LockRank rank,
                                    const char *name,
                                    vmk_Lock *lock);
void NVMEPCIELockDestroy(vmk_Lock *lock);
VMK_ReturnStatus NVMEPCIELockDomainCreate(const char* name, vmk_LockDomainID *domain);
void NVMEPCIELockDomainDestroy(vmk_LockDomainID domain);
VMK_ReturnStatus NVMEPCIEDriverRegister();
void NVMEPCIEDriverUnregister();
VMK_ReturnStatus NVMEPCIEDmaAlloc(NVMEPCIECtrlrOsResources *ctrlrOsRes,
                                  vmk_ByteCount size,
                                  NVMEPCIEDmaEntry *dmaEntry,
                                  vmk_uint32 timeout);
VMK_ReturnStatus NVMEPCIEDmaFree(NVMEPCIECtrlrOsResources *ctrlrOsRes,
                                 NVMEPCIEDmaEntry *dmaEntry);
VMK_ReturnStatus NVMEPCIEIntrRegister(vmk_Device device,
                                      vmk_IntrCookie intrCookie,
                                      void *handlerData,
                                      const char *name,
                                      vmk_IntrAcknowledge intrAck,
                                      vmk_IntrHandler intrHandler);
VMK_ReturnStatus NVMEPCIEIntrUnregister(vmk_IntrCookie intrCookie, void *handlerData);

#endif // ifndef _NVME_PCIE_OS_H_
