#ifndef _OSLIB_H_
#define _OSLIB_H_


/* Logging related stuff */


/**
 * Log prefix - printed in the beginning of every log message from this driver
 *
 * format: driverName:functionName:lineNumber:
 */
#define NVME_LOG_PREFIX "nvme:%s:%d:"


/**
 * Controls whether log messages should be printed based on the
 * current log level set to the log component.
 */
enum Nvme_LogLevel {
   NVME_LOG_LEVEL_ERROR = 1,
   NVME_LOG_LEVEL_WARNING,
   NVME_LOG_LEVEL_INFO,
   NVME_LOG_LEVEL_VERBOSE,
   NVME_LOG_LEVEL_DEBUG,
   NVME_LOG_LEVEL_LAST,
};


/**
 * Log message with no handle. This is used when log handle
 * is not initialized.
 */
#define Nvme_LogNoHandle(fmt, args...)       \
   do {                                      \
      vmk_LogMessage(NVME_LOG_PREFIX fmt,    \
         __FUNCTION__,                       \
         __LINE__,                           \
         ##args);                            \
   } while(0)


/**
 * Log message with level.
 */
#define Nvme_Log(level, fmt, args...)        \
   do {                                      \
      vmk_LogLevel(VMK_LOG_URGENCY_NORMAL,   \
         NVME_DRIVER_RES_LOG_HANDLE,         \
         level,                              \
         NVME_LOG_PREFIX fmt "\n",           \
         __FUNCTION__,                       \
         __LINE__,                           \
         ##args);                            \
   } while (0)


/**
 * Log debug messages
 */
#define Nvme_LogDebug(fmt, args...) \
   Nvme_Log(NVME_LOG_LEVEL_DEBUG, fmt, ##args)


/**
 * Log verbose messages
 */
#define Nvme_LogVerb(fmt, args...) \
   Nvme_Log(NVME_LOG_LEVEL_VERBOSE, fmt, ##args)


/**
 * Log information messages
 */
#define Nvme_LogInfo(fmt, args...) \
   Nvme_Log(NVME_LOG_LEVEL_INFO, fmt, ##args)


/**
 * Log warning messages
 */
#define Nvme_LogWarning(fmt, args...) \
   Nvme_Log(NVME_LOG_LEVEL_WARNING, fmt, ##args)


/**
 * Log error messages
 */
#define Nvme_LogError(fmt, args...) \
   Nvme_Log(NVME_LOG_LEVEL_ERROR, fmt, ##args)


/**
 * Data structure to track DMA buffer allocation
 */
struct NvmeDmaEntry {
   /** virtual address */
   vmk_VA va;
   /** I/O address, mapped through IOMMU */
   vmk_IOA ioa;
   /** size of the buffer */
   vmk_ByteCount size;
   /** TODO: can this be removed? accessory data for SG array used during allocation */
   vmk_SgArray *sgIn;
   /** TODO: can this be removed? accessory data for SG array used during allocation */
   vmk_SgArray *sgOut;
   /** dma operation direction */
   vmk_DMADirection direction;
   /** TODO: can this be removed? accessory data for machine page range used during map */
   vmk_MpnRange mpnRange;
};

/**
 * We still require struct NvmeCtrlr somewhere in this OS library wapper so far.
 */
struct NvmeCtrlr;


enum {
   NVME_LOCK_RANK_INVALID  = 0,
   NVME_LOCK_RANK_LOW,
   NVME_LOCK_RANK_MEDIUM,
   NVME_LOCK_RANK_HIGH,
};

VMK_ReturnStatus
OsLib_LockCreateNoRank(const char *name, vmk_Lock *lock);

VMK_ReturnStatus
OsLib_LockCreate(vmk_LockDomainID lockDomain, vmk_LockRank rank,
                 const char *name, vmk_Lock *lock);

VMK_ReturnStatus
OsLib_LockDestroy(vmk_Lock *lock);

VMK_ReturnStatus
OsLib_SemaphoreCreate(const char *name, int value, vmk_Semaphore *sema);

VMK_ReturnStatus
OsLib_SemaphoreDestroy(vmk_Semaphore *sema);

VMK_ReturnStatus
OsLib_DmaAlloc(struct NvmeCtrlr *ctrlr, vmk_ByteCount size,
               struct NvmeDmaEntry *dmaEntry);

VMK_ReturnStatus
OsLib_DmaFree(struct NvmeCtrlr *ctrlr, struct NvmeDmaEntry *dmaEntry);

VMK_ReturnStatus
OsLib_IntrRegister(vmk_Device device, vmk_IntrCookie intrCookie,
                   void *handlerData, int idx,
                   vmk_IntrAcknowledge intrAck, vmk_IntrHandler intrHandler);

VMK_ReturnStatus
OsLib_IntrUnregister(vmk_IntrCookie intrCookie, void *handlerData);

void
OsLib_StrToUpper(char *str, int length);

/**
 * Get microseconds since system boot
 */
static inline vmk_uint64
OsLib_GetTimerUs()
{
   return vmk_TimerUnsignedTCToUS(vmk_GetTimerCycles());
}


/**
 * If t1 > t0, returns difference between; otherwise returns 0.
 */
static inline vmk_uint64
OsLib_TimeAfter(vmk_uint64 t0, vmk_uint64 t1)
{
   if (t1 > t0) {
      return (t1 - t0);
   }

   return 0;
}

#endif /* _OSLIB_H_ */
