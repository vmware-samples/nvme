/*
 * ******************************************************************
 * Copyright (c) 2022-2025 Broadcom. All Rights Reserved.
 * Broadcom Confidential. The term "Broadcom" refers to Broadcom Inc.
 * and/or its subsidiaries.
 * ******************************************************************
 */

/**
 * @file: nvme_pcie_mgmt.c --
 *
 *   vmkapi_mgmt implementation of native nvme_pcie driver
 */

#include "nvme_pcie_mgmt.h"

static vmk_uint32
NVMEPCIEKeyGetHelpPage(vmk_uint8 *buf,
                       vmk_uint32 buf_len,
                       NVMEPCIEKVMgmtData *keyList,
                       vmk_uint32 keyNum);

// Controller key ops
#if NVME_PCIE_STORAGE_POLL
static VMK_ReturnStatus
NVMEPCIEKeyPollActGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPollActSet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPerfFSASet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAEvaSecGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAEvaSecSet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAEvaRatioGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAEvaRatioSet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAProtectSecGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAProtectSecSet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAMonSecGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAMonSecSet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAMonRatioGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAMonRatioSet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPollOIOThrGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPollOIOThrSet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPollIntervalGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPollIntervalSet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPerfStatsGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPerfStatsSet(vmk_uint64 cookie, void *keyVal);
#endif
#if NVME_PCIE_BLOCKSIZE_AWARE
static VMK_ReturnStatus
NVMEPCIEKeyBlkSizeAwarePollActGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyBlkSizeAwarePollActSet(vmk_uint64 cookie, void *keyVal);
#endif
static VMK_ReturnStatus NVMEPCIEKeyHelpGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus NVMEPCIEKeyHelpSet(vmk_uint64 cookie, void *keyVal);

// Global key ops
static VMK_ReturnStatus
NVMEPCIEKeyDebugMaskGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyDebugMaskSet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyLogLevelGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyLogLevelSet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus NVMEPCIEGlobalKeyHelpGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus NVMEPCIEGlobalKeyHelpSet(vmk_uint64 cookie, void *keyVal);

// Controller keys
static NVMEPCIEKVMgmtData nvmePCIEKVMgmtData[] = {
#if NVME_PCIE_STORAGE_POLL
   {
      "pollAct",
      VMK_MGMT_KEY_TYPE_LONG,
      NVMEPCIEKeyPollActGet,
      "Display hybrid poll activation info of the device.",
      NVMEPCIEKeyPollActSet,
      "Set pollAct, non-zero for activation, 0 for deactivation",
   },
   {
      "perfFSA",
      VMK_MGMT_KEY_TYPE_LONG,
      NVMEPCIEKeyPerfFSAGet,
      "Display whether Performance Finite State Automata enabled."
      " Valid if pollAct activated, default enabled.",
      NVMEPCIEKeyPerfFSASet,
      "Set perfFSA, non-zero to enable, 0 to disable.",
   },
   {
      "perfFSAEvaSec",
      VMK_MGMT_KEY_TYPE_LONG,
      NVMEPCIEKeyPerfFSAEvaSecGet,
      "Display perfFSA evaluation time window in seconds."
      " Valid if perfFSA enabled, default 2.",
      NVMEPCIEKeyPerfFSAEvaSecSet,
      "Set perfFSAEvaSec in seconds. Must not less than 1.",
   },
   {
      "perfFSAEvaRatio",
      VMK_MGMT_KEY_TYPE_LONG,
      NVMEPCIEKeyPerfFSAEvaRatioGet,
      "Display perfFSA evaluation ratio in percentage."
      " Valid if perfFSA enabled, default 10.",
      NVMEPCIEKeyPerfFSAEvaRatioSet,
      "Set perfFSAEvaSec in percentage.",
   },
   {
      "perfFSAProtectSec",
      VMK_MGMT_KEY_TYPE_LONG,
      NVMEPCIEKeyPerfFSAProtectSecGet,
      "Display perfFSA protection time window in seconds."
      " Valid if perfFSA enabled, default 4.",
      NVMEPCIEKeyPerfFSAProtectSecSet,
      "Set perfFSAProtectSec in seconds. Must not less than 1.",
   },
   {
      "perfFSAMonSec",
      VMK_MGMT_KEY_TYPE_LONG,
      NVMEPCIEKeyPerfFSAMonSecGet,
      "Display perfFSA monitor time window in seconds."
      " Valid if perfFSA enabled, default 60.",
      NVMEPCIEKeyPerfFSAMonSecSet,
      "Set perfFSAMonSec in seconds. Must not less than 1.",
   },
   {
      "perfFSAMonRatio",
      VMK_MGMT_KEY_TYPE_LONG,
      NVMEPCIEKeyPerfFSAMonRatioGet,
      "Display perfFSA monitor ratio in percentage."
      " Valid if perfFSA enabled, default 15.",
      NVMEPCIEKeyPerfFSAMonRatioSet,
      "Set perfFSAMonRatio in percentage.",
   },
   {
      "pollOIOThr",
      VMK_MGMT_KEY_TYPE_LONG,
      NVMEPCIEKeyPollOIOThrGet,
      "Display hybrid poll OIO activation threshold per queue of the device."
      " Valid if poll activated.",
      NVMEPCIEKeyPollOIOThrSet,
      "Set pollOIOThr",
   },
   {
      "pollInterval",
      VMK_MGMT_KEY_TYPE_LONG,
      NVMEPCIEKeyPollIntervalGet,
      "Display hybrid poll interval (us) per queue of the device."
      " Valid if poll activated.",
      NVMEPCIEKeyPollIntervalSet,
      "Set pollInterval",
   },
   {
      "perfStats",
      VMK_MGMT_KEY_TYPE_STRING,
      NVMEPCIEKeyPerfStatsGet,
      "Display the performance statistics of the device.",
      NVMEPCIEKeyPerfStatsSet,
      "Clear all the performance statistics to 0.",
   },
#endif
#if NVME_PCIE_BLOCKSIZE_AWARE
   {
      "blkSizeAwarePollAct",
      VMK_MGMT_KEY_TYPE_LONG,
      NVMEPCIEKeyBlkSizeAwarePollActGet,
      "Display hybrid poll block size aware activation info of the device."
      " Valid if poll activated.",
      NVMEPCIEKeyBlkSizeAwarePollActSet,
      "Set blkSizeAwarePollAct, non-zero for activation, 0 for deactivation",
   },
#endif
   // Should be always at the end
   {
      "help",
      VMK_MGMT_KEY_TYPE_STRING,
      NVMEPCIEKeyHelpGet,
      "Display the help page.",
      NVMEPCIEKeyHelpSet,
      NULL,
   }
};

// Global keys
static NVMEPCIEKVMgmtData nvmePCIEGlobalKVMgmtData[] = {
   {
      "logLevel",
      VMK_MGMT_KEY_TYPE_LONG,
      NVMEPCIEKeyLogLevelGet,
      "Display driver log level.",
      NVMEPCIEKeyLogLevelSet,
      "Set driver log level.\n"
      "\t\t\t1: Error\n"
      "\t\t\t2: Warning\n"
      "\t\t\t3: Info\n"
      "\t\t\t4: Verbose\n"
      "\t\t\t5: Debug",
   },
   {
      "debugMask",
      VMK_MGMT_KEY_TYPE_STRING,
      NVMEPCIEKeyDebugMaskGet,
      "Display driver debug level.",
      NVMEPCIEKeyDebugMaskSet,
      "Set driver debug level. Hexadecimal(started with \"0x\" or \"0X\") and decimal are both accepted.\n"
      "\t\t\tBIT_0: Print ctrlr level log.\n"
      "\t\t\tBIT_2: Print queue level log.\n"
      "\t\t\tBIT_3: Print command level log.\n"
      "\t\t\tBIT_18: Dump submission queue entry.\n"
      "\t\t\tBIT_19: Dump completion queue entry.",
   },
   // Should be always at the end
   {
      "help",
      VMK_MGMT_KEY_TYPE_STRING,
      NVMEPCIEGlobalKeyHelpGet,
      "Display the help page.",
      NVMEPCIEGlobalKeyHelpSet,
      NULL,
   }
};


#if NVME_PCIE_STORAGE_POLL
static VMK_ReturnStatus
NVMEPCIEKeyPollActGet(vmk_uint64 cookie, void *keyVal)
{
   vmk_uint64 *kv = (vmk_uint64 *) keyVal;
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;

   *kv = vmk_AtomicRead8(&ctrlr->pollAct);

   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyPollActSet(vmk_uint64 cookie, void *keyVal)
{
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;
   vmk_Bool pollAct = vmk_AtomicRead8(&ctrlr->pollAct);
   vmk_Bool val = (vmk_Strtoul((char *) keyVal, NULL, 10) != 0);

   if (pollAct != val) {
      if (val == VMK_FALSE) {
         vmk_AtomicWrite8(&ctrlr->pollAct, VMK_FALSE);

         IPRINT(ctrlr, "pollAct is set as 0.");
      } else {
         if (nvmePCIEMsiEnbaled) {
            IPRINT(ctrlr, "To activate polling, interrupt type should"
                          " be MSIX.");
         } else {
            vmk_AtomicWrite8(&ctrlr->pollAct, VMK_TRUE);
            IPRINT(ctrlr, "pollAct is set as 1.");
         }
      }
   }

   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAGet(vmk_uint64 cookie, void *keyVal)
{
   vmk_uint64 *kv = (vmk_uint64 *) keyVal;
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;

   *kv = vmk_AtomicRead8(&ctrlr->perfFSA);

   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyPerfFSASet(vmk_uint64 cookie, void *keyVal)
{
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;
   vmk_Bool perfFSA = (vmk_Strtoul((char *) keyVal, NULL, 10) != 0);

   vmk_AtomicWrite8(&ctrlr->perfFSA, perfFSA);

   IPRINT(ctrlr, "perfFSA is set as %d.", perfFSA);

   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAEvaSecGet(vmk_uint64 cookie, void *keyVal)
{
   vmk_uint64 *kv = (vmk_uint64 *) keyVal;
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;

   *kv = vmk_AtomicRead32(&ctrlr->perfFSAEvaSec);

   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAEvaSecSet(vmk_uint64 cookie, void *keyVal)
{
   VMK_ReturnStatus status = VMK_OK;
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;
   vmk_uint32 perfFSAEvaSec = vmk_Strtoul((char *) keyVal, NULL, 10);

   if (perfFSAEvaSec >= 1) {
      vmk_AtomicWrite32(&ctrlr->perfFSAEvaSec, perfFSAEvaSec);

      IPRINT(ctrlr, "perfFSAEvaSec is set as %d.", perfFSAEvaSec);
   } else {
      WPRINT(ctrlr, "perfFSAEvaSec must not be less than 1.");
      status = VMK_BAD_PARAM;
   }

   return status;
}


static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAEvaRatioGet(vmk_uint64 cookie, void *keyVal)
{
   vmk_uint64 *kv = (vmk_uint64 *) keyVal;
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;

   *kv = vmk_AtomicRead32(&ctrlr->perfFSAEvaRatio);

   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAEvaRatioSet(vmk_uint64 cookie, void *keyVal)
{
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;
   vmk_uint32 perfFSAEvaRatio = vmk_Strtoul((char *) keyVal, NULL, 10);

   vmk_AtomicWrite32(&ctrlr->perfFSAEvaRatio, perfFSAEvaRatio);

   IPRINT(ctrlr, "perfFSAEvaRatio is set as %d.", perfFSAEvaRatio);

   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAProtectSecGet(vmk_uint64 cookie, void *keyVal)
{
   vmk_uint64 *kv = (vmk_uint64 *) keyVal;
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;

   *kv = vmk_AtomicRead32(&ctrlr->perfFSAProtectSec);

   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAProtectSecSet(vmk_uint64 cookie, void *keyVal)
{
   VMK_ReturnStatus status = VMK_OK;
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;
   vmk_uint32 perfFSAProtectSec = vmk_Strtoul((char *) keyVal, NULL, 10);

   if (perfFSAProtectSec >= 1) {
      vmk_AtomicWrite32(&ctrlr->perfFSAProtectSec, perfFSAProtectSec);

      IPRINT(ctrlr, "perfFSAProtectSec is set as %d.", perfFSAProtectSec);
   } else {
      WPRINT(ctrlr, "perfFSAProtectSec must not be less than 1.");
      status = VMK_BAD_PARAM;
   }

   return status;
}


static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAMonSecGet(vmk_uint64 cookie, void *keyVal)
{
   vmk_uint64 *kv = (vmk_uint64 *) keyVal;
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;

   *kv = vmk_AtomicRead32(&ctrlr->perfFSAMonSec);

   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAMonSecSet(vmk_uint64 cookie, void *keyVal)
{
   VMK_ReturnStatus status = VMK_OK;
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;
   vmk_uint32 perfFSAMonSec = vmk_Strtoul((char *) keyVal, NULL, 10);

   if (perfFSAMonSec >= 1) {
      vmk_AtomicWrite32(&ctrlr->perfFSAMonSec, perfFSAMonSec);

      IPRINT(ctrlr, "perfFSAMonSec is set as %d.", perfFSAMonSec);
   } else {
      WPRINT(ctrlr, "perfFSAMonSec must not be less than 1.");
      status = VMK_BAD_PARAM;
   }

   return status;
}


static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAMonRatioGet(vmk_uint64 cookie, void *keyVal)
{
   vmk_uint64 *kv = (vmk_uint64 *) keyVal;
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;

   *kv = vmk_AtomicRead32(&ctrlr->perfFSAMonRatio);

   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyPerfFSAMonRatioSet(vmk_uint64 cookie, void *keyVal)
{
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;
   vmk_uint32 perfFSAMonRatio = vmk_Strtoul((char *) keyVal, NULL, 10);

   vmk_AtomicWrite32(&ctrlr->perfFSAMonRatio, perfFSAMonRatio);

   IPRINT(ctrlr, "perfFSAMonRatio is set as %d.", perfFSAMonRatio);

   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyPollOIOThrGet(vmk_uint64 cookie, void *keyVal)
{
   vmk_uint64 *kv = (vmk_uint64 *) keyVal;
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;

   *kv = vmk_AtomicRead32(&ctrlr->pollOIOThr);

   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyPollOIOThrSet(vmk_uint64 cookie, void *keyVal)
{
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;
   vmk_uint32 pollOIOThr = vmk_Strtoul((char *) keyVal, NULL, 10);

   vmk_AtomicWrite32(&ctrlr->pollOIOThr, pollOIOThr);

   IPRINT(ctrlr, "pollOIOThr is set as %d.", pollOIOThr);

   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyPollIntervalGet(vmk_uint64 cookie, void *keyVal)
{
   vmk_uint64 *kv = (vmk_uint64 *) keyVal;
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;

   *kv = vmk_AtomicRead64(&ctrlr->pollInterval);

   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyPollIntervalSet(vmk_uint64 cookie, void *keyVal)
{
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;
   vmk_uint64 pollInterval = vmk_Strtoul((char *) keyVal, NULL, 10);

   vmk_AtomicWrite64(&ctrlr->pollInterval, pollInterval);

   IPRINT(ctrlr, "pollInterval is set as %lu.", pollInterval);

   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyPerfStatsGet(vmk_uint64 cookie, void *keyVal)
{
   VMK_ReturnStatus status;
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;
   vmk_uint8 *buf = NULL;
   vmk_ByteCount out_len = 0;
   // Map all IO queue's pollLastDoSwitch into bit sequentially
   vmk_uint32 pollLastDoSwitch = 0;
   vmk_uint64 pollCount, pollBackToIntrCount, pollAccuCount;
   vmk_uint64 pollAccuCmd, pollCmdDone;
   vmk_uint64 intrCount = 0, intrCmdDone;
   vmk_uint32 i;
   const char perfStatsStr[] =
                        "{\n"
                        "\tpollLastDoSwitch bitmap of each IO queue: 0x%x,\n"
                        "\tpollCount: %lu,\n"
                        "\tpollBackToIntrCount: %lu,\n"
                        "\tpollBackToIntrCount ratio: %lu/10000,\n"
                        "\tpollAccuCount: %lu,\n"
                        "\tpollAccuCount ratio: %lu/10000,\n"
                        "\tpollCmdDone: %lu,\n"
                        "\tpollAccuCmd: %lu,\n"
                        "\tpollAccuCmd ratio: %lu/10000,\n"
                        "\tintrCount: %lu,\n"
                        "\tintrCmdDone: %lu,\n"
                        "}";

   buf = NVMEPCIEAlloc(NVMEPCIE_KVMGMT_BUF_SIZE, 0);
   if (buf == NULL) {
      WPRINT(ctrlr, "Failed to allocate buffer.");
      status = VMK_NO_MEMORY;

      goto fail_alloc_buf;
   }

   for (i = 1; i <= ctrlr->numIoQueues; i++) {
      pollLastDoSwitch |= vmk_AtomicRead8(&ctrlr->queueList[i].pollLastDoSwitch) << (i - 1);
      intrCount += vmk_AtomicRead64(&ctrlr->queueList[i].intrCount);
   }
   intrCmdDone = vmk_AtomicRead64(&ctrlr->perfStats.intrCmdDone);
   pollCount = vmk_AtomicRead64(&ctrlr->perfStats.pollCount);
   pollBackToIntrCount = vmk_AtomicRead64(&ctrlr->perfStats.pollBackToIntrCount);
   pollAccuCount = vmk_AtomicRead64(&ctrlr->perfStats.pollAccuCount);
   pollCmdDone = vmk_AtomicRead64(&ctrlr->perfStats.pollCmdDone);
   pollAccuCmd = vmk_AtomicRead64(&ctrlr->perfStats.pollAccuCmd);
   status = vmk_StringFormat(buf, NVMEPCIE_KVMGMT_BUF_SIZE,
                             &out_len, perfStatsStr,
                             pollLastDoSwitch, pollCount, pollBackToIntrCount,
                             pollCount ? ((pollBackToIntrCount * 10000) / pollCount) : 0,
                             pollAccuCount,
                             pollCount ? ((pollAccuCount * 10000) / pollCount) : 0,
                             pollCmdDone, pollAccuCmd,
                             pollCmdDone ? ((pollAccuCmd * 10000) / pollCmdDone) : 0,
                             intrCount, intrCmdDone);
   if (status != VMK_OK) {
      WPRINT(ctrlr, "Failed to get perfStats, %s.",
             vmk_StatusToString(status));

      goto fail_to_get_perfStats;
   }

   vmk_StringCopy(keyVal, buf, out_len + 1);

fail_to_get_perfStats:
   NVMEPCIEFree(buf);
fail_alloc_buf:
   return status;
}


static VMK_ReturnStatus
NVMEPCIEKeyPerfStatsSet(vmk_uint64 cookie, void *keyVal)
{
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;
   vmk_uint32 i;

   vmk_AtomicWrite64(&ctrlr->perfStats.pollCount, 0);
   vmk_AtomicWrite64(&ctrlr->perfStats.pollBackToIntrCount, 0);
   vmk_AtomicWrite64(&ctrlr->perfStats.pollAccuCount, 0);
   vmk_AtomicWrite64(&ctrlr->perfStats.pollAccuCmd, 0);
   vmk_AtomicWrite64(&ctrlr->perfStats.pollCmdDone, 0);
   for (i = 0; i <= ctrlr->numIoQueues; i++) {
      vmk_AtomicWrite64(&ctrlr->queueList[i].intrCount, 0);
      vmk_AtomicWrite8(&ctrlr->queueList[i].pollLastDoSwitch, 0);
   }
   vmk_AtomicWrite64(&ctrlr->perfStats.intrCmdDone, 0);

   return VMK_OK;
}
#endif


#if NVME_PCIE_BLOCKSIZE_AWARE
static VMK_ReturnStatus
NVMEPCIEKeyBlkSizeAwarePollActGet(vmk_uint64 cookie, void *keyVal)
{
   vmk_uint64 *kv = (vmk_uint64 *) keyVal;
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;

   *kv = vmk_AtomicRead8(&ctrlr->blkSizeAwarePollAct);

   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyBlkSizeAwarePollActSet(vmk_uint64 cookie, void *keyVal)
{
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;
   vmk_Bool blkSizeAwarePollAct = VMK_TRUE;

   if ((vmk_Strtoul((char *) keyVal, NULL, 10)) == 0) {
      blkSizeAwarePollAct = VMK_FALSE;
   }

   vmk_AtomicWrite8(&ctrlr->blkSizeAwarePollAct, blkSizeAwarePollAct);

   IPRINT(ctrlr, "blkSizeAwarePollAct is set as %d.", blkSizeAwarePollAct);

   return VMK_OK;
}
#endif


static vmk_uint32
NVMEPCIEKeyGetHelpPage(vmk_uint8 *buf, vmk_uint32 buf_len, NVMEPCIEKVMgmtData *keyList, vmk_uint32 keyNum)
{
   VMK_ReturnStatus status = VMK_OK;
   vmk_ByteCount len = 0;
   vmk_ByteCount out_len = 0;
   vmk_uint32 i = 0;

   status = vmk_StringFormat(buf + len, buf_len - len,
                             &out_len, "\nnvme_pcie help page:\n"
                             "\tKeyName\tOptions\n"
                             "\t-------\t-------\n\n");
   if (status != VMK_OK) {
      goto out_get_help_page;
   }
   len += out_len;

   for (i = 0; i < keyNum ; i++) {
      status = vmk_StringFormat(buf + len, buf_len - len,
                                &out_len, "\t%s\n",
                                keyList[i].keyName);
      if (status != VMK_OK) {
         goto out_get_help_page;
      }
      len += out_len;

      if (keyList[i].getDesc != NULL) {
         status = vmk_StringFormat(buf + len, buf_len - len, &out_len,
                                   "\t\t-g : %s\n",
                                   keyList[i].getDesc);
         if (status != VMK_OK) {
            goto out_get_help_page;
         }
         len += out_len;
      }

      if (keyList[i].setDesc != NULL) {
         status = vmk_StringFormat(buf + len, buf_len - len, &out_len,
                                   "\t\t-s : %s\n",
                                   keyList[i].setDesc);
         if (status != VMK_OK) {
            goto out_get_help_page;
         }
         len += out_len;
      }
   }

out_get_help_page:
   return len;
}


static VMK_ReturnStatus
NVMEPCIEKeyHelpGet(vmk_uint64 cookie, void *keyVal)
{
   NVMEPCIEController *ctrlr = (NVMEPCIEController *) cookie;
   vmk_uint8 *buf = NULL;
   vmk_uint32 len = 0;

   buf = NVMEPCIEAlloc(NVMEPCIE_KVMGMT_BUF_SIZE, 0);
   if (buf == NULL) {
      IPRINT(ctrlr, "Failed to allocate buffer.");
      goto out_help_get;
   }

   len = NVMEPCIEKeyGetHelpPage(buf,
                                NVMEPCIE_KVMGMT_BUF_SIZE,
                                nvmePCIEKVMgmtData,
                                sizeof(nvmePCIEKVMgmtData) / sizeof(NVMEPCIEKVMgmtData));
   vmk_StringCopy(keyVal, buf, len + 1);
   NVMEPCIEFree(buf);

out_help_get:
   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyHelpSet(vmk_uint64 cookie, void *keyVal)
{
   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEGlobalKeyHelpGet(vmk_uint64 cookie, void *keyVal)
{
   vmk_uint8 *buf = NULL;
   vmk_uint32 len = 0;

   buf = NVMEPCIEAlloc(NVMEPCIE_KVMGMT_BUF_SIZE, 0);
   if (buf == NULL) {
      MOD_IPRINT("Failed to allocate buffer.");
      goto out_help_get;
   }

   len = NVMEPCIEKeyGetHelpPage(buf,
                                NVMEPCIE_KVMGMT_BUF_SIZE,
                                nvmePCIEGlobalKVMgmtData,
                                sizeof(nvmePCIEGlobalKVMgmtData) / sizeof(NVMEPCIEKVMgmtData));
   vmk_StringCopy(keyVal, buf, len + 1);
   NVMEPCIEFree(buf);

out_help_get:
   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEGlobalKeyHelpSet(vmk_uint64 cookie, void *keyVal)
{
   return VMK_OK;
}


/**
 * Destory key-value management for the controller
 *
 * @param[in]    ctrlr       Controller instance
 *
 * @return void
 */
void
NVMEPCIEKeyValDestory(NVMEPCIEController *ctrlr)
{
   if (ctrlr->kvMgmtHandle != NULL) {
      IPRINT(ctrlr, "Destroy key-value management.");
      vmk_MgmtDestroy(ctrlr->kvMgmtHandle);

      ctrlr->kvMgmtHandle = NULL;
   }
}


/**
 * Init key-value management for the controller
 *
 * @param[in]    ctrlr       Controller instance
 *
 * @return VMK_OK on success, error code otherwise
 */
VMK_ReturnStatus
NVMEPCIEKeyValInit(NVMEPCIEController *ctrlr)
{
   VMK_ReturnStatus status = VMK_OK;
   vmk_uint32 i, keyNum;
   vmk_Name keyName;
   vmk_MgmtProps mgmtProps;

   if (ctrlr->kvMgmtHandle != NULL) {
      IPRINT(ctrlr, "Already init key-value management.");

      return status;
   }

   IPRINT(ctrlr, "Init key-value management.");

   ctrlr->kvMgmtSig.version = NVME_PCIE_KV_MGMT_VERSION;
   vmk_NameFormat(&ctrlr->kvMgmtSig.name,
                  "%s_%s", vmk_NvmeGetAdapterName(ctrlr->osRes.vmkAdapter),
                  NVMEPCIEGetCtrlrName(ctrlr));

   status = vmk_NameInitialize(&ctrlr->kvMgmtSig.vendor, "VMware");
   if (status != VMK_OK) {
      return status;
   }

   mgmtProps.modId = vmk_ModuleCurrentID;
   mgmtProps.heapId = NVME_PCIE_DRIVER_RES_HEAP_ID;
   mgmtProps.sig = &ctrlr->kvMgmtSig;
   mgmtProps.cleanupFn = NULL;
   mgmtProps.sessionAnnounceFn = NULL;
   mgmtProps.sessionCleanupFn = NULL;
   mgmtProps.handleCookie = (vmk_uint64) ctrlr;

   status = vmk_MgmtInit(&mgmtProps, &ctrlr->kvMgmtHandle);
   if (status != VMK_OK) {
      return status;
   }

   keyNum = sizeof(nvmePCIEKVMgmtData) / sizeof(NVMEPCIEKVMgmtData);
   for (i = 0; i < keyNum; i++) {
      vmk_NameInitialize(&keyName, nvmePCIEKVMgmtData[i].keyName);
      status = vmk_MgmtAddKey(ctrlr->kvMgmtHandle,
                              nvmePCIEKVMgmtData[i].type,
                              &keyName,
                              nvmePCIEKVMgmtData[i].getFn,
                              nvmePCIEKVMgmtData[i].setFn);
      if (status != VMK_OK) {
         return status;
      }
   }

   return status;
}


static VMK_ReturnStatus
NVMEPCIEKeyDebugMaskGet(vmk_uint64 cookie, void *keyVal)
{
   VMK_ReturnStatus status;
   char debugStr[40];

   status = vmk_StringFormat(debugStr, sizeof(debugStr), NULL, "0x%x", nvmePCIEDebugMask);
   if (status == VMK_OK) {
      status = vmk_StringCopy(keyVal, debugStr, sizeof(debugStr));
   }
   return status;
}


static VMK_ReturnStatus
NVMEPCIEKeyDebugMaskSet(vmk_uint64 cookie, void *keyVal)
{
   nvmePCIEDebugMask = vmk_Strtoul(keyVal, NULL, 0);
   MOD_IPRINT("Set driver debug mask to 0x%x.", nvmePCIEDebugMask);
   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyLogLevelGet(vmk_uint64 cookie, void *keyVal)
{
   vmk_uint64 *logPtr = (vmk_uint64 *)keyVal;
   *logPtr = vmk_LogGetCurrentLogLevel(NVME_PCIE_DRIVER_RES_LOG_HANDLE);
   return VMK_OK;
}


static VMK_ReturnStatus
NVMEPCIEKeyLogLevelSet(vmk_uint64 cookie, void *keyVal)
{
   VMK_ReturnStatus status;
   vmk_uint64 logLevel = vmk_Strtoul(keyVal, NULL, 10);

   if (logLevel < 1 || logLevel > 5) {
      return VMK_BAD_PARAM;
   }
   status = vmk_LogSetCurrentLogLevel(NVME_PCIE_DRIVER_RES_LOG_HANDLE, logLevel);
   if (status == VMK_OK) {
      MOD_IPRINT("Set driver log level to %lu.", logLevel);
   }
   return status;
}


/**
 * Init key-value management for the module
 *
 * @return VMK_OK on success, error code otherwise
 */
VMK_ReturnStatus
NVMEPCIEGlobalKeyValInit()
{
   VMK_ReturnStatus status = VMK_OK;
   vmk_uint32 i, keyNum;
   vmk_Name keyName;
   vmk_MgmtProps mgmtProps;
   vmk_MgmtApiSignature mgmtSig;

   if (NVME_PCIE_DRIVER_MGMT_HANDLE != NULL) {
      MOD_EPRINT("Global key-value management already exists.");
      return VMK_EXISTS;
   }

   vmk_Memset(&mgmtSig, 0, sizeof(mgmtSig));
   mgmtSig.version = NVME_PCIE_KV_MGMT_VERSION;
   vmk_NameFormat(&mgmtSig.name, "nvme_pcie");
   vmk_NameInitialize(&mgmtSig.vendor, "VMware");

   mgmtProps.modId = vmk_ModuleCurrentID;
   mgmtProps.heapId = NVME_PCIE_DRIVER_RES_HEAP_ID;
   mgmtProps.sig = &mgmtSig;
   mgmtProps.cleanupFn = NULL;
   mgmtProps.sessionAnnounceFn = NULL;
   mgmtProps.sessionCleanupFn = NULL;
   mgmtProps.handleCookie = 0L;

   status = vmk_MgmtInit(&mgmtProps, &NVME_PCIE_DRIVER_MGMT_HANDLE);
   if (status != VMK_OK) {
      MOD_EPRINT("Failed to initialize global key value management, %s",
                 vmk_StatusToString(status));
      return status;
   }

   keyNum = sizeof(nvmePCIEGlobalKVMgmtData) / sizeof(NVMEPCIEKVMgmtData);
   for (i = 0; i < keyNum; i++) {
      vmk_NameInitialize(&keyName, nvmePCIEGlobalKVMgmtData[i].keyName);
      status = vmk_MgmtAddKey(NVME_PCIE_DRIVER_MGMT_HANDLE,
                              nvmePCIEGlobalKVMgmtData[i].type,
                              &keyName,
                              nvmePCIEGlobalKVMgmtData[i].getFn,
                              nvmePCIEGlobalKVMgmtData[i].setFn);
      if (status != VMK_OK) {
         MOD_EPRINT("Failed to add key %s, %s",
                    nvmePCIEGlobalKVMgmtData[i].keyName,
                    vmk_StatusToString(status));
         return status;
      }
   }
   return VMK_OK;
}


/**
 * Destory key-value management for the module
 *
 */
void
NVMEPCIEGlobalKeyValDestroy()
{
   if (NVME_PCIE_DRIVER_MGMT_HANDLE != NULL) {
      MOD_IPRINT("Destroy global key-value management.");
      vmk_MgmtDestroy(NVME_PCIE_DRIVER_MGMT_HANDLE);
      NVME_PCIE_DRIVER_MGMT_HANDLE = NULL;
   }
}
