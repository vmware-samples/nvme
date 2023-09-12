/*****************************************************************************
 * Copyright (c) 2016-2018, 2020, 2022-2023 VMware, Inc. All rights reserved.
 *****************************************************************************/

/*
 * @file: nvme_pcie_debug.c --
 *
 *   Development/debug facilities
 */

#include "nvme_pcie_int.h"

#if NVME_DEBUG

void
NVMEPCIEDumpSqe(NVMEPCIEController *ctrlr, vmk_NvmeSubmissionQueueEntry *sqe)
{
   if (VMK_LIKELY(!(nvmePCIEDebugMask & NVME_DEBUG_DUMP_SQE))) {
      return;
   }

   DPRINT(ctrlr, "sqe: %p", sqe);
   DPRINT(ctrlr, "\topc: 0x%x, fuse: 0x%x, psdt: 0x%x, cid: 0x%x, nsid: 0x%x",
          sqe->cdw0.opc, sqe->cdw0.fuse, sqe->cdw0.psdt, sqe->cdw0.cid, sqe->nsid);
   DPRINT(ctrlr, "\tmtpr: 0x%lx, prp1/sglLow: 0x%lx, prp2/sglHigh: 0x%lx",
          sqe->mptr, sqe->dptr.prps.prp1.pbao, sqe->dptr.prps.prp2.pbao);
   DPRINT(ctrlr, "\tcdw10: 0x%x, cdw11: 0x%x, cdw12: 0x%x",
          sqe->cdw10, sqe->cdw11, sqe->cdw12);
   DPRINT(ctrlr, "\tcdw13: 0x%x, cdw14: 0x%x, cdw15: 0x%x",
          sqe->cdw13, sqe->cdw14, sqe->cdw15);
}


void
NVMEPCIEDumpCqe(NVMEPCIEController *ctrlr, vmk_NvmeCompletionQueueEntry *cqe)
{
   if (VMK_LIKELY(!(nvmePCIEDebugMask & NVME_DEBUG_DUMP_CQE))) {
      return;
   }

   DPRINT(ctrlr, "cqe: %p", cqe);
   DPRINT(ctrlr, "\tdw0: 0x%x, dw1: 0x%x", cqe->dw0, cqe->dw1);
   DPRINT(ctrlr, "\tsqhd: 0x%x, sqid: 0x%x", cqe->dw2.sqhd, cqe->dw2.sqid);
   DPRINT(ctrlr, "\tcid: 0x%x, p: 0x%x, sc: 0x%x, sct: 0x%x, m: 0x%x, dnr: 0x%x",
          cqe->dw3.cid, cqe->dw3.p, cqe->dw3.sc, cqe->dw3.sct, cqe->dw3.m, cqe->dw3.dnr);
}


void
NVMEPCIEDumpSGL(NVMEPCIEController *ctrlr, vmk_SgArray *sgArray)
{
   int i;

   if (VMK_LIKELY(!(nvmePCIEDebugMask & NVME_DEBUG_DUMP_SGL))) {
      return;
   }

   if (sgArray == NULL) {
      return;
   }

   DPRINT(ctrlr, "sgArray: %p, numE: %d", sgArray, sgArray->numElems);
   for (i = 0; i < sgArray->numElems; i++) {
      DPRINT(ctrlr, "\t %d/%d ioa: 0x%lx, length: %d", i, sgArray->numElems-1,
             sgArray->elem[i].ioAddr, sgArray->elem[i].length);
   }
}

#else

void NVMEPCIEDumpSqe(NVMEPCIEController *ctrlr, vmk_NvmeSubmissionQueueEntry *sqe) {}
void NVMEPCIEDumpCqe(NVMEPCIEController *ctrlr, vmk_NvmeCompletionQueueEntry *cqe) {}
void NVMEPCIEDumpSGL(NVMEPCIEController *ctrlr, vmk_SgArray *sgArray) {}

#endif
