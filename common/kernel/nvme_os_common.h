/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (c) 2012-2013  Integrated Device Technology, Inc.
 * All rights reserved.
 *******************************************************************************/

/*
 * @file: nvme_os_common.h --
 *
 *    OS agnostic types and functions.
 */

#ifndef _NVME_OS_COMMON_H
#define _NVME_OS_COMMON_H
#include <vmkapi.h>


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



#endif
