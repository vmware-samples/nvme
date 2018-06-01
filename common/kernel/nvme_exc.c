/*******************************************************************************
 * Copyright (c) 2012-2014, Micron Technology, Inc.
 *******************************************************************************/

/**
 * @file  nvme_exc.c
 * @brief Exception module.
 */



#include "nvme_exc.h"
#include "nvme_private.h"


/*
 * Imported functions
 */
VMK_ReturnStatus scsiProcessCommand (void *clientData, void *cmdPtr,
      void *deviceData);
Nvme_Status NvmeCore_SubmitCommandAsync (struct NvmeQueueInfo *qinfo,
      struct NvmeCmdInfo *cmdInfo,
      NvmeCore_CompleteCommandCb cb);



/*
 * Critical warning bits in smart log
 */
enum {
   NVME_CRIT_WARNING_SPARE,
   NVME_CRIT_WARNING_OVER_TEMP,
   NVME_CRIT_WARNING_MEDIA_ERROR,
   NVME_CRIT_WARNING_READ_ONLY,
   NVME_CRIT_WARNING_VOLATLE_FAILED,
   NVME_CRIT_WARNING_LAST
};



#if ASYNC_EVENTS_ENABLED
static vmk_Bool NvmeExc_CheckCriticalError (vmk_uint8 criticalError)
{
   return (criticalError &
         ((1 << NVME_CRIT_WARNING_SPARE) | (1 << NVME_CRIT_WARNING_OVER_TEMP)
          | (1 << NVME_CRIT_WARNING_READ_ONLY) | (1 <<
             NVME_CRIT_WARNING_MEDIA_ERROR)));
}
#endif



/**
 * @brief Signal an exception event to the ExceptionHandler task.
 *
 * @param ctrlr
 * @param exceptionCode
 *
 * @return
 */
VMK_ReturnStatus NvmeExc_SignalException (struct NvmeCtrlr * ctrlr, vmk_uint64 exceptionCode)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;

   /*
    * Perform some preparation work prior to waking the exception handler
    */

   // Check if exception is still being processed.
   if (!NvmeExc_CheckExceptionPending (ctrlr, exceptionCode)) {
      NvmeExc_AtomicSetExceptionState (ctrlr, exceptionCode);

      DPRINT_EXC ("Signal exception = %lx, es = %lx", exceptionCode, NvmeExc_AtomicGetExceptionState(ctrlr));
      vmkStatus = vmk_WorldForceWakeup (ctrlr->exceptionHandlerTask);

      if (vmkStatus != VMK_OK) {
         EPRINT("Failed to signal %lx to exception handler task, 0x%x", exceptionCode, vmkStatus);
      }
   }
   return vmkStatus;
}



/**
 * @brief Polls until exception processing is cleared.
 *
 * @param ctrlr
 * @param exceptionCode
 * @param timeoutMs
 * @param pollIntervalUs
 * @param callerMsg
 *
 * @return
 */
static VMK_ReturnStatus WaitForException (struct NvmeCtrlr *ctrlr, vmk_uint64 exceptionCode,
      vmk_uint64 timeoutMs, vmk_uint64 pollIntervalUs,
      const char *callerMsg)
{
   vmk_Bool exceptionCleared;
   vmk_uint64 endTime;


   exceptionCleared = VMK_FALSE;

   endTime = OsLib_GetTimerUs () + (1000UL * timeoutMs);

   // Wait for the exception bit to clear for some amount of time
   while (OsLib_TimeAfter (OsLib_GetTimerUs (), endTime)) {
      if ((NvmeExc_AtomicGetExceptionState (ctrlr) & exceptionCode) == 0) {
         // exception bit has cleared, now test for the desired result.
         exceptionCleared = VMK_TRUE;
         break;
      }
      vmk_WorldSleep (pollIntervalUs);
   }

   if (exceptionCleared == VMK_FALSE) {
      EPRINT ("Exception timeout waiting for %lx to be processed: %s",
            exceptionCode, callerMsg);
      return VMK_TIMEOUT;
   }
   return VMK_OK;
}


//* Controller wide helper functions */
VMK_ReturnStatus NvmeExc_SignalExceptionAndWait (struct NvmeCtrlr * ctrlr,
      vmk_uint64 exceptionCode,
      vmk_uint32 timeoutMs)
{
   VMK_ReturnStatus vmkStatus;

   // Once we signal the exception, if it succeeds, we now for sure the
   // exception bit was set!
   vmkStatus = NvmeExc_SignalException (ctrlr, exceptionCode);
   if (vmkStatus == VMK_OK) {
      vmkStatus = WaitForException (ctrlr, exceptionCode, timeoutMs,
            WAIT_FOR_EXCEPTION_POLL_INTERVAL_US,
            __FUNCTION__);
   }

   return vmkStatus;
}



/**
 * @brief Exception task entry point.
 *
 * @param ctrlr
 */
void NvmeExc_ExceptionHandlerTask (struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;
   VMK_ReturnStatus wake;
   vmk_uint64 exceptionEvent;
   vmk_uint64 exceptionIgnoreMask = -1L;
#if ASYNC_EVENTS_ENABLED
   Nvme_CtrlrState       ctrlrState;
#endif
   Nvme_ResetType resetType;


   vmk_uint32 sleepTime = VMK_TIMEOUT_UNLIMITED_MS;

   struct smart_log *smartLog = NULL;
   struct error_log *errorLog = NULL;
   int    retry = 0;

   DPRINT("Exception task starting. The sleepTime is %d.", sleepTime);

   do
   {
#if USE_TIMER
      if (ctrlr->ioTimeout) {
         OsLib_StartIoTimeoutCheckTimer(ctrlr);
      }
#endif
      vmk_SpinlockLock (ctrlr->exceptionLock);
      wake = vmk_WorldWait (VMK_EVENT_NONE,
            ctrlr->exceptionLock,
            sleepTime, "Waiting for exceptions");

      DPRINT_EXC ("Exception task woke up wake = %x, exception = %lx", wake, NvmeExc_AtomicGetExceptionState (ctrlr));
#if USE_TIMER
      OsLib_StopIoTimeoutCheckTimer(ctrlr);
#endif

      if (VMK_OK == wake) {
         while (NvmeExc_AtomicGetExceptionState (ctrlr)) {
            /* Clear exceptions that are being ignored */
            NvmeExc_AtomicClrExceptionState (ctrlr, ~exceptionIgnoreMask);
            DPRINT_EXC ("Exception event = %lx", NvmeExc_AtomicGetExceptionState (ctrlr));
            exceptionEvent = NvmeExc_AtomicGetExceptionState (ctrlr);
            if (exceptionEvent  == 0)
               continue;

            /**
             * Events are handled in order of priority (ref : mtip32xx driver).\
             *    1. Device removal
             *    2. Driver shutdown
             *    3. Abort
             *    4. Device reset
             *    5. Error/Smart
             *    6. Start
             *    7. Quiesce
             */

            if (exceptionEvent & NVME_EXCEPTION_DEVICE_REMOVED) {
               /*  ForgetDevice  will fail all outstanding commands and set path lost by device. */
               NvmeCtrlr_Remove(ctrlr);
               /* Ignore all exception events except the shutdown and quiesce events. */
               exceptionIgnoreMask = NVME_EXCEPTION_TASK_SHUTDOWN | NVME_EXCEPTION_QUIESCE;
            }
            if (exceptionEvent & NVME_EXCEPTION_TASK_SHUTDOWN) {
               wake = VMK_DEATH_PENDING;
            }
#if USE_TIMER
            if  (exceptionEvent & NVME_EXCEPTION_TASK_TIMER) {
               if (VMK_TRUE == NvmeCtrlr_Timeout (ctrlr, &sleepTime)) {
                  #if (NVME_ENABLE_EXCEPTION_STATS == 1)
                     STATS_Increment(ctrlr->statsData.CMDTimeouts);
                  #endif
                  WPRINT("Detect IO timeout on %s, resetting controller.",
                         Nvme_GetCtrlrName(ctrlr));
                  NvmeCtrlr_HwReset (ctrlr, NULL, NVME_STATUS_TIMEOUT, VMK_TRUE);
               }
            }
#endif
            if (exceptionEvent &
                (NVME_EXCEPTION_TM_ABORT | NVME_EXCEPTION_TM_VIRT_RESET)) {
               vmkStatus = NvmeCtrlr_DoTaskMgmtAbort(ctrlr,
                                                     &(ctrlr->taskMgmtExcArgs.taskMgmt),
                                                     ctrlr->taskMgmtExcArgs.ns);
            }
            if (exceptionEvent & (NVME_EXCEPTION_TM_BUS_RESET |
                                  NVME_EXCEPTION_TM_LUN_RESET |
                                  NVME_EXCEPTION_TM_DEVICE_RESET)) {
               if (exceptionEvent & NVME_EXCEPTION_TM_BUS_RESET)
                  resetType = NVME_TASK_MGMT_BUS_RESET;
               else if (exceptionEvent & NVME_EXCEPTION_TM_LUN_RESET)
                  resetType = NVME_TASK_MGMT_LUN_RESET;
               else
                  resetType = NVME_TASK_MGMT_DEVICE_RESET;

               vmkStatus = NvmeCtrlr_DoTaskMgmtReset(ctrlr, resetType,
                                                     ctrlr->taskMgmtExcArgs.ns);
            }
#if ASYNC_EVENTS_ENABLED
            if (exceptionEvent & NVME_EXCEPTION_ERROR_CHECK) {
               VPRINT ("Read error Log");
               ctrlrState = NvmeState_GetCtrlrState(ctrlr);
               if (ctrlrState < NVME_CTRLR_STATE_INRESET) {
                  errorLog = Nvme_Alloc (ERR_LOG_PG_SIZE, 0, NVME_ALLOC_ZEROED);
                  if (!errorLog) {
                     EPRINT("Failed to allocate error log.");
                     if (ctrlrState == NVME_CTRLR_STATE_OPERATIONAL) {
                        // Clear the async event by controller reset.
                        EPRINT("Resetting controller.");
                        NvmeCtrlr_HwReset (ctrlr, NULL, NVME_STATUS_RESET, VMK_TRUE);
                     } else {
                        NvmeExc_RegisterForEvents(ctrlr);
                     }
                  } else {
                     retry = 0;
                     do {
                        vmkStatus = NvmeCtrlrCmd_GetLogPage(ctrlr, NVME_FULL_NAMESPACE,
                                                            GLP_ID_ERR_INFO, errorLog,
                                                            ERR_LOG_PG_SIZE);
                        if (vmkStatus != VMK_OK) {
                           EPRINT("Failed to get error log page, retry: %d.", retry++);
                        }
                     } while (vmkStatus != VMK_OK && retry < LOG_PAGE_MAX_RETRY);
                     if (vmkStatus != VMK_OK && NvmeState_GetCtrlrState(ctrlr) == NVME_CTRLR_STATE_OPERATIONAL) {
                        EPRINT("Resetting controller.");
                        NvmeCtrlr_HwReset (ctrlr, NULL, NVME_STATUS_RESET, VMK_TRUE);
                     } else {
                        NvmeExc_RegisterForEvents (ctrlr);
                     }
                     Nvme_Free (errorLog);
                  }
               }
            }
            if (exceptionEvent & NVME_EXCEPTION_HEALTH_CHECK) {
               VPRINT ("Read smart Log");
               ctrlrState = NvmeState_GetCtrlrState(ctrlr);
               if (ctrlrState < NVME_CTRLR_STATE_INRESET) {
                  smartLog = Nvme_Alloc (SMART_LOG_PG_SIZE, 0, NVME_ALLOC_ZEROED);
                  if (!smartLog) {
                     EPRINT("Failed to allocate smart log.");
                     if (ctrlrState == NVME_CTRLR_STATE_OPERATIONAL) {
                        // Clear the async event by controller reset.
                        EPRINT("Resetting controller.");
                        NvmeCtrlr_HwReset (ctrlr, NULL, NVME_STATUS_RESET, VMK_TRUE);
                     } else {
                        NvmeExc_RegisterForEvents (ctrlr);
                     }
                  } else {
                     retry = 0;
                     do {
                        vmkStatus = NvmeCtrlrCmd_GetLogPage(ctrlr, NVME_FULL_NAMESPACE,
                                                            GLP_ID_SMART_HEALTH,
                                                            smartLog,
                                                            SMART_LOG_PG_SIZE);
                        if (vmkStatus != VMK_OK) {
                           EPRINT("Failed to get smart log page, retry: %d.", retry++);
                        }
                     } while (vmkStatus != VMK_OK && retry < LOG_PAGE_MAX_RETRY);
                     if (vmkStatus != VMK_OK && NvmeState_GetCtrlrState(ctrlr) == NVME_CTRLR_STATE_OPERATIONAL) {
                        EPRINT("Resetting controller.");
                        NvmeCtrlr_HwReset (ctrlr, NULL, NVME_STATUS_RESET, VMK_TRUE);
                     } else {
                        if (vmkStatus == VMK_OK && NvmeExc_CheckCriticalError (smartLog->criticalError)) {
                           vmk_AtomicOr64(&ctrlr->healthMask, smartLog->criticalError);
                           EPRINT("Critical warnings detected in smart log [%x], "
                                  "failing controller", smartLog->criticalError);
                           NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_HEALTH_DEGRADED);
                        }
                        NvmeExc_RegisterForEvents (ctrlr);
                     }
                     Nvme_Free (smartLog);
                  }
               }
            }
#endif
            if (exceptionEvent & NVME_EXCEPTION_TASK_START) {
               vmkStatus = NvmeCtrlr_Start (ctrlr);
            }

            if (exceptionEvent & NVME_EXCEPTION_QUIESCE) {
#if ALLOW_IOS_IN_QUIESCED_STATE
               /**
                * Defer putting the controller in an idle state until
                * device driver is dettached.
                */
               ctrlrState = NvmeState_GetCtrlrState(ctrlr);
               if (ctrlrState == NVME_CTRLR_STATE_MISSING) {
                  DPRINT_EXC("Quiesce exception received in SRSI scenario");
               }
               else {
                  vmkStatus = NvmeCtrlr_Quiesce(ctrlr);
               }
#else
               vmkStatus = NvmeCtrlr_Stop(ctrlr);
#endif
            }
            NvmeExc_AtomicClrExceptionState (ctrlr, exceptionEvent);
         }
      }
   } while (wake != VMK_DEATH_PENDING);

   DPRINT("Exception handler exiting");
}



#if ASYNC_EVENTS_ENABLED

#define NVME_EVENT_DELAY_US (1000 * 100)
void NvmeExc_RegisterForEvents (struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus = VMK_OK;
   Nvme_CtrlrState       ctrlrState;


   ctrlrState = NvmeState_GetCtrlrState(ctrlr);
   if (ctrlrState > NVME_CTRLR_STATE_INRESET) {
      EPRINT("Async event registration requested while controller is in"
            " %s state.", NvmeState_GetCtrlrStateString(ctrlrState));
      return;
   }

   /*
    * Enable notifications coming from the controller
    */
   NvmeCtrlr_ConfigAsyncEvents (ctrlr, ASYNC_EVT_CFG_BITS);
   /*
    * Now arm the controller to send the event notifications
    */
   vmkStatus = NvmeCtrlrCmd_AsyncEventRequest (ctrlr);
   if (vmkStatus != VMK_OK) {
      EPRINT("Failed to send Async Event Request command, 0x%x.", vmkStatus);
   }
}
#endif
