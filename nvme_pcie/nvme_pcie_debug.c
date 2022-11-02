/*****************************************************************************
 * Copyright (c) 2016-2018, 2020 VMware, Inc. All rights reserved.
 *****************************************************************************/

/*
 * @file: nvme_pcie_debug.c --
 *
 *   Development/debug facilities
 */

#include "nvme_pcie_int.h"

void
NVMEPCIEDumpSqe(NVMEPCIEController *ctrlr, vmk_NvmeSubmissionQueueEntry *sqe)
{
   if (!(nvmePCIEDebugMask & NVME_DEBUG_DUMP_SQE)) {
      return;
   }

   DPRINT(ctrlr, "sqe: %p", sqe);
   DPRINT(ctrlr, "\topc: 0x%x, fuse: 0x%x, psdt: 0x%x, cid: 0x%x, nsid: 0x%x",
          sqe->cdw0.opc, sqe->cdw0.fuse, sqe->cdw0.psdt, sqe->cdw0.cid, sqe->nsid);
   DPRINT(ctrlr, "\tmtpr: 0x%lx", sqe->mptr);
   DPRINT(ctrlr, "\tprp1/sglLow: 0x%lx, prp2/sglHigh: 0x%lx",
          sqe->dptr.prps.prp1.pbao, sqe->dptr.prps.prp2.pbao);
   DPRINT(ctrlr, "\tcdw10: 0x%x", sqe->cdw10);
   DPRINT(ctrlr, "\tcdw11: 0x%x", sqe->cdw11);
   DPRINT(ctrlr, "\tcdw12: 0x%x", sqe->cdw12);
   DPRINT(ctrlr, "\tcdw13: 0x%x", sqe->cdw13);
   DPRINT(ctrlr, "\tcdw14: 0x%x", sqe->cdw14);
   DPRINT(ctrlr, "\tcdw15: 0x%x", sqe->cdw15);
}


void
NVMEPCIEDumpCqe(NVMEPCIEController *ctrlr, vmk_NvmeCompletionQueueEntry *cqe)
{
   if (!(nvmePCIEDebugMask & NVME_DEBUG_DUMP_CQE)) {
      return;
   }

   DPRINT(ctrlr, "cqe: %p", cqe);
   DPRINT(ctrlr, "\tdw0: 0x%x", cqe->dw0);
   DPRINT(ctrlr, "\tdw1: 0x%x", cqe->dw1);
   DPRINT(ctrlr, "\tsqhd: 0x%x, sqid: 0x%x", cqe->dw2.sqhd, cqe->dw2.sqid);
   DPRINT(ctrlr, "\tcid: 0x%x, p: 0x%x, sc: 0x%x, sct: 0x%x, m: 0x%x, dnr: 0x%x",
          cqe->dw3.cid, cqe->dw3.p, cqe->dw3.sc, cqe->dw3.sct, cqe->dw3.m, cqe->dw3.dnr);
}

void
NVMEPCIEDumpCommand(NVMEPCIEController *ctrlr, vmk_NvmeCommand *vmkCmd)
{
   if (!(nvmePCIEDebugMask & (NVME_DEBUG_DUMP_CQE | NVME_DEBUG_DUMP_SQE))) {
      return;
   }

   DPRINT(ctrlr, "vmkCmd: %p", vmkCmd);
   NVMEPCIEDumpSqe(ctrlr, &vmkCmd->nvmeCmd);
   NVMEPCIEDumpCqe(ctrlr, &vmkCmd->cqEntry);
   DPRINT(ctrlr, "vmkCmd->done: 0x%p", vmkCmd->done);
   DPRINT(ctrlr, "vmkCmd->doneData: 0x%p", vmkCmd->doneData);
   DPRINT(ctrlr, "vmkCmd->nvmeStatus: 0x%x", vmkCmd->nvmeStatus);
   return;
}
