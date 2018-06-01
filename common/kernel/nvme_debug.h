/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (c) 2012-2013  Integrated Device Technology, Inc.
 * All rights reserved.
 *******************************************************************************/

/*
 * @file: nvme_private.h --
 *
 *    Private data structures and functions for native nvme driver.
 */

#ifndef _NVME_DEBUG_H_
#define _NVME_DEBUG_H_

#ifdef ERRINJ
#define NVME_DEBUG_INJECT_ERRORS       (1)
#define NVME_DEBUG_INJECT_TIMEOUT         (1 && NVME_DEBUG_INJECT_ERRORS)
#define NVME_DEBUG_INJECT_STATE_DELAYS    (1 && NVME_DEBUG_INJECT_ERRORS)
#endif

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
 * Determine whether to enable error injection facilities in the driver
 *
 * 0 - Debugging facilities disabled
 * 1 - Debugging facilities enabled
 */
#ifndef NVME_DEBUG_INJECT_ERRORS
#define NVME_DEBUG_INJECT_ERRORS       (0)
#endif


/**
 * Determine whether to inject command timeout errors.
 *
 * 0 - timeout injection disabled
 * 1 - timeout injection enabled
 */
#ifndef NVME_DEBUG_INJECT_TIMEOUT
#define NVME_DEBUG_INJECT_TIMEOUT         (0 && NVME_DEBUG_INJECT_ERRORS)
#endif


/**
 * Determine whether to inject delays during stage stransitions for hot plug
 * testing.
 *
 * 0 - no delays
 * 1 - delay between state transitions
 */
#ifndef NVME_DEBUG_INJECT_STATE_DELAYS
#define NVME_DEBUG_INJECT_STATE_DELAYS    (0 && NVME_DEBUG_INJECT_ERRORS)
#endif

#if NVME_DEBUG_INJECT_STATE_DELAYS
/**
 * Time (in microseconds) to delay between state transitions.
 */
#define NVME_DEBUG_STATE_DELAY_US         (5 * 1000 * 1000)
#endif


#if NVME_DEBUG
extern int  nvme_dbg;

/**
 * Bitmap nvme_dbg:
 * 31                               15                              0
 * +----------------------------------------------------------------+
 * |   NVME_DEBUG_DUMP_x            |        DPRINTx                |
 * +----------------------------------------------------------------+
 *
 *
 */
#define BIT_0     (1 << 0)
#define BIT_1     (1 << 1)
#define BIT_2     (1 << 2)
#define BIT_3     (1 << 3)
#define BIT_4     (1 << 4)
#define BIT_5     (1 << 5)
#define BIT_6     (1 << 6)
#define BIT_7     (1 << 7)
#define BIT_8     (1 << 8)
#define BIT_9     (1 << 9)
#define BIT_10    (1 << 10)
#define BIT_11    (1 << 11)
#define BIT_12    (1 << 12)
#define BIT_13    (1 << 13)
#define BIT_14    (1 << 14)
#define BIT_15    (1 << 15)
#define BIT_16    (1 << 16)
#define BIT_17    (1 << 17)
#define BIT_18    (1 << 18)
#define BIT_19    (1 << 19)
#define BIT_20    (1 << 20)
#define BIT_21    (1 << 21)
#define BIT_22    (1 << 22)
#define BIT_23    (1 << 23)
#define BIT_24    (1 << 24)
#define BIT_25    (1 << 25)
#define BIT_26    (1 << 26)
#define BIT_27    (1 << 27)
#define BIT_28    (1 << 28)
#define BIT_29    (1 << 29)
#define BIT_30    (1 << 30)
#define BIT_31    (1 << 31)

/**
 * BIT_0: Ctrlr
 * BIT_1: Namespace
 * BIT_2: Queue
 * BIT_3: Command
 */
#define NVME_DEBUG_IO   (BIT_0|BIT_1|BIT_2|BIT_3)

/**
 * BIT_4: Admin command
 * BIT_5: Management
 */
#define NVME_DEBUG_IOCTL   (BIT_4|BIT_5)

/**
 * BIT_4: Admin command
 */
#define NVME_DEBUG_ADMIN   (BIT_4)

/**
 * BIT_6: Exception handler
 */
#define NVME_DEBUG_EXC    (BIT_6)

/**
 * BIT_3: IO command
 * BIT_7: Split command
 */
#define NVME_DEBUG_SPLIT    (BIT_7 | BIT_3)

/**
 * BIT_14: Init/cleanup routine
 */
#define NVME_DEBUG_INIT    (BIT_14)

/**
 * BIT_15: temp use, like entry of function.
 */
#define NVME_DEBUG_TEMP    (BIT_15)

/**
 * Switch for debug utils in nvme_debug.c
 * */
#define NVME_DEBUG_DUMP_SG      (BIT_16)
#define NVME_DEBUG_DUMP_PRP     (BIT_17)
#define NVME_DEBUG_DUMP_CDB     (BIT_18)
#define NVME_DEBUG_DUMP_CMD     (BIT_19)
#define NVME_DEBUG_DUMP_CPL     (BIT_20)
#define NVME_DEBUG_DUMP_UIO     (BIT_21)
#define NVME_DEBUG_DUMP_NS      (BIT_22)
#define NVME_DEBUG_DUMP_TIMEOUT (BIT_23)
#define NVME_DEBUG_DUMP_SMART   (BIT_24)

#define NVME_DEBUG_NONE    (0)
#define NVME_DEBUG_DPRINT_ALL  (0xffff)
#define NVME_DEBUG_DUMP_ALL (0xffff0000) 
#define NVME_DEBUG_ALL  (0xffffffff)

/**
 * DPRINT shall print the log once setting nvme_log_devel as DEBUG
 */
#define  DPRINT(fmt, arg...)  \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT_CTRLR(fmt, arg...) \
   if (nvme_dbg & BIT_0) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT_NS(fmt, arg...) \
   if (nvme_dbg & BIT_1) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT_Q(fmt, arg...) \
   if (nvme_dbg & BIT_2) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT_CMD(fmt, arg...) \
   if (nvme_dbg & BIT_3) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT_ADMIN(fmt, arg...) \
   if (nvme_dbg & BIT_4) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT_MGMT(fmt, arg...) \
   if (nvme_dbg & BIT_5) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT_EXC(fmt, arg...) \
   if (nvme_dbg & BIT_6) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT_SPLIT(fmt, arg...)   \
   if (nvme_dbg & NVME_DEBUG_SPLIT) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT_TIMEOUT(fmt, arg...)   \
   if (nvme_dbg & BIT_13) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT_INIT(fmt, arg...)   \
   if (nvme_dbg & BIT_14) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT_TEMP(fmt, arg...)   \
   if (nvme_dbg & BIT_15) \
      Nvme_LogDebug(fmt, ##arg)

#else
#define  DPRINT(fmt, arg...)
#define  DPRINT_CTRLR(fmt, arg...)
#define  DPRINT_NS(fmt, arg...)
#define  DPRINT_Q(fmt, arg...)
#define  DPRINT_CMD(fmt, arg...)
#define  DPRINT_ADMIN(fmt, arg...)
#define  DPRINT_MGMT(fmt, arg...)
#define  DPRINT_EXC(fmt, arg...)
#define  DPRINT_SPLIT(fmt, arg...)
#define  DPRINT_TIMEOUT(fmt, arg...)
#define  DPRINT_INIT(fmt, arg...)
#define  DPRINT_TEMP(fmt, arg...)
#endif

#define  EPRINT(fmt, arg...)  \
   Nvme_LogError(fmt, ##arg)

#define  WPRINT(fmt, arg...)  \
   Nvme_LogWarning(fmt, ##arg)

#define  IPRINT(fmt, arg...)  \
   Nvme_LogInfo(fmt, ##arg)

#define  VPRINT(fmt, arg...)  \
   Nvme_LogVerb(fmt, ##arg)

#define NVME_DEBUG_ERROR_RANGE  1000

enum {
   NVME_DEBUG_ERROR_NONE = 0,
   NVME_DEBUG_ERROR_ADMIN_TIMEOUT,
   NVME_DEBUG_ERROR_TIMEOUT,
   NVME_DEBUG_ERROR_LAST,
   NVME_DEBUG_NUM_ERRORS,
};

struct NvmeDebug_ErrorCounterInfo {
   int        id;
   vmk_uint32 seed;
   vmk_uint32 likelyhood;
   char      *name;
   int       count;
};


#if NVME_DEBUG_INJECT_ERRORS

VMK_ReturnStatus NvmeDebug_ErrorInjectInit(struct NvmeDebug_ErrorCounterInfo *errorCounter);
vmk_Bool NvmeDebug_ErrorCounterHit(struct NvmeDebug_ErrorCounterInfo *errorInfo);

#endif /* NVME_DEBUG_INJECT_ERRORS */

#if (NVME_ENABLE_STATISTICS == 1)
   void NvmeDebug_InitStatisticsData(STATS_StatisticData *statsData);
#endif

#endif /* _NVME_DEBUG_H_ */
