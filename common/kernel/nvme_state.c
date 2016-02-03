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
   Nvme_CtrlrState rc;

   if (locked) {
      vmk_SpinlockLock(ctrlr->lock);
   }

   rc = ctrlr->state;

   if (locked) {
      vmk_SpinlockUnlock(ctrlr->lock);
   }

   return rc;
}


Nvme_CtrlrState
NvmeState_SetCtrlrState(struct NvmeCtrlr *ctrlr, Nvme_CtrlrState state, vmk_Bool locked)
{
   Nvme_CtrlrState rc;

   if (locked) {
      vmk_SpinlockLock(ctrlr->lock);
   }

   rc = ctrlr->state;

   /*
    * Do not move to other state when
    * 1. current state is missing, or
    * 2. current state is failed but target state is not missing,
    *    i.e. if target state is missing, we should change state to missing.
    */

   if (!((rc == NVME_CTRLR_STATE_MISSING) ||
       (rc == NVME_CTRLR_STATE_FAILED && state != NVME_CTRLR_STATE_MISSING))) {
      ctrlr->state = state;
   }

   VPRINT("State transitioned from %s to %s.",
          NvmeState_GetCtrlrStateString(rc),
          NvmeState_GetCtrlrStateString(ctrlr->state));

   if (locked) {
      vmk_SpinlockUnlock(ctrlr->lock);
   }

   return rc;
}
