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
 * @file: nvme_private.h --
 *
 *    Private data structures and functions for native nvme driver.
 */

#ifndef _NVME_DEBUG_H_
#define _NVME_DEBUG_H_

#include "nvme_private.h"

#if NVME_DEBUG
extern int  nvme_dbg;
#endif

#if NVME_DEBUG

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

#define NVME_DEBUG_ALL  (BIT_0|BIT_1|BIT_2|BIT_3|BIT_4|BIT_5|BIT_6|BIT_7|\
         BIT_8|BIT_9|BIT_10|BIT_11|BIT_12|BIT_16|BIT_17|BIT_31)
#define NVME_DEBUG_IO   (BIT_0|BIT_1|BIT_2|BIT_3|BIT_4|BIT_31)
#define NVME_DEBUG_TIMEOUT (BIT_4)
#define NVME_DEBUG_IOCTL   (BIT_8|BIT_9|BIT_10)
#define NVME_DEBUG_DIF     (BIT_0|BIT_12|BIT_16)
#define NVME_DEBUG_LOG     (BIT_11)
#define NVME_DEBUG_DUMP    (BIT_16)
#define NVME_DEBUG_DUMP_CE (BIT_17)
#define NVME_DEBUG_DUMP_Q  (BIT_18)
#define NVME_DEBUG_DUMP_TIME     (BIT_19)
#define NVME_DEBUG_DUMP_SPLITCMD (BIT_20)
#define NVME_DEBUG_INIT    (BIT_30)
#define NVME_DEBUG_TEMP    (BIT_31)
#define NVME_DEBUG_NONE    (0)

#define  DPRINT(fmt, arg...)  \
   if (nvme_dbg & BIT_0) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT1(fmt, arg...) \
   if (nvme_dbg & BIT_1) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT2(fmt, arg...) \
   if (nvme_dbg & BIT_2) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT3(fmt, arg...) \
   if (nvme_dbg & BIT_3) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT4(fmt, arg...) \
   if (nvme_dbg & BIT_4) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT5(fmt, arg...) \
   if (nvme_dbg & BIT_5) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT6(fmt, arg...) \
   if (nvme_dbg & BIT_6) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT8(fmt, arg...) \
   if (nvme_dbg & BIT_8) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT9(fmt, arg...) \
   if (nvme_dbg & BIT_9) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT10(fmt, arg...)   \
   if (nvme_dbg & BIT_10) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT11(fmt, arg...)   \
   if (nvme_dbg & BIT_11) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT12(fmt, arg...)   \
   if (nvme_dbg & BIT_12) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINT31(fmt, arg...)   \
   if (nvme_dbg & BIT_31) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINTX(fmt, arg...) \
   if (nvme_dbg & BIT_0) \
      Nvme_LogDebug(fmt, ##arg)

#define  DPRINTX12(fmt, arg...)  \
   if (nvme_dbg & BIT_12) \
      Nvme_LogDebug(fmt, ##arg)

/**
 * Debug messages for init/cleanup routine
 */
#define  DPRINT_INIT(fmt, arg...)   \
   if (nvme_dbg & BIT_30) \
      Nvme_LogDebug(fmt, ##arg)

#else
#define  DPRINT(fmt, arg...)
#define  DPRINT1(fmt, arg...)
#define  DPRINT2(fmt, arg...)
#define  DPRINT3(fmt, arg...)
#define  DPRINT4(fmt, arg...)
#define  DPRINT5(fmt, arg...)
#define  DPRINT6(fmt, arg...)
#define  DPRINT8(fmt, arg...)
#define  DPRINT9(fmt, arg...)
#define  DPRINT10(fmt, arg...)
#define  DPRINT11(fmt, arg...)
#define  DPRINT12(fmt, arg...)
#define  DPRINT30(fmt, arg...)
#define  DPRINT_INIT(fmt, arg...)
#define  DPRINTX(fmt, arg...)
#define  DPRINTX12(fmt, arg...)
#endif


#define  EPRINT(fmt, arg...)  \
   Nvme_LogError(fmt, ##arg)

#define  NPRINT(fmt, arg...)  \
   Nvme_LogWarning(fmt, ##arg)

#define  IPRINT(fmt, arg...)  \
   Nvme_LogInfo(fmt, ##arg)


void NvmeDebug_DumpCmd(struct nvme_cmd *cmd);
void NvmeDebug_DumpCpl(struct cq_entry *cqe);
void NvmeDebug_DumpSgArray(vmk_SgArray *);
void NvmeDebug_DumpCdb(vmk_uint8 cdb[16]);
void NvmeDebug_DumpPrps(struct NvmeCmdInfo *cmdInfo);
void NvmeDebug_DumpUio(struct usr_io *uio);
void NvmeDebug_DumpNsInfo(struct NvmeNsInfo *ns);


#if NVME_DEBUG_INJECT_ERRORS
enum {
   NVME_DEBUG_ERROR_NONE = 0,
   NVME_DEBUG_ERROR_ADMIN_TIMEOUT,
   NVME_DEBUG_ERROR_TIMEOUT,
   NVME_DEBUG_ERROR_LAST,
};

struct NvmeDebug_ErrorCounterInfo {
   int        id;
   vmk_uint32 seed;
   vmk_uint32 likelyhood;
   char      *name;
};

#define NVME_DEBUG_ERROR_RANGE  1000

vmk_Bool NvmeDebug_ErrorCounterHit(int errorIndex);
#endif /* NVME_DEBUG_INJECT_ERRORS */

void NvmeDebug_DumpSmart(struct smart_log *smart);
#endif /* _NVME_DEBUG_H_ */
