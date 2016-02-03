/*********************************************************************************
 * Copyright (c) 2012-2014, Micron Technology, Inc.
 *******************************************************************************/

/**
* @brief nvme_exc.h
* Exception handler types and definitions
*/
#ifndef NVME_EXCEPTION_H
#define NVME_EXCEPTION_H

#include "oslib.h"

#define ASYNC_EVT_CFG_BITS       (0x1F)
#define NVME_TIMER_TIMEOUT_TICK  VMK_USEC_PER_SEC

enum Nvme_ExceptionCode {
	NVME_EXCEPTION_TASK_READY_BIT,
	NVME_EXCEPTION_TASK_START_BIT,
	NVME_EXCEPTION_TASK_SHUTDOWN_BIT,
	NVME_EXCEPTION_TM_ABORT_BIT,
	NVME_EXCEPTION_TM_VIRT_RESET_BIT,
	NVME_EXCEPTION_TM_LUN_RESET_BIT,
	NVME_EXCEPTION_TM_DEVICE_RESET_BIT,
	NVME_EXCEPTION_TM_BUS_RESET_BIT,
	NVME_EXCEPTION_HEALTH_CHECK_BIT,
	NVME_EXCEPTION_ERROR_CHECK_BIT,
	NVME_EXCEPTION_QUIESCE_BIT,
	NVME_EXCEPTION_DEVICE_REMOVED_BIT,
	NVME_EXCEPTION_TIMER_BIT,
	NVME_EXCEPTION_BIT_LAST=63
};
#define NVME_EXCEPTION_TASK_READY       (1 << NVME_EXCEPTION_TASK_READY_BIT)
#define NVME_EXCEPTION_TASK_START       (1 << NVME_EXCEPTION_TASK_START_BIT)
#define NVME_EXCEPTION_TASK_SHUTDOWN    (1 << NVME_EXCEPTION_TASK_SHUTDOWN_BIT)
#define NVME_EXCEPTION_TM_ABORT         (1 << NVME_EXCEPTION_TM_ABORT_BIT)
#define NVME_EXCEPTION_TM_VIRT_RESET    (1 << NVME_EXCEPTION_TM_VIRT_RESET_BIT)
#define NVME_EXCEPTION_TM_LUN_RESET     (1 << NVME_EXCEPTION_TM_LUN_RESET_BIT)
#define NVME_EXCEPTION_TM_DEVICE_RESET  (1 << NVME_EXCEPTION_TM_DEVICE_RESET_BIT)
#define NVME_EXCEPTION_TM_BUS_RESET     (1 << NVME_EXCEPTION_TM_BUS_RESET_BIT)
#define NVME_EXCEPTION_HEALTH_CHECK     (1 << NVME_EXCEPTION_HEALTH_CHECK_BIT)
#define NVME_EXCEPTION_ERROR_CHECK      (1 << NVME_EXCEPTION_ERROR_CHECK_BIT)
#define NVME_EXCEPTION_QUIESCE          (1 << NVME_EXCEPTION_QUIESCE_BIT)
#define NVME_EXCEPTION_DEVICE_REMOVED   (1 << NVME_EXCEPTION_DEVICE_REMOVED_BIT)
#define NVME_EXCEPTION_TASK_TIMER       (1 << NVME_EXCEPTION_TIMER_BIT)

#define TASKMGMT_TIMEOUT (3*60*1000L) // CAP_TO timeout maximum is 2 minutes. The extra minute takes into account the time to identify namespaces and re-create the queues.

enum AsyncEvent_ErrorStatus {
	ASYNC_EVENT_ERROR_IVALID_SQ,
	ASYNC_EVENT_ERROR_IVALID_DB_WRITE_VAL,
	ASYNC_EVENT_ERROR_DIAG_FAILURE,
	ASYNC_EVENT_ERROR_PERSISTENT,
	ASYNC_EVENT_ERROR_TRANSIENT,
	ASYNC_EVENT_ERROR_FW_LOAD,
	ASYNC_EVENT_ERROR_LAST
};

#define WAIT_FOR_EXCEPTION_POLL_INTERVAL_US (10000)
#define NVME_EXC_DELAY_US (700*1000)


/**
 * NvmeCtrl_AtomicSetExceptionState - use this to perform a setting of exception
 * bits.  You cannot clear bits in this operation and still maintain atomicity.
 *
 * @param ctrlr  Valid pointer to NvmeCtrlr instance
 * @param exceptionEventSetFlags Bits to set to '1'.
 */
#define NvmeExc_AtomicSetExceptionState(ctrlr, exceptionEventSetFlags) \
   vmk_AtomicOr64(&((ctrlr)->exceptionEvent), exceptionEventSetFlags)

/**
 * ahciAtomicClrExceptionState - use this to perform a clear of exception
 * bits. You cannot set bits in this operation and still maintain atomicity.
 *
 * @param adapter_state_ptr  Valid pointer to a devices static adapter state
 *                           AHCI_AdapterStateData structure.
 * @param mask_bits Create a bit mask with '1' in the positions you want to
 *                  clear.
 */
#define NvmeExc_AtomicClrExceptionState(ctrlr, mask_bits) \
   vmk_AtomicAnd64(&(ctrlr->exceptionEvent), ~(mask_bits))



/**
 * NvmeCtrl_AtomicGetExceptionState - Use this macro to acquire the exception events
 * status in its entirety.  It makes sure the operation is atomic.
 *
 * @param ctrlr  Valid pointer to a devices static adapter state
 *                           AHCI_AdapterStateData structure.
 */
#define NvmeExc_AtomicGetExceptionState(ctrlr) \
   vmk_AtomicRead64(&((ctrlr)->exceptionEvent))

/**
 * NvmeCtrl_AtomicCheckPendingException - Use this macro to acquire the exception events
 * status in its entirety.  It makes sure the operation is atomic.
 *
 * @param ctrlr  Valid pointer to a devices static adapter state
 *                           AHCI_AdapterStateData structure.
 */
#define NvmeExc_CheckExceptionPending(ctrlr, exceptionCode) \
   (vmk_AtomicRead64(&((ctrlr)->exceptionEvent)) & exceptionCode)



#if ASYNC_EVENTS_ENABLED
void NvmeExc_RegisterForEvents (struct NvmeCtrlr *ctrlr);
#endif


/*
 * Event signaling helper functions
 */
VMK_ReturnStatus NvmeExc_SignalException(struct NvmeCtrlr *ctrlr, vmk_uint64 exceptionCode);
VMK_ReturnStatus NvmeExc_SignalExceptionAndWait(struct NvmeCtrlr *ctrlr,  vmk_uint64 exceptionCode,
                                            vmk_uint32 timeoutMs);
void NvmeExc_ExceptionHandlerTask (struct NvmeCtrlr *ctrlr);
#endif
