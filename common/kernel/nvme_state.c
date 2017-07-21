/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (c) 2012-2013  Integrated Device Technology, Inc.
 * All rights reserved.
 *******************************************************************************/

/**
 * @file: nvme_state.c --
 *
 *    Manages NVMe driver state transitions
 */

#include "oslib.h"
#include "../../common/kernel/nvme_private.h"


static const char * Nvme_CtrlrStateStrings [] = {
   "Init",
   "Started",
   "Operational",
   "Suspend",
   "InReset",
   "Missing",
   "Quiesced",
   "Detached",
   "Failed",
   "Health Degraded",
   "Last",
};


const char *
NvmeState_GetCtrlrStateString(Nvme_CtrlrState state)
{
   if (state > NVME_CTRLR_STATE_LAST) {
      VMK_ASSERT(0);
      state = NVME_CTRLR_STATE_LAST;
   }
   return Nvme_CtrlrStateStrings[state];
}


Nvme_CtrlrState
NvmeState_GetCtrlrState(struct NvmeCtrlr *ctrlr, vmk_Bool locked)
{
   return (Nvme_CtrlrState)vmk_AtomicRead32(&ctrlr->atomicState);
}


Nvme_CtrlrState
NvmeState_SetCtrlrState(struct NvmeCtrlr *ctrlr, Nvme_CtrlrState state, vmk_Bool locked)
{
   Nvme_CtrlrState rc;
   do {
       rc = (Nvme_CtrlrState)vmk_AtomicRead32(&ctrlr->atomicState);
       if (((rc == NVME_CTRLR_STATE_MISSING) ||
           (rc == NVME_CTRLR_STATE_FAILED && state != NVME_CTRLR_STATE_MISSING))) {
          return rc;
       }
   } while (vmk_AtomicReadIfEqualWrite32(&ctrlr->atomicState,
               (vmk_uint32)rc, (vmk_uint32)state) != (vmk_uint32)rc);
   /** Return the previous state of the controller */
   return rc;
}
