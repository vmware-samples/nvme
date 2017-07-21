/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (c) 2012-2013  Integrated Device Technology, Inc.
 * All rights reserved.
 *******************************************************************************/

/*
 * @file: nvme_os.h --
 *
 *    OS specific types and functions.
 */

#ifndef _NVME_OS_H
#define _NVME_OS_H

#include <vmkapi.h>
//#include "../../common/kernel/nvme_private.h"
//#include "../../common/kernel/nvme_os_common.h"


/**
 * reservation of driver's mempool (shared by all controllers)
 */
#define NVME_DRIVER_PROPS_MPOOL_RESV (10 * 1024 * 1024 / VMK_PAGE_SIZE)

/**
 * limit of driver's mempool (shared by all controllers)
 */
#define NVME_DRIVER_PROPS_MPOOL_LIMIT (4 * 1024 * 1024 * (NVME_MAX_IO_QUEUES+1) / VMK_PAGE_SIZE * (NVME_MAX_ADAPTERS))

/**
 * name of driver's mempool
 */
#define NVME_DRIVER_PROPS_MPOOL_NAME "nvmeMemPool"



/**
 * name of the controller's dma engine
 */
#define NVME_DRIVER_PROPS_CTRLR_DMAENGINE_NAME "nvmeCtrlrDmaEngine"

/**
 * dma constraints for ctrlr
 */
#define NVME_DRIVER_PROPS_CTRLR_DMAENGINE_ADDRMASK (VMK_ADDRESS_MASK_64BIT)
#define NVME_DRIVER_PROPS_CTRLR_DMAENGINE_MAXXFER (NVME_DRIVER_PROPS_MAX_PRP_LIST_ENTRIES * VMK_PAGE_SIZE)
#define NVME_DRIVER_PROPS_CTRLR_DMAENGINE_SGMAXENTRIES (NVME_DRIVER_PROPS_MAX_PRP_LIST_ENTRIES)
#define NVME_DRIVER_PROPS_CTRLR_DMAENGINE_SGELEMMAXSIZE (0)
#define NVME_DRIVER_PROPS_CTRLR_DMAENGINE_SGELEMSIZEMULT (0)
#define NVME_DRIVER_PROPS_CTRLR_DMAENGINE_SGELEMALIGN (VMK_PAGE_SIZE)
#define NVME_DRIVER_PROPS_CTRLR_DMAENGINE_SGELEMSTRADDLE (0)



/**
 * Nvme_DriverResource - global data structure that holds module wide
 * resources. Instantiated in nvme.c and should have just one instance
 * in the whole driver module.
 */
typedef struct Nvme_DriverResource {
   /** Heap ID */
   vmk_HeapID heapId;
   /** Log component ID */
   vmk_LogComponent logHandle;
   /** Driver handle */
   vmk_Driver driverHandle;
   /** Memory pool handle */
   vmk_MemPool memPool;
   /** Management handle */
   vmk_MgmtHandle mgmtHandle;
   /** Page slab handle */
   vmk_PageSlabID pageSlabId;
   /** Global lock */
   vmk_Lock lock;
   /** Adapter list */
   vmk_ListLinks adapters;
} Nvme_DriverResource;

struct NvmeCtrlOsResources
{
   /** Device handle */
   vmk_Device device;

   /** Lock domain */
   /* do we need controller wide lock? */
   vmk_LockDomainID lockDomain;

   /** PCI device handle, resource*/
   vmk_PCIDevice pciDevice;
   vmk_PCIDeviceID pciId;
   vmk_PCIDeviceAddr sbdf;
   vmk_IOReservation pciResv;

   /** DMA engine */
   vmk_DMAEngine dmaEngine;
   /** Scatter-Gather handle */
   vmk_SgOpsHandle sgHandle;

   /** Interrupt cookies */
   vmk_IntrCookie *intrArray;
   /** Number of interrupt vectors allocated */
   vmk_uint32 numVectors;
   /** MSIx mode enabled */
   vmk_uint32 msixEnabled;


   /* Scsi Adapter */
   vmk_ScsiAdapter *scsiAdapter;
   /* Scsi DMA Engine */
   vmk_DMAEngine scsiDmaEngine;
   /* Scsi logical device */
   vmk_Device logicalDevice;
 
   /* Management handle */
   vmk_MgmtHandle mgmtHandle;
   /* Management interface signature definition */
   vmk_MgmtApiSignature nvmeSignature;
};


/**
 * Instance of Nvme_DriverResource. Defined as global, static variable
 * in nvme.c and is accessed by other components of the driver.
 */
extern Nvme_DriverResource __driverResource;


/**
 * Accessor macros for module-wide resources
 */
#define NVME_DRIVER_RES_HEAP_ID (__driverResource.heapId)
#define NVME_DRIVER_RES_LOG_HANDLE (__driverResource.logHandle)
#define NVME_DRIVER_RES_DRIVER_HANDLE (__driverResource.driverHandle)
#define NVME_DRIVER_RES_MEMPOOL (__driverResource.memPool)
#define NVME_DRIVER_RES_MGMT_HANDLE (__driverResource.mgmtHandle)
#define NVME_DRIVER_RES_PAGESLAB_ID (__driverResource.pageSlabId)
#define NVME_DRIVER_RES_LOCK (__driverResource.lock)
#define NVME_DRIVER_RES_ADAPTERLIST (__driverResource.adapters)


/**
 * Resource properties
 */
#define NVME_GLOBAL_LOCK_NAME  "nvmeGlobalLock"


/* Module-wide helper functions */



/**
 * Nvme_Alloc - allocate memory from driver's default heap.
 *
 * @param size size of memory to allocate
 * @param alignment alignment reqirement
 * @param zero whether the memory should be zeroed
 *
 * @return pointer to the memory in the heap
 * @return NULL allocation failed
 */
static inline void *
Nvme_Alloc(vmk_uint32 size, vmk_uint32 alignment, Nvme_AllocFlags zeroed)
{
   void *ret = NULL;

   /* No alignment required */
   if (alignment == 0) {
      ret = vmk_HeapAlloc(NVME_DRIVER_RES_HEAP_ID, size);
   } else {
      ret = vmk_HeapAlign(NVME_DRIVER_RES_HEAP_ID, size, alignment);
   }

   if (ret != NULL && zeroed) {
      vmk_Memset(ret, 0, size);
   }

   return ret;
}


/**
 * Nvme_Free - free memory allocated from the default heap
 *
 * @param mem memory to be freed
 */
static inline void
Nvme_Free(void *mem)
{
   vmk_HeapFree(NVME_DRIVER_RES_HEAP_ID, mem);
}


/**
 * Get the smaller value of a given type
 */
#define min_t(type,x,y) ({  \
   type _min1 = (x);        \
   type _min2 = (y);        \
   _min1 < _min2 ? _min1 : _min2;})


/**
 * Get the larger value of a given type
 */
#define max_t(type,x,y) ({ \
   type _max1 = (x);       \
   type _max2 = (y);       \
   _max1 > _max2 ? _max1 : _max2; })


typedef vmk_Lock OsLib_Lock_t;

// General locking will consolidate around using the same subQueue lock
#define LOCK_FUNC(qinfo)	(qinfo->lockFunc(qinfo->lock))
#define UNLOCK_FUNC(qinfo)	(qinfo->unlockFunc(qinfo->lock))
#define LOCK_INIT(qinfo)	(qinfo->lock = VMK_LOCK_INVALID)

#define LOCK_ASSERT_QLOCK_HELD(qinfo)\
      vmk_SpinlockAssertHeldByWorldInt(qinfo->lock)

#define LOCK_COMPQ(qinfo)	(qinfo->lockFunc(qinfo->compqLock))
#define UNLOCK_COMPQ(qinfo)	(qinfo->unlockFunc(qinfo->compqLock))

#define LOCK_ASSERT_CLOCK_HELD(qinfo)\
      vmk_SpinlockAssertHeldByWorldInt(qinfo->compqLock)



VMK_ReturnStatus
NvmeCtrlr_IntxAck(void *handlerData, vmk_IntrCookie intrCookie);
void
NvmeCtrlr_IntxHandler(void *handlerData, vmk_IntrCookie intrCookie);



#endif /* _NVME_OS_H */
