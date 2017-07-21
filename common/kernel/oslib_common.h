/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (c) 2012-2013  Integrated Device Technology, Inc.
 * All rights reserved.
 *******************************************************************************/

#ifndef _OSLIB_COMMON_H_
#define _OSLIB_COMMON_H_

#include <vmkapi.h>

/* Logging related stuff */
/**
 * Controls whether log messages should be printed based on the
 * current log level set to the log component.
 */
enum Nvme_LogLevel {
   NVME_LOG_LEVEL_ERROR = 1,
   NVME_LOG_LEVEL_WARNING,
   NVME_LOG_LEVEL_INFO,
   NVME_LOG_LEVEL_VERBOSE,
   NVME_LOG_LEVEL_DEBUG,
   NVME_LOG_LEVEL_LAST,
};

enum {
   NVME_LOCK_RANK_INVALID  = 0,
   NVME_LOCK_RANK_LOW,
   NVME_LOCK_RANK_MEDIUM,
   NVME_LOCK_RANK_HIGH,
   NVME_LOCK_RANK_ULTRA,
};


/**
 * Log prefix - printed in the beginning of every log message from this driver
 *
 * format: driverName:functionName:lineNumber:
 */
#define NVME_LOG_PREFIX "nvme:%s:%d:"


/**
 * Log message with no handle. This is used when log handle
 * is not initialized.
 */
#define Nvme_LogNoHandle(fmt, args...)       \
   do {                                      \
      vmk_LogMessage(NVME_LOG_PREFIX fmt,    \
         __FUNCTION__,                       \
         __LINE__,                           \
         ##args);                            \
   } while(0)

/**
 * Data structure to track DMA buffer allocation
 */
struct NvmeDmaEntry {
   /** virtual address */
   vmk_VA va;
   /** I/O address, mapped through IOMMU */
   vmk_IOA ioa;
   /** size of the buffer */
   vmk_ByteCount size;
   /** TODO: can this be removed? accessory data for SG array used during allocation */
   vmk_SgArray *sgIn;
   /** TODO: can this be removed? accessory data for SG array used during allocation */
   vmk_SgArray *sgOut;
   /** dma operation direction */
   vmk_DMADirection direction;
   /** TODO: can this be removed? accessory data for machine page range used during map */
   vmk_MpnRange mpnRange;
};

#endif
