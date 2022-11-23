/*****************************************************************************
 * Copyright (c) 2022 VMware, Inc. All rights reserved.
 * -- VMware Confidential
 *****************************************************************************/

/**
 * @file: nvme_pcie_mgmt.c --
 *
 *   vmkapi_mgmt implementation of native nvme_pcie driver
 */

#include "nvme_pcie_mgmt.h"


#if NVME_PCIE_STORAGE_POLL
static VMK_ReturnStatus
NVMEPCIEKeyPollActGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPollActSet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPollOIOThrGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPollOIOThrSet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPollIntervalGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyPollIntervalSet(vmk_uint64 cookie, void *keyVal);
#endif
#if NVME_PCIE_BLOCKSIZE_AWARE
static VMK_ReturnStatus
NVMEPCIEKeyBlkSizeAwarePollActGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus
NVMEPCIEKeyBlkSizeAwarePollActSet(vmk_uint64 cookie, void *keyVal);
#endif
static vmk_uint32 NVMEPCIEKeyGetHelpPage(vmk_uint8 *buf, vmk_uint32 buf_len);
static VMK_ReturnStatus NVMEPCIEKeyHelpGet(vmk_uint64 cookie, void *keyVal);
static VMK_ReturnStatus NVMEPCIEKeyHelpSet(vmk_uint64 cookie, void *keyVal);


static const NVMEPCIEKVMgmtData nvmePCIEKVMgmtHelp = {
   "help",
   NVMEPCIEKeyHelpGet,
   "Display the help page.",
   NVMEPCIEKeyHelpSet,
   NULL,
};


static NVMEPCIEKVMgmtData nvmePCIEKVMgmtData[] = {
#if NVME_PCIE_STORAGE_POLL
   {
      "pollAct",
      NVMEPCIEKeyPollActGet,
      "Display hybrid poll activation info of the device.",
      NVMEPCIEKeyPollActSet,
      "Set pollAct, non-zero for activation, 0 for deactivation",
   },
   {
      "pollOIOThr",
      NVMEPCIEKeyPollOIOThrGet,
      "Display hybrid poll OIO activation threshold per queue of the device."
      " Valid if poll activated.",
      NVMEPCIEKeyPollOIOThrSet,
      "Set pollOIOThr",
   },
   {
      "pollInterval",
      NVMEPCIEKeyPollIntervalGet,
      "Display hybrid poll interval (us) per queue of the device."
      " Valid if poll activated.",
      NVMEPCIEKeyPollIntervalSet,
      "Set pollInterval",
   },
#endif
#if NVME_PCIE_BLOCKSIZE_AWARE
   {
      "blkSizeAwarePollAct",
      NVMEPCIEKeyBlkSizeAwarePollActGet,
      "Display hybrid poll block size aware activation info of the device."
      " Valid if poll activated.",
      NVMEPCIEKeyBlkSizeAwarePollActSet,
      "Set blkSizeAwarePollAct, non-zero for activation, 0 for deactivation",
   },
#endif
   // Should be always at the end
   nvmePCIEKVMgmtHelp
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
NVMEPCIEKeyGetHelpPage(vmk_uint8 *buf, vmk_uint32 buf_len)
{
   VMK_ReturnStatus status = VMK_OK;
   vmk_ByteCount len = 0;
   vmk_ByteCount out_len = 0;
   vmk_uint32 i = 0;
   vmk_uint32 keyNum = 0;

   status = vmk_StringFormat(buf + len, buf_len - len,
                             &out_len, "\nnvme_pcie help page:\n"
                             "\tKeyName\tOptions\n"
                             "\t-------\t-------\n\n");
   if (status != VMK_OK) {
      goto out_get_help_page;
   }
   len += out_len;

   keyNum = sizeof(nvmePCIEKVMgmtData) / sizeof(NVMEPCIEKVMgmtData);
   for (i = 0; i < keyNum ; i++) {
      status = vmk_StringFormat(buf + len, buf_len - len,
                                &out_len, "\t%s\n",
                                nvmePCIEKVMgmtData[i].keyName);
      if (status != VMK_OK) {
         goto out_get_help_page;
      }
      len += out_len;

      if (nvmePCIEKVMgmtData[i].getDesc != NULL) {
         status = vmk_StringFormat(buf + len, buf_len - len, &out_len,
                                   "\t\t-g : %s\n",
                                   nvmePCIEKVMgmtData[i].getDesc);
         if (status != VMK_OK) {
            goto out_get_help_page;
         }
         len += out_len;
      }

      if (nvmePCIEKVMgmtData[i].setDesc != NULL) {
         status = vmk_StringFormat(buf + len, buf_len - len, &out_len,
                                   "\t\t-s : %s\n",
                                   nvmePCIEKVMgmtData[i].setDesc);
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

   len = NVMEPCIEKeyGetHelpPage(buf, NVMEPCIE_KVMGMT_BUF_SIZE);
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
   for (i = 0; i < keyNum - 1; i++) {
      vmk_NameInitialize(&keyName, nvmePCIEKVMgmtData[i].keyName);
      status = vmk_MgmtAddKey(ctrlr->kvMgmtHandle,
                              VMK_MGMT_KEY_TYPE_LONG,
                              &keyName,
                              nvmePCIEKVMgmtData[i].getFn,
                              nvmePCIEKVMgmtData[i].setFn);
      if (status != VMK_OK) {
         return status;
      }
   }

   vmk_NameInitialize(&keyName, nvmePCIEKVMgmtHelp.keyName);
   status = vmk_MgmtAddKey(ctrlr->kvMgmtHandle,
                           VMK_MGMT_KEY_TYPE_STRING,
                           &keyName,
                           nvmePCIEKVMgmtHelp.getFn,
                           nvmePCIEKVMgmtHelp.setFn);
   if (status != VMK_OK) {
      return status;
   }

   return status;
}
