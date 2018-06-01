/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (c) 2012-2013  Integrated Device Technology, Inc.
 * All rights reserved.
 *******************************************************************************/

/*
 * @file: nvme_debug.c --
 *
 *    Development/debug facilities
 */
//#include "../../common/kernel/oslib_common.h"
#include "oslib.h"
//#include "../../common/kernel/nvme_private.h"
//#include "nvme_debug.h"


void
NvmeDebug_DumpSgArray(vmk_SgArray *sgArray)
{
   int i;
   DPRINT("sgArray: %p, numE: %d", sgArray, sgArray->numElems);
   for (i = 0; i < sgArray->numElems; i++) {
      DPRINT("\t %d/%d ioa: 0x%lx, length: %d", i, sgArray->numElems, sgArray->elem[i].ioAddr, sgArray->elem[i].length);
   }
}


void
NvmeDebug_DumpCdb(vmk_uint8 cdb[16])
{
   DPRINT("cdb: %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x",
      cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7], cdb[8], cdb[9], cdb[10],
      cdb[11], cdb[12], cdb[13], cdb[14], cdb[15]);
}


void
NvmeDebug_DumpCmd(struct nvme_cmd *cmd)
{
   int i;
   vmk_uint32 *ptr;
   ptr = (vmk_uint32 *)cmd;
   for (i=0; i<sizeof(struct nvme_cmd)/sizeof(vmk_uint32); i += 4) {
      DPRINT("%02x: %08x %08x %08x %08x", i,
         ptr[i], ptr[i+1], ptr[i+2], ptr[i+3]);
   }
}


void
NvmeDebug_DumpCpl(struct cq_entry *cqe)
{
   int  i;
   vmk_uint32 *ptr;
   ptr = (vmk_uint32 *)cqe;
   for (i = 0; i < sizeof(struct cq_entry)/sizeof(vmk_uint32); i += 4) {
      DPRINT("%02x: %08x %08x %08x %08x", i,
      ptr[i], ptr[i+1], ptr[i+2], ptr[i+3]);
   }
}

void
NvmeDebug_DumpUio(struct usr_io *uio)
{
   DPRINT("--- uio ---");
   NvmeDebug_DumpCmd(&uio->cmd);
   DPRINT("NS %d DI %d TO %ld ST %d DL %d ML %d DA 0x%lx MA 0x%lx",
      uio->namespaceID, uio->direction, uio->timeoutUs, uio->status, uio->length,
      uio->meta_length, uio->addr, uio->meta_addr);
}


/**
 * Dump sgPosition and PRPs from a command
 */
void
NvmeDebug_DumpPrps(struct NvmeCmdInfo *cmdInfo)
{
   struct NvmeCmdInfo *cmdBase = cmdInfo->cmdBase;
   vmk_ScsiCommand *vmkCmd;
   int prpNum = 0;
   int length = 0;
   int i;

   if (cmdBase == NULL) {
      cmdBase = cmdInfo;
   }

   GET_VMK_SCSI_CMD(cmdBase->cmdPtr, vmkCmd);

   if (cmdInfo->count == 0) {
      return;
   }

   length = VMK_PAGE_SIZE - (cmdInfo->nvmeCmd.header.prp[0].addr & VMK_PAGE_MASK);
   if (cmdInfo->count <= length) {
      prpNum = 1;
   } else {
      length = cmdInfo->count - length;
      prpNum = (length + VMK_PAGE_SIZE - 1) / VMK_PAGE_SIZE + 1;
   }

   DPRINT("cmd [%d] %p base %p vmkCmd %p[0x%x] lba 0x%lx lbc %d count %ld req %ld, prp %d",
      cmdInfo->cmdId, cmdInfo, cmdBase, vmkCmd, vmkCmd->cdb[0],
      vmkCmd->lba, vmkCmd->lbc, cmdInfo->count, cmdBase->requiredLength, prpNum);

   if (prpNum <= 2) {
      DPRINT("\t prp1: 0x%lx prp2: 0x%lx.", cmdInfo->nvmeCmd.header.prp[0].addr,
         cmdInfo->nvmeCmd.header.prp[1].addr);
   } else {
      DPRINT("\t prp1: 0x%lx prp2: 0x%lx.", cmdInfo->nvmeCmd.header.prp[0].addr,
         cmdInfo->nvmeCmd.header.prp[1].addr);
      for (i = 0; i < prpNum - 1; i += 8) {
         DPRINT("\t %04d: 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx", i,
                cmdInfo->prps[i].addr, cmdInfo->prps[i+1].addr, cmdInfo->prps[i+2].addr,
                cmdInfo->prps[i+3].addr, cmdInfo->prps[i+4].addr, cmdInfo->prps[i+5].addr,
                cmdInfo->prps[i+6].addr, cmdInfo->prps[i+7].addr);
      }
   }
}


/**
 * Dump namespace info
 */
void
NvmeDebug_DumpNsInfo(struct NvmeNsInfo *ns)
{
   DPRINT("ID %d FL 0x%x BC %ld LBAS %d FEAT 0x%02x FMLS %d "
      "MDCAP 0x%x PICAP 0x%x PISET 0x%x MDSZ %d EUI 0x%08lx",
      ns->id, ns->flags, ns->blockCount, ns->lbaShift, ns->feature,
      ns->fmtLbaSize, ns->metaDataCap, ns->dataProtCap, ns->dataProtSet,
      ns->metasize, ns->eui64);
}

/**
 * Dump timeout info
 */
#if USE_TIMER
void
NvmeDebug_DumpTimeoutInfo(struct NvmeQueueInfo *qinfo)
{
   int i;
   vmk_uint32 *ptr = (vmk_uint32 *) qinfo->timeoutCount;
   vmk_uint32 *ptrComp = (vmk_uint32 *) qinfo->timeoutComplCount;
   int ioTimeout = qinfo->ctrlr->ioTimeout;
   for (i = 0; i < ioTimeout; i++)
   {
      if (ptr[i] - (int)vmk_AtomicRead32(&ptrComp[i]))
      {
         DPRINT("non-zero qinfo %p [%d] timeout IDs: %02x: %08x, in timer %d", qinfo, qinfo->id,
                i, ptr[i] - (int)vmk_AtomicRead32(&ptrComp[i]), qinfo->ctrlr->timeoutId);
      }
   }
}
#endif

#if NVME_DEBUG_INJECT_ERRORS
static const struct NvmeDebug_ErrorCounterInfo errorCounters[] = {
   {
      .id         = NVME_DEBUG_ERROR_NONE,
      .seed       = 0,
      .likelyhood = 0,
      .name       = "None",
      .count      = 0,
   },
   {
      .id         = NVME_DEBUG_ERROR_ADMIN_TIMEOUT,
      .seed       = 0,
      .likelyhood = 0,
      .name       = "Admin command timeout",
      .count      = 0,
   },
   {
      .id         = NVME_DEBUG_ERROR_TIMEOUT,
      .seed       = 0,
      .likelyhood = 0,
      .name       = "IO command timeout",
      .count      = 0,
   },
   {
      .id         = NVME_DEBUG_ERROR_LAST,
      .seed       = 0,
      .likelyhood = 0,
      .name       = "Last",
      .count      = 0,
   }
};


VMK_ReturnStatus NvmeDebug_ErrorInjectInit(struct NvmeDebug_ErrorCounterInfo *errorCounterArg)
{
   vmk_uint8   i;
   for (i = NVME_DEBUG_ERROR_NONE; i <= NVME_DEBUG_ERROR_LAST; i++) {
      errorCounterArg[i] = errorCounters[i];
   }

   return VMK_OK;
}


vmk_Bool
NvmeDebug_ErrorCounterHit(struct NvmeDebug_ErrorCounterInfo *errorInfo)
{
   vmk_uint32 value;

   if (errorInfo->id <= NVME_DEBUG_ERROR_NONE || errorInfo->id >= NVME_DEBUG_ERROR_LAST) {
      return VMK_FALSE;
   }


   if (errorInfo->count <= 0) {
      return VMK_FALSE;
   }

   if (errorInfo->likelyhood == 0) {
      return VMK_FALSE;
   }

   if (errorInfo->seed == 0) {
      errorInfo->seed = vmk_GetRandSeed();
   }

   value = vmk_Rand(errorInfo->seed);
   errorInfo->seed = value;
   if (value % NVME_DEBUG_ERROR_RANGE < errorInfo->likelyhood) {
      errorInfo->count--;
      return VMK_TRUE;
   }

   return VMK_FALSE;
}
#endif /* NVME_DEBUG_INJECT_ERRORS */


/*
 * Dump the smart log information
 */
void
NvmeDebug_DumpSmart(struct smart_log *smartLog)
{
   if (!smartLog) {
      DPRINT("Null smart pointer!");
      return;
   }
   else {
      vmk_uint8 *smart = (vmk_uint8*)smartLog;
      int i;
      for(i = 0; i < sizeof(struct smart_log); i+=8) {
         DPRINT("\t %03d: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
                i, smart[i],smart[i+1],smart[i+2],smart[i+3],smart[i+4],
                smart[i+5],smart[i+6],smart[i+7]);
      }

   }
   DPRINT("dump smart log successfully!");
}

#if (NVME_ENABLE_STATISTICS == 1)
   inline void NvmeDebug_InitStatisticsData(STATS_StatisticData *statsData)
   {
      vmk_Memset(statsData, 0, sizeof(STATS_StatisticData));
   }
#endif
