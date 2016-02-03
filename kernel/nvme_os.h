/*********************************************************************************
 * Copyright 2013 VMware, Inc. All rights reserved.
 * ******************************************************************************/

/**
*******************************************************************************
** Copyright (c) 2012-2013  Integrated Device Technology, Inc.               **
**                                                                           **
** All rights reserved.                                                      **
**                                                                           **
*******************************************************************************
**                                                                           **
** Redistribution and use in source and binary forms, with or without        **
** modification, are permitted provided that the following conditions are    **
** met:                                                                      **
**                                                                           **
**   1. Redistributions of source code must retain the above copyright       **
**      notice, this list of conditions and the following disclaimer.        **
**                                                                           **
**   2. Redistributions in binary form must reproduce the above copyright    **
**      notice, this list of conditions and the following disclaimer in the  **
**      documentation and/or other materials provided with the distribution. **
**                                                                           **
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS   **
** IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, **
** THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR    **
** PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR         **
** CONTRIBUTORS BE LIABLE FOR ANY DIRECT,INDIRECT, INCIDENTAL, SPECIAL,      **
** EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,       **
** PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR        **
** PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF    **
** LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING      **
** NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS        **
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.              **
**                                                                           **
** The views and conclusions contained in the software and documentation     **
** are those of the authors and should not be interpreted as representing    **
** official policies, either expressed or implied,                           **
** Integrated Device Technology Inc.                                         **
**                                                                           **
*******************************************************************************
**/

/*
 * @file: nvme_os.h --
 *
 *    OS specific types and functions.
 */

#ifndef _NVME_OS_H
#define _NVME_OS_H

#include <vmkapi.h>

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
 * Nvme_AllocFlags - flags for memory allocation
 */
typedef enum Nvme_AllocFlags{
   /** allocate memory without initialization */
   NVME_ALLOC_DEFAULT = VMK_FALSE,
   /** allocate memory and zero it out */
   NVME_ALLOC_ZEROED  = VMK_TRUE,
} Nvme_AllocFlags;


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
 * Read 32bit MMIO
 */
static inline vmk_uint32
Nvme_Readl(vmk_VA addr)
{
   vmk_CPUMemFenceRead();
   return (*(volatile vmk_uint32 *)(addr));
}


/**
 * Write to 32bit MMIO
 */
static inline void
Nvme_Writel(vmk_uint32 value, vmk_VA addr)
{
   vmk_CPUMemFenceWrite();
   (*(volatile vmk_uint32 *)(addr)) = value;
}


/**
 * Read 64bit MMIO
 */
static inline vmk_uint64
Nvme_Readq(vmk_VA addr)
{
   vmk_CPUMemFenceRead();
   return (*(volatile vmk_uint64 *)(addr));
}


/**
 * Write to 64bit MMIO
 */
static inline void
Nvme_Writeq(vmk_uint64 value, vmk_VA addr)
{
   vmk_CPUMemFenceWrite();
   Nvme_Writel(value & 0xffffffff, addr);
   Nvme_Writel((value & 0xffffffff00000000UL) >> 32, addr+4);
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

#endif /* _NVME_OS_H */
