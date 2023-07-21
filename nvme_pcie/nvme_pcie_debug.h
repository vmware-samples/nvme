/*****************************************************************************
 * Copyright (c) 2016-2018, 2020, 2023 VMware, Inc. All rights reserved.
 * -- VMware Confidential
 *****************************************************************************/

/*
 * @file: nvme_pcie_debug.h --
 *
 *    Log and debug interfaces for native nvme_pcie driver.
 */

#ifndef _NVME_DEBUG_H_
#define _NVME_DEBUG_H_

/**
 * Determine whether to enable debugging facilities in the driver
 *
 * 0 - Debugging facilities disabled
 * 1 - Debugging facilities enabled
 */
#ifndef NVME_DEBUG
#define NVME_DEBUG                     1
#endif

/**
 * Controls whether log messages should be printed based on the
 * current log level set to the log component.
 * TODO: Maybe we do not need so many log levels
 */
enum NVMEPCIELogLevel {
   NVME_LOG_LEVEL_ERROR = 1,
   NVME_LOG_LEVEL_WARNING,
   NVME_LOG_LEVEL_INFO,
   NVME_LOG_LEVEL_VERBOSE,
   NVME_LOG_LEVEL_DEBUG,
   NVME_LOG_LEVEL_LAST,
};

/**
 * Log prefix - printed in the beginning of every log message from this driver
 *
 * Format: CustomName:functionName:lineNumber:
 */
#define NVME_PCIE_LOG_PREFIX "%s:%s:%d:"

/**
 * Log messages with no handle.
 * The log prefix is driver name.
 * This is used when log handle is not initialized.
 */
#define NVMEPCIELogNoHandle(fmt, args...)       \
   do {                                         \
      vmk_LogMessage(NVME_PCIE_LOG_PREFIX fmt,  \
         NVME_PCIE_DRIVER_NAME,                 \
         __FUNCTION__,                          \
         __LINE__,                              \
         ##args);                               \
   } while(0)

/**
 * Log normal messages.
 *
 * @param[in] name       Log prefix
 * @param[in] level      Log level
 * @param[in] fmt        Format string
 * @param[in] args       List of message arguments
 */
#define NVMEPCIELog(name, level, fmt, args...) \
   do {                                        \
      vmk_LogLevel(VMK_LOG_URGENCY_NORMAL,     \
         NVME_PCIE_DRIVER_RES_LOG_HANDLE,      \
         level,                                \
         NVME_PCIE_LOG_PREFIX fmt "\n",        \
         name,                                 \
         __FUNCTION__,                         \
         __LINE__,                             \
         ##args);                              \
   } while (0)

/**
 * Log warning messages.
 *
 * @param[in] name       Log prefix
 * @param[in] level      Log level
 * @param[in] fmt        Format string
 * @param[in] args       List of message arguments
 */
#define NVMEPCIEWarning(name, level, fmt, args...) \
   do {                                        \
      vmk_LogLevel(VMK_LOG_URGENCY_WARNING,    \
         NVME_PCIE_DRIVER_RES_LOG_HANDLE,      \
         level,                                \
         NVME_PCIE_LOG_PREFIX fmt "\n",        \
         name,                                 \
         __FUNCTION__,                         \
         __LINE__,                             \
         ##args);                              \
   } while (0)

/**
 * Log alert messages.
 * This should be used to log severe problems.
 *
 * @param[in] name       Log prefix
 * @param[in] level      Log level
 * @param[in] fmt        Format string
 * @param[in] args       List of message arguments
 */
#define NVMEPCIEAlert(name, level, fmt, args...) \
   do {                                        \
      vmk_LogLevel(VMK_LOG_URGENCY_ALERT,      \
         NVME_PCIE_DRIVER_RES_LOG_HANDLE,      \
         level,                                \
         NVME_PCIE_LOG_PREFIX fmt "\n",        \
         name,                                 \
         __FUNCTION__,                         \
         __LINE__,                             \
         ##args);                              \
   } while (0)

/**
 * Log alert messages per controller
 */
#define ALERT(ctrlr, fmt, args...) \
   NVMEPCIEAlert(NVMEPCIEGetCtrlrName(ctrlr), NVME_LOG_LEVEL_ERROR, fmt, ##args)

/**
 * Log error messages per controller
 */
#define EPRINT(ctrlr, fmt, args...) \
   NVMEPCIEWarning(NVMEPCIEGetCtrlrName(ctrlr), NVME_LOG_LEVEL_ERROR, fmt, ##args)

/**
 * Log warning messages per controller
 */
#define WPRINT(ctrlr, fmt, args...) \
   NVMEPCIEWarning(NVMEPCIEGetCtrlrName(ctrlr), NVME_LOG_LEVEL_WARNING, fmt, ##args)

/**
 * Log information messages per controller
 */
#define IPRINT(ctrlr, fmt, args...) \
   NVMEPCIELog(NVMEPCIEGetCtrlrName(ctrlr), NVME_LOG_LEVEL_INFO, fmt, ##args)

/**
 * Log verbose messages per controller
 */
#define VPRINT(ctrlr, fmt, args...) \
   NVMEPCIELog(NVMEPCIEGetCtrlrName(ctrlr), NVME_LOG_LEVEL_VERBOSE, fmt, ##args)

/**
 * Log information messages
 */
#define MOD_IPRINT(fmt, args...) \
   NVMEPCIELog(NVME_PCIE_DRIVER_NAME, NVME_LOG_LEVEL_INFO, fmt, ##args)

/**
 * Log error messages
 */
#define MOD_EPRINT(fmt, args...) \
   NVMEPCIEWarning(NVME_PCIE_DRIVER_NAME, NVME_LOG_LEVEL_ERROR, fmt, ##args)

extern int nvmePCIEDebugMask;

#if NVME_DEBUG

/** Controller level log */
#define NVME_DEBUG_CTRLR  (1 << 0)
/** Namespace level log */
#define NVME_DEBUG_NS     (1 << 1)
/** Queue level log */
#define NVME_DEBUG_Q      (1 << 2)
/** NVM command log */
#define NVME_DEBUG_CMD    (1 << 3)
/** Admin command log */
#define NVME_DEBUG_ADMIN  (1 << 4)
/** Management log */
#define NVME_DEBUG_MGMT   (1 << 5)
/** Driver init/cleanup log */
#define NVME_DEBUG_INIT   (1 << 6)

/** Dump controller info */
#define NVME_DEBUG_DUMP_CTRLR      (1 << 16)
/** Dump namespace info */
#define NVME_DEBUG_DUMP_NS         (1 << 17)
/** Dump submission queue entry */
#define NVME_DEBUG_DUMP_SQE        (1 << 18)
/** Dump completion queue entry */
#define NVME_DEBUG_DUMP_CQE        (1 << 19)
/** Dump PRPs */
#define NVME_DEBUG_DUMP_PRP        (1 << 20)
/** Dump SGL */
#define NVME_DEBUG_DUMP_SGL        (1 << 21)

#define NVME_DEBUG_NONE    (0)
#define NVME_DEBUG_ALL  (0xffffffff)

/**
 * Log debug messages
 *
 * @param[in] mask       Debug mask
 * @param[in] fmt        Format string
 * @param[in] args       List of message arguments
 */
#define NVMEPCIEModDebug(mask, fmt, args...)  \
   if (mask & nvmePCIEDebugMask)              \
      NVMEPCIELog(NVME_PCIE_DRIVER_NAME, NVME_LOG_LEVEL_DEBUG, fmt, ##args)

/**
 * Log debug messages per controller
 *
 * @param[in] ctrlr      Controller instance
 * @param[in] mask       Debug mask
 * @param[in] fmt        Format string
 * @param[in] args       List of message arguments
 *
 * TODO: Currently use the global nvmePCIEDebugMask.
 *       If each controller has a debug mask in future
 *       implementation, use its own debug mask instead.
 */
#define NVMEPCIECtrlrDebug(ctrlr, mask, fmt, args...) \
   if (mask & nvmePCIEDebugMask)                      \
      NVMEPCIELog(NVMEPCIEGetCtrlrName(ctrlr), NVME_LOG_LEVEL_DEBUG, fmt, ##args)


/**
 * Log debug messages per controller without mask
 *
 * @param[in] ctrlr      Controller instance
 * @param[in] fmt        Format string
 * @param[in] args       List of message arguments
 */
#define DPRINT(ctrlr, fmt, args...) \
   NVMEPCIELog(NVMEPCIEGetCtrlrName(ctrlr), NVME_LOG_LEVEL_DEBUG, fmt, ##args)

/**
 * Wrappers of debug on different componets
 */
#define DPRINT_CTRLR(ctrlr, fmt, args...) \
   NVMEPCIECtrlrDebug(ctrlr, NVME_DEBUG_CTRLR, fmt, ##args)

#define DPRINT_NS(ctrlr, fmt, args...) \
   NVMEPCIECtrlrDebug(ctrlr, NVME_DEBUG_NS, fmt, ##args)

#define DPRINT_Q(ctrlr, fmt, args...) \
   NVMEPCIECtrlrDebug(ctrlr, NVME_DEBUG_Q, fmt, ##args)

#define DPRINT_MGMT(ctrlr, fmt, args...) \
   NVMEPCIECtrlrDebug(ctrlr, NVME_DEBUG_MGMT, fmt, ##args)

#define DPRINT_INIT(ctrlr, fmt, args...) \
   NVMEPCIECtrlrDebug(ctrlr, NVME_DEBUG_INIT, fmt, ##args)

#define DPRINT_CMD(ctrlr, qid, fmt, args...) \
   if (((qid == 0) && (nvmePCIEDebugMask & NVME_DEBUG_ADMIN)) ||  \
       ((qid > 0) && (nvmePCIEDebugMask & NVME_DEBUG_CMD))) \
      DPRINT(ctrlr, fmt, ##args)

#else

#define NVMEPCIEModDebug(mask, fmt, args...)
#define NVMEPCIECtrlrDebug(ctrlr, mask, fmt, args...)
#define DPRINT(ctrlr, fmt, args...)
#define DPRINT_CTRLR(ctrlr, fmt, args...)
#define DPRINT_NS(ctrlr, fmt, args...)
#define DPRINT_Q(ctrlr, fmt, args...)
#define DPRINT_MGMT(ctrlr, fmt, args...)
#define DPRINT_INIT(ctrlr, fmt, args...)
#define DPRINT_CMD(ctrlr, qid, fmt, args...)

#endif /* NVME_DEBUG */

#endif /* _NVME_DEBUG_H_ */
