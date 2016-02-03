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
 * @file: nvme_debug.c --
 *
 *    Development/debug facilities
 */

#include "nvme_private.h"
#include "nvme_debug.h"


void
NvmeDebug_DumpSgArray(vmk_SgArray *sgArray)
{
   int i;
   Nvme_LogDebug("sgArray: %p, numE: %d", sgArray, sgArray->numElems);
   for (i = 0; i < sgArray->numElems; i++) {
      Nvme_LogDebug("\t %d/%d ioa: 0x%lx, length: %d", i, sgArray->numElems, sgArray->elem[i].ioAddr, sgArray->elem[i].length);
   }
}


void
NvmeDebug_DumpCdb(vmk_uint8 cdb[16])
{
   Nvme_LogDebug("cdb: %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x",
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
      Nvme_LogDebug("%02x: %08x %08x %08x %08x", i,
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
      Nvme_LogDebug("%02x: %08x %08x %08x %08x", i,
      ptr[i], ptr[i+1], ptr[i+2], ptr[i+3]);
   }
}

void
NvmeDebug_DumpUio(struct usr_io *uio)
{
   Nvme_LogDebug("--- uio ---");
   NvmeDebug_DumpCmd(&uio->cmd);
   Nvme_LogDebug("NS %d DI %d TO %ld ST %d DL %d ML %d DA 0x%lx MA 0x%lx",
      uio->namespace, uio->direction, uio->timeoutUs, uio->status, uio->length,
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

	if (cmdBase == NULL) {
		cmdBase = cmdInfo;
	}

	vmkCmd = cmdBase->vmkCmd;

	Nvme_LogDebug("cmd %d info %p base %p vmkCmd %p[0x%x] lba 0x%lx lbc %d count %ld req %ld.",
		cmdInfo->cmdId, cmdInfo, cmdBase, vmkCmd, vmkCmd->cdb[0],
		vmkCmd->lba, vmkCmd->lbc, cmdInfo->count, cmdBase->requiredLength);

	if (cmdInfo->count < VMK_PAGE_SIZE) {
		Nvme_LogDebug("\t prp1: 0x%lx prp2: 0x%lx.", cmdInfo->nvmeCmd.header.prp[0].addr,
			cmdInfo->nvmeCmd.header.prp[1].addr);
	} else {
		int i;
		Nvme_LogDebug("\t prp1: 0x%lx prp2: 0x%lx.", cmdInfo->nvmeCmd.header.prp[0].addr,
			cmdInfo->nvmeCmd.header.prp[1].addr);
		for (i = 0; i < cmdInfo->count / VMK_PAGE_SIZE + 3; i += 8) {
			Nvme_LogDebug("\t 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx",
				cmdInfo->prps[i].addr, cmdInfo->prps[i+1].addr, cmdInfo->prps[i+2].addr, cmdInfo->prps[i+3].addr,
				cmdInfo->prps[i+4].addr, cmdInfo->prps[i+5].addr, cmdInfo->prps[i+6].addr, cmdInfo->prps[i+7].addr);
		}
	}
}


/**
 * Dump namespace info
 */
void
NvmeDebug_DumpNsInfo(struct NvmeNsInfo *ns)
{
   Nvme_LogDebug("ID %d FL 0x%x BC %ld LBAS %d FEAT 0x%02x FMLS %d "
      "MDCAP 0x%x PICAP 0x%x PISET 0x%x MDSZ %d EUI 0x%08lx",
      ns->id, ns->flags, ns->blockCount, ns->lbaShift, ns->feature,
      ns->fmtLbaSize, ns->metaDataCap, ns->dataProtCap, ns->dataProtSet,
      ns->metasize, ns->eui64);
}


#if NVME_DEBUG_INJECT_ERRORS
struct NvmeDebug_ErrorCounterInfo errorCounters[] = {
   {
      .id         = NVME_DEBUG_ERROR_NONE,
      .seed       = 0,
      .likelyhood = 0,
      .name       = "None",
   },
   {
      .id         = NVME_DEBUG_ERROR_ADMIN_TIMEOUT,
      .seed       = 0,
      .likelyhood = 0,
      .name       = "Admin command timeout",
   },
   {
      .id         = NVME_DEBUG_ERROR_TIMEOUT,
      .seed       = 0,
      .likelyhood = 0,
      .name       = "IO command timeout",
   },
   {
      .id         = NVME_DEBUG_ERROR_LAST,
      .seed       = 0,
      .likelyhood = 0,
      .name       = "Last",
   }
};


vmk_Bool
NvmeDebug_ErrorCounterHit(int errorIndex)
{
   struct NvmeDebug_ErrorCounterInfo *errorInfo;
   vmk_uint32 value;

   if (errorIndex <= NVME_DEBUG_ERROR_NONE || errorIndex >= NVME_DEBUG_ERROR_LAST) {
      return VMK_FALSE;
   }

   errorInfo = &errorCounters[errorIndex];
   VMK_ASSERT(errorInfo->id == errorIndex);

   if (errorInfo->likelyhood == 0) {
      return VMK_FALSE;
   }

   if (errorInfo->seed == 0) {
      errorInfo->seed = vmk_GetRandSeed();
   }

   value = vmk_Rand(errorInfo->seed);
   errorInfo->seed = value;
   if (value % NVME_DEBUG_ERROR_RANGE < errorInfo->likelyhood) {
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
      Nvme_LogInfo("Null smart pointer!");
      return;
   }
   else {
      vmk_uint8 *smart = (vmk_uint8*)smartLog;
      int i;
      for(i = 0; i<sizeof(struct smart_log); i+=8) {
         Nvme_LogInfo("\t 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",
                     smart[i],smart[i+1],smart[i+2],smart[i+3],smart[i+4],
                     smart[i+5],smart[i+6],smart[i+7]);
      }

   }
    Nvme_LogInfo("dump smart log successfully!");

}
