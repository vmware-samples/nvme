/*********************************************************************************
 * Copyright (c) 2013-2020 VMware, Inc. All rights reserved.
 * ******************************************************************************/

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <vmkapi.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include "nvme_lib.h"


/*****************************************************************************
 * Global Variables
 ****************************************************************************/

struct nvme_adapter_list adapterList;


/*****************************************************************************
 * NVMe Management Ops
 ****************************************************************************/

vmk_MgmtCallbackInfo globalCallbacks[NVME_MGMT_GLOBAL_NUM_CALLBACKS] = {
   {
      .location      = VMK_MGMT_CALLBACK_KERNEL,
      .callback      = NULL, /* serviced by NVMEMgmtListAdapters in kernel */
      .synchronous   = 1,
      .numParms      = 2,
      .parmSizes     = {sizeof(vmk_uint32),
                        sizeof(NvmeAdapterInfo) * NVME_MGMT_MAX_ADAPTERS},
      .parmTypes     = {VMK_MGMT_PARMTYPE_OUT, VMK_MGMT_PARMTYPE_OUT},
      .callbackId    = NVME_MGMT_GLOBAL_CB_LISTADAPTERS,
   },
};

vmk_MgmtCallbackInfo nvmeCallbacks[NVME_MGMT_ADAPTER_NUM_CALLBACKS] = {
   {
      .location      = VMK_MGMT_CALLBACK_KERNEL,
      .callback      = NULL,
      .synchronous   = 0,
      .numParms      = 0,
      .callbackId    = NVME_MGMT_CB_SMART,
   },
   {
      .location      = VMK_MGMT_CALLBACK_KERNEL,
      .callback      = NULL, /* serviced by NVMEKernelCbIoctl in kernel */
      .synchronous   = 1,
      .numParms      = 2,
      .parmSizes     = {sizeof(vmk_uint32), sizeof(NvmeUserIo)},
      .parmTypes     = {VMK_MGMT_PARMTYPE_IN, VMK_MGMT_PARMTYPE_INOUT},
      .callbackId    = NVME_MGMT_CB_IOCTL,
   },
};

vmk_MgmtApiSignature globalSignature = {
   .version         = VMK_REVISION_FROM_NUMBERS(NVME_MGMT_MAJOR,
                                                NVME_MGMT_MINOR,
                                                NVME_MGMT_UPDATE,
                                                NVME_MGMT_PATCH),
   .name.string     = NVME_MGMT_NAME,
   .vendor.string   = NVME_MGMT_VENDOR,
   .numCallbacks    = NVME_MGMT_GLOBAL_NUM_CALLBACKS,
   .callbacks       = globalCallbacks,
};

/**
 * Open a handle to the specified vmhba device
 *
 * @param [in] name name of the vmhba
 *
 * @return pointer to the device handle if successful; NULL specified vmhba
 *         is not a valid NVM Express device.
 */
struct nvme_handle *
Nvme_Open(struct nvme_adapter_list *adapters, const char *name)
{
   struct nvme_handle *handle;
   NvmeAdapterInfo    *adapter;
   vmk_MgmtApiSignature signature;
   int i;
   int rc;

   assert(adapters != NULL);
   assert(name != NULL);

   adapter = NULL;
   for (i = 0; i < adapters->count; i++) {
      if (strcmp(name, adapters->adapters[i].name) == 0) {
         adapter = &adapters->adapters[i];
         break;
      }
   }
   if (adapter == NULL) {
      return NULL;
   }

   handle = (struct nvme_handle *)malloc(sizeof(*handle));
   if (!handle) {
      return NULL;
   }

   strncpy(handle->name, name, sizeof(handle->name));

   signature.version = VMK_REVISION_FROM_NUMBERS(NVME_MGMT_MAJOR, NVME_MGMT_MINOR, NVME_MGMT_UPDATE, NVME_MGMT_PATCH);
   strncpy(signature.name.string, adapter->signature, sizeof(signature.name.string));
   strncpy(signature.vendor.string, NVME_MGMT_VENDOR, sizeof(signature.vendor.string));
   signature.numCallbacks = NVME_MGMT_ADAPTER_NUM_CALLBACKS;
   signature.callbacks = nvmeCallbacks;

   rc = vmk_MgmtUserInit(&signature, 0LL, &handle->handle);
   if (rc) {
      free(handle);
      return NULL;
   }

   return handle;
}


/**
 * Close a handle
 *
 * @param [in] handle pointer to the device handle
 */
void
Nvme_Close(struct nvme_handle *handle)
{
   assert(handle);

   if (!handle || handle->handle == NULL) {
      return;
   }

   vmk_MgmtUserDestroy(handle->handle);
   free(handle);
}


/**
 * Get device management signature
 *
 * @param [in] vmhba name of the vmhba
 * @param [out] signature buffer to hold the signature name
 * @param [in] length signature buffer length
 *
 * @return 0 if successful
 */
int
Nvme_GetAdapterList(struct nvme_adapter_list *list)
{
   vmk_MgmtUserHandle driverHandle;
   int rc;

   assert(list != NULL);

   rc = vmk_MgmtUserInit(&globalSignature, 0LL, &driverHandle);
   if (rc) {
      return rc;
   }

   rc = vmk_MgmtUserCallbackInvoke(driverHandle, 0LL,
      NVME_MGMT_GLOBAL_CB_LISTADAPTERS,
      &list->count, &list->adapters);
   if (rc) {
      vmk_MgmtUserDestroy(driverHandle);
      return rc;
   }

   vmk_MgmtUserDestroy(driverHandle);
   return 0;
}


/**
 * Set driver parameter: nvme_log_level and nvme_dbg.
 *
 * @param [in] log level
 * @param [in] debug level
 *
 * @return 0 if successful
 */
int
Nvme_SetLogLevel(int loglevel, int debuglevel)
{
   /* This is not supported after ESX7.0 */
   return ENOENT;
}


/**
 * Issue Ioctl command to a device
 *
 * @param [in] handle handle to a device
 * @param [in] cmd Ioctl command to be executed.
 * @param [inout] uio pointer to uio data structure
 *
 * @return 0 if successful
 */
int
Nvme_Ioctl(struct nvme_handle *handle, int cmd, NvmeUserIo *uio)
{
   int ioctlCmd;

   assert(handle);
   assert(uio);

   ioctlCmd = cmd;

   return vmk_MgmtUserCallbackInvoke(handle->handle, 0LL,
      NVME_MGMT_CB_IOCTL, &ioctlCmd, uio);
}

/**
 * Issue admin passthru command to a device
 *
 * @param [in] handle handle to a device
 * @param [inout] uio pointer to uio data structure
 *
 * @return 0 if successful
 */
int
Nvme_AdminPassthru(struct nvme_handle *handle, NvmeUserIo *uio)
{
   int rc;

   rc = Nvme_Ioctl(handle, NVME_IOCTL_ADMIN_CMD, uio);

   /**
    * If the command has been successfully submitted to driver, the actual
    * return code for the admin command is returned in uio->status field.
    */
   if (!rc) {
      rc = uio->status;
   }

   return rc;
}

/**
 * Issue error admin passthru command to a device
 *
 * @param [in] handle handle to a device
 * @param [inout] uio pointer to uio data structure
 *
 * @return 0 if successful
 */
int
Nvme_AdminPassthru_error(struct nvme_handle *handle,int cmd, NvmeUserIo *uio)
{
   return Nvme_Ioctl(handle, cmd, uio);
}

/**
 * Issue IDENTIFY admin command to a device
 *
 * @param [in] handle handle to a device
 * @param [in] ns namespace identifier, -1 for the controller
 * @param [out] id pointer to a page buffer to hold identify data
 *
 * @return 0 if successful
 */
int
Nvme_Identify(struct nvme_handle *handle, int cns, int cntId, int nsId, void *id)
{
   int rc = 0;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));

   uio.cmd.identify.cdw0.opc = VMK_NVME_ADMIN_CMD_IDENTIFY;
   uio.cmd.identify.cdw10.cns = cns;
   uio.cmd.identify.cdw10.cntid = cntId;
   uio.cmd.identify.nsid = nsId;

   uio.namespaceID = nsId;
   uio.direction = XFER_FROM_DEV;

   uio.timeoutUs = ADMIN_TIMEOUT;
#define PAGE_SIZE 4096
   uio.length = PAGE_SIZE;
   uio.addr = (vmk_uintptr_t)id;

   rc = Nvme_AdminPassthru(handle, &uio);

   return rc;
}

/**
 * Check if nvme controller supports namespace management and attachment commands
 *
 * @param [in] handle handle to a device
 *
 * @return 0  if not support
 * @return 1  if support
 * @return -1 if fail to check
 */
int
Nvme_NsMgmtAttachSupport(struct nvme_handle *handle)
{
   int rc;
   vmk_NvmeIdentifyController  *id;

   id = malloc(sizeof(*id));
   if (id == NULL) {
      return -1;
   }

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, id);
   if (rc != 0) {
      rc = -1;
      goto out;
   }

   rc = (id->oacs & VMK_NVME_CTLR_IDENT_OACS_NS_MGMT) ? 1 : 0;

out:
   free(id);
   return rc;
}

/**
 * Check if nsId is valid.
 *
 * @param [in] handle handle to a device
 * @param [in] nsId   nsId to be validated
 *
 * @retval return 1 if nsId is valid.
 *         return 0 if nsId is invalid.
 *         return -1 if check failure.
 */
int
Nvme_ValidNsId(struct nvme_handle *handle, int nsId)
{
   int                       rc = 0;
   int                       numNs = 0;
   vmk_NvmeIdentifyController   *idCtrlr;

   idCtrlr = malloc(sizeof(*idCtrlr));
   if (idCtrlr == NULL) {
      return -1;
   }

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, idCtrlr);
   if (rc != 0) {
      rc = -1;
      goto free_id;
   }

   numNs = (int)idCtrlr->nn;

   rc =  (nsId > numNs || nsId > NVME_MAX_NAMESPACE_PER_CONTROLLER || nsId < 1) ? 0 : 1;

free_id:
   free(idCtrlr);
   return rc;
}

/**
 * Check if namespace is created.
 *
 * @retval return 1 if nsId is allocated.
 *         return 0 if nsId is not allocated.
 *         return -1 if check failure.
 * @Note   Assume nsId is valid
 */
int
Nvme_AllocatedNsId(struct nvme_handle *handle, int nsId)
{
   int                       i = 0;
   int                       rc = 0;
   int                       entryNum = 0;
   struct nvme_ns_list       *nsList;

   rc = Nvme_NsMgmtAttachSupport(handle);
   if (rc == -1) {
      goto out;
   }
   if (rc == 0) {
      /**
       * Assume valid namespace is allocated automatically on controller
       * not supporting namespace mgmt and attachment.
       */
      rc = 1;
      goto out;
   }

   nsList = malloc(sizeof(*nsList));
   if (nsList == NULL) {
      rc = -1;
      goto out;
   }

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_NAMESPACE_IDS,
                      0, 0, nsList);
   if (rc != 0) {
      rc = -1;
      goto free_nsList;
   }

   entryNum = sizeof(*nsList)/sizeof(nsList->nsId[0]);
   while (nsId > nsList->nsId[i] && nsList->nsId[i] && i < entryNum) {
      i++;
   }
   if (nsId == nsList->nsId[i]) {
      rc = 1;
   } else {
      rc = 0;
   }

free_nsList:
   free(nsList);
out:
   return rc;
}

/**
 * Check if namespace is attached.
 *
 * @retval return 1 if nsId is attached.
 *         return 0 if nsId is not attached.
 *         return -1 if check failure.
 * @Note   Assume nsId is valid
 */
int
Nvme_AttachedNsId(struct nvme_handle *handle, int nsId)
{
   int                       i = 0;
   int                       rc = 0;
   int                       entryNum = 0;
   struct nvme_ns_list       *nsList;

   rc = Nvme_NsMgmtAttachSupport(handle);
   if (rc == -1) {
      goto out;
   }
   if (rc == 0) {
      /**
       * Assume valid namespace is attached automatically on controller
       * not supporting namespace mgmt and attachment.
       */
      rc = 1;
      goto out;
   }

   nsList = malloc(sizeof(*nsList));
   if (nsList == NULL) {
      rc = -1;
      goto out;
   }

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_NAMESPACE_IDS_ACTIVE,
                      0, 0, nsList);
   if (rc != 0) {
      rc = -1;
      goto free_nsList;
   }

   entryNum = sizeof(*nsList)/sizeof(nsList->nsId[0]);
   while (nsId > nsList->nsId[i] && nsList->nsId[i] && i < entryNum) {
      i++;
   }
   if (nsId == nsList->nsId[i]) {
      rc = 1;
   } else {
      rc = 0;
   }

free_nsList:
   free(nsList);
out:
   return rc;
}

/**
  * Namespace Management Create
  *
  * @param [in] handle handle to a device
  * @param [in] idNs   attributes for the namespace to be created
  *
  * @return nsId if creating namespace successfully, otherwise return -1
  */
int
Nvme_NsMgmtCreate(struct nvme_handle *handle,
                  vmk_NvmeIdentifyNamespace *idNs,
                  int *cmdStatus)
{
   int rc = 0;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));

   uio.cmd.nsMgmt.cdw0.opc = VMK_NVME_ADMIN_CMD_NAMESPACE_MANAGEMENT;
   uio.direction = XFER_TO_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.nsMgmt.cdw10.sel = VMK_NVME_NS_MGMT_CREATE;
   uio.addr = (vmk_uintptr_t)idNs;
   uio.length = sizeof(*idNs);

   rc = Nvme_AdminPassthru(handle, &uio);
   if (cmdStatus) {
      *cmdStatus = (uio.comp.dw3.sct << 8) | uio.comp.dw3.sc;
   }
   if (rc != 0) {
      return -1;
   }

   return (int)uio.comp.dw0;
}

/**
  * Namespace Management Delete
  *
  * @param [in] handle handle to a device
  * @param [in] nsId   namespace id to be deleted
  *
  * @return 0 if successful
  * TODO: Return status code to indicate the failure reason
  */
int
Nvme_NsMgmtDelete(struct nvme_handle *handle, int nsId)
{
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));

   uio.cmd.nsMgmt.cdw0.opc = VMK_NVME_ADMIN_CMD_NAMESPACE_MANAGEMENT;
   uio.cmd.nsMgmt.nsid = nsId;
   uio.direction = XFER_NO_DATA;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.nsMgmt.cdw10.sel = VMK_NVME_NS_MGMT_DELETE;

   return Nvme_AdminPassthru(handle, &uio);
}

/**
  * Namespace Attach
  *
  * @param [in] handle     handle to a device
  * @param [in] sel        operation: attach or detach
  * @param [in] nsId       namespace id to be deleted
  * @param [in] ctrlrList  ctrlr list for this operation
  *
  * @return 0 if successful
  */
int
Nvme_NsAttach(struct nvme_handle *handle, int sel, int nsId,
              struct nvme_ctrlr_list *ctrlrList, int *cmdStatus)
{
   NvmeUserIo uio;
   int rc;

   memset(&uio, 0, sizeof(uio));

   uio.cmd.nsAttach.cdw0.opc = VMK_NVME_ADMIN_CMD_NAMESPACE_ATTACHMENT;
   uio.cmd.nsAttach.nsid = nsId;
   uio.direction = XFER_TO_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.nsAttach.cdw10.sel = sel;
   uio.addr = (vmk_uintptr_t)ctrlrList;
   uio.length = sizeof(*ctrlrList);

   rc = Nvme_AdminPassthru(handle, &uio);
   if (cmdStatus) {
      *cmdStatus = (uio.comp.dw3.sct << 8) | uio.comp.dw3.sc;
   }
   return rc;
}

int
Nvme_NsUpdate(struct nvme_handle *handle, int nsId)
{
   int rc = 0;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));
   uio.namespaceID = nsId;

   rc = Nvme_Ioctl(handle, NVME_IOCTL_UPDATE_NS, &uio);
   if (!rc) {
      rc = uio.status;
   }

   return rc;
}

int
Nvme_NsListUpdate(struct nvme_handle *handle, int sel, int nsId)
{
   int rc = 0;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));
   uio.namespaceID = nsId;
   uio.cmd.nsAttach.cdw10.sel = sel;

   rc = Nvme_Ioctl(handle, NVME_IOCTL_UPDATE_NS_LIST, &uio);
   if (!rc) {
      rc = uio.status;
   }

   return rc;
}

int
Nvme_NsGetStatus(struct nvme_handle *handle, int nsId, int *status)
{
   int rc = 0;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));
   uio.namespaceID = nsId;

   rc = Nvme_Ioctl(handle, NVME_IOCTL_GET_NS_STATUS, &uio);
   if (!rc) {
      *status = uio.status;
   }

   return rc;
}

int
Nvme_NsSetStatus(struct nvme_handle *handle, int nsId, int status)
{
   int rc = 0;
   int cmd;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));
   uio.namespaceID = nsId;

   if (status == NS_ONLINE) {
     cmd = NVME_IOCTL_SET_NS_ONLINE;
   } else {
     cmd = NVME_IOCTL_SET_NS_OFFLINE;
   }

   rc = Nvme_Ioctl(handle, cmd, &uio);
   if (!rc) {
      rc = uio.status;
   }

   return rc;
}


/**
  * Issue IDT specific Create Namespace admin command to a device
  *
  * @param [in] handle handle to a device
  * @param [in] snu starting namespace unit
  * @param [in] nnu number of namespace units
  * @param [in] ns namespace identifier
  *
  * @return 0 if successful
  */
int
Nvme_CreateNamespace_IDT(struct nvme_handle *handle,
                         int ns,
                         vmk_uint32 snu,
                         vmk_uint32 nnu)
{
   int rc = 0;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));

   uio.cmd.vendorSpecificCmd.cdw0.opc = IDT_SYSTEM_CONFIG;
   uio.cmd.vendorSpecificCmd.cdw12 = IDT_CREATE_NAMESPACE;
   uio.cmd.vendorSpecificCmd.cdw13 = snu;
   uio.cmd.vendorSpecificCmd.cdw14 = nnu;
   uio.cmd.vendorSpecificCmd.nsid = ns;
   uio.namespaceID = ns;
   uio.timeoutUs = ADMIN_TIMEOUT;

   rc = Nvme_AdminPassthru(handle, &uio);

   return rc;
}

/**
 * Issue IDT specific Delete Namespace admin command to a device
 *
 * @param [in] handle handle to a device
 * @param [in] ns namespace identifier
 *
 * return 0 if successful
 */
int Nvme_DeleteNamespace_IDT(struct nvme_handle *handle, int ns)
{
   int rc = 0;
   NvmeUserIo uio;
   memset(&uio, 0, sizeof(uio));
   uio.cmd.vendorSpecificCmd.cdw0.opc = IDT_SYSTEM_CONFIG;
   uio.cmd.vendorSpecificCmd.cdw12 = IDT_DELETE_NAMESPACE;
   uio.namespaceID = ns;
   uio.cmd.vendorSpecificCmd.nsid = ns;
   uio.timeoutUs = ADMIN_TIMEOUT;
   rc = Nvme_AdminPassthru(handle, &uio);
   return rc;
}


int Nvme_FWLoadImage(char *fw_path, void **fw_buf, int *fw_size)
{
   int fd;
   struct stat	sb;
   int fw_file_size;

   assert (fw_path && fw_buf && fw_size);

   /* get fw binary */
   fd = open (fw_path, O_RDONLY);
   if (fd == -1) {
      fprintf (stderr, "ERROR: Failed to open firmware image.\n");
      return -ENOENT;
   }

   if (fstat (fd, &sb) == -1) {
      fprintf (stderr, "ERROR: Failed to call fstat.\n");
      if (close (fd) == -1) {
         fprintf (stderr, "ERROR: Failed to close fd: %d.\n", fd);
         return -EBADF;
      }
      return -EPERM;
   }

   if (!S_ISREG (sb.st_mode)) {
      fprintf (stderr, "ERROR: %s is not a file.\n", fw_path);
      if (close (fd) == -1) {
         fprintf (stderr, "ERROR: Failed to close fd: %d.\n", fd);
         return -EBADF;
      }
      return -EPERM;
   }

   fw_file_size = (int)sb.st_size;
   if ((*fw_buf = malloc(fw_file_size)) == NULL) {//need to free!!!!
      fprintf (stderr, "ERROR: Failed to malloc %d bytes.\n", fw_file_size);
      if (close (fd) == -1) {
         fprintf (stderr, "ERROR: Failed to close fd: %d.\n", fd);
         return -EBADF;
      }
      return -ENOMEM;
   }

   //read fw image from file
   if ((*fw_size = read(fd, *fw_buf, fw_file_size)) < 0) {
      fprintf(stderr, "ERROR: Failed to read firmware image: %s.\n", fw_path);
      if (close (fd) == -1) {
         fprintf (stderr, "ERROR: Failed to close fd: %d.\n", fd);
         return -EBADF;
      }
      return -EIO;
   }

#ifdef FIRMWARE_DUMP
   printf ("Dump whole fw image: \n");
   int i, j;
   for(i=0; i<(*fw_size); i+=16) {
      for(j=0; j<16 && (i+j<(*fw_size)); j++)
         printf ("%4x  ", *(unsigned char *)(*fw_buf+i+j));
      printf ("\n");
   }
   printf ("\n");
#endif

   if (close (fd) == -1) {
      fprintf (stderr, "ERROR: Failed to close fd: %d.\n", fd);
      return -EBADF;
   }

   return 0;
}

int
Nvme_FWDownload(struct nvme_handle *handle,
                unsigned char *rom_buf,
                int rom_size)
{
   NvmeUserIo uio;
   int offset, size;
   int rc;
   void *chunk;

   if ((chunk = malloc(NVME_MAX_XFER_SIZE)) == NULL) {
      fprintf(stderr, "ERROR: Failed to malloc %d bytes.\n",
              NVME_MAX_XFER_SIZE);
      return -ENOMEM;
   }

   for (offset = 0; offset < rom_size; offset += NVME_MAX_XFER_SIZE) {
      size = ((rom_size-offset) >= NVME_MAX_XFER_SIZE) ?
         NVME_MAX_XFER_SIZE : (rom_size-offset);
      memcpy(chunk, rom_buf+offset, size);

      memset(&uio, 0, sizeof(uio));
      uio.cmd.firmwareDownload.cdw0.opc =
         VMK_NVME_ADMIN_CMD_FIRMWARE_DOWNLOAD;
      uio.cmd.firmwareDownload.nsid = 0;
      uio.direction = XFER_TO_DEV;
      uio.timeoutUs = FIRMWARE_DOWNLOAD_TIMEOUT;
      uio.cmd.firmwareDownload.cdw10.numd =
         (size / sizeof(vmk_uint32)) - 1;
      uio.cmd.firmwareDownload.cdw11.ofst = offset / sizeof(vmk_uint32);
      uio.addr = (vmk_uintptr_t)chunk;
      uio.length = size;

      rc = Nvme_AdminPassthru(handle, &uio);
      if (rc) {
         /* Failed to execute download firmware command. */
         free(chunk);
         return rc;
      }
   }

   free(chunk);
   return 0;
}

int Nvme_FWFindSlot(struct nvme_handle *handle, int *slot)
{
   NvmeUserIo uio;
   union {
      vmk_NvmeErrorInfoLogEntry errLog;
      vmk_NvmeSmartInfoEntry smartLog;
      vmk_NvmeFirmwareSlotInfo fwSlotLog;
   } log;
   unsigned char fw_rev_slot[MAX_FW_SLOT][FW_REV_LEN];
   int rc = -1;
   int i;

   assert(handle && slot);
   assert(*slot > 0 && *slot < 8);

   memset(&uio, 0, sizeof(uio));

   uio.cmd.getLogPage.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_LOG_PAGE;
   uio.cmd.getLogPage.nsid = VMK_NVME_DEFAULT_NSID;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getLogPage.cdw10.lid = VMK_NVME_LID_FW_SLOT;
   uio.cmd.getLogPage.cdw10.numdl = sizeof(vmk_NvmeFirmwareSlotInfo) / 4 - 1;
   uio.length = sizeof(vmk_NvmeFirmwareSlotInfo);
   uio.addr = (vmk_uintptr_t)&log.fwSlotLog;
   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      return -EIO;
   }

   /* copy firmware revision info from log.fwSlotLog.frs. */
   memcpy(fw_rev_slot, log.fwSlotLog.frs, sizeof(fw_rev_slot));

   /* search for first available slot */
   for(i=0; i<MAX_FW_SLOT; i++) {
      if( !fw_rev_slot[i][0] ) {
         *slot = i+1;
         return 0;
      }
   }

   if(i==MAX_FW_SLOT)
      return -EINVAL;

   return 0;
}

/**
 * Issue firmware activate command and get the command status.
 *
 * @param [in]  handle    Handle to a device
 * @param [in]  slot      Firmware slot
 * @param [in]  action    Activate action code
 * @param [out] cmdStatus Command execution status
 *
 * @return  0      if successful
 *          others Command failed to be submitted or
 *                 executed with non-zero status
 */
int
Nvme_FWActivate(struct nvme_handle *handle,
                int slot,
                int action,
                int *cmdStatus)
{
   NvmeUserIo uio;
   int rc = -1;

   assert(handle);
   assert(slot >= 0 && slot < 8);
   assert(action >= 0 && action < 4);

   memset(&uio, 0, sizeof(uio));

   uio.cmd.firmwareActivate.cdw0.opc = VMK_NVME_ADMIN_CMD_FIRMWARE_COMMIT;
   uio.cmd.firmwareActivate.nsid = 0;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = FIRMWARE_ACTIVATE_TIMEOUT;
   uio.cmd.firmwareActivate.cdw10.fs = slot;
   uio.cmd.firmwareActivate.cdw10.ca = action;
   rc = Nvme_AdminPassthru(handle, &uio);

   if (cmdStatus) {
      *cmdStatus = uio.comp.dw3.sct << 8 | uio.comp.dw3.sc;
   }

   if (uio.comp.dw3.sct << 8 | uio.comp.dw3.sc) {
      rc = 0xbad0001;
   }

   return rc;
}

/**
 * Issue Format NVM
 *
 * @param [in]  handle handle to a device
 * @param [in]  ses    Secure Erase Settings
 * @param [in]  pil    Protection Information Locatoin
 * @param [in]  pi     Protection Information
 * @param [in]  ms     Metadata Settings
 * @param [in]  lbaf   LBA Format
 * @param [in]  ns     Namespace ID
 *
 * @return                                     0 if successful
 *         (SCT << 8 | SC) or negative UNIX code if failed.
 */
int
Nvme_FormatNvm(struct nvme_handle *handle,
               int ses,
               int pil,
               int pi,
               int ms,
               int lbaf,
               int ns)
{
   int rc = 0;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));

   uio.cmd.format.cdw0.opc = VMK_NVME_ADMIN_CMD_FORMAT_NVM;
   uio.cmd.format.nsid = ns;
   uio.cmd.format.cdw10.ses = ses;
   uio.cmd.format.cdw10.pil = pil;
   uio.cmd.format.cdw10.pi = pi;
   uio.cmd.format.cdw10.mset = ms;
   uio.cmd.format.cdw10.lbaf = lbaf;

   uio.namespaceID = ns;
   /* Set timeout as 30mins, we do see some devices need
    * ~20 minutes to format.
    */
   uio.timeoutUs = FORMAT_TIMEOUT;
   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      return rc;
   } else {
      return (uio.comp.dw3.sct << 8 | uio.comp.dw3.sc);
   }
}

/**
 * Set IO timeout
 *
 * @param [in] handle handle to a device
 * @param [in] timeout
 *
 * @return 0 if successful
 */
int
Nvme_SetTimeout(struct nvme_handle *handle, int timeout)
{
   int rc = 0;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));

   uio.length = timeout;
   rc = Nvme_Ioctl(handle, NVME_IOCTL_SET_TIMEOUT, &uio);

   if (!rc) {
      rc = uio.status;
   }

   return rc;
}

/**
 * Get IO timeout
 *
 * @param [in] handle handle to a device
 * @param [out] timeout
 *
 * @return 0 if successful
 */
int
Nvme_GetTimeout(struct nvme_handle *handle, int *timeout)
{
   int rc = 0;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));

   rc = Nvme_Ioctl(handle, NVME_IOCTL_GET_TIMEOUT, &uio);

   if (!rc) {
      rc = uio.status;
   }

   if (!rc) {
      *timeout = uio.length;
   }

   return rc;
}
