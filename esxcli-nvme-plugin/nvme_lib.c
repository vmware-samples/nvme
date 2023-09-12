/*********************************************************************************
 * Copyright (c) 2013-2023 VMware, Inc. All rights reserved.
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
vmk_uint32 logLevel = NVME_LOG_ERR;
vmk_uint64 adminTimeout = ADMIN_TIMEOUT;

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
      LogError("Failed to allocate nvme device handle.");
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
      LogError("Failed to init mgmt, 0x%x.", rc);
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

int
Nvme_WriteRawDataToFile(void *data, int len, char* path)
{
   int fd;
   if (data == NULL || path == NULL) {
      return -EINVAL;
   }
   fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0666);
   if (fd == -1) {
      LogError("Failed to open file %s.", path);
      return -ENOENT;
   }
   if (write(fd, data, len) != len) {
      LogError("Failed to write data to file.");
      close(fd);
      return -EIO;
   }
   close(fd);
   return 0;
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
      LogError("Failed to init mgmt, 0x%x.", rc);
      return rc;
   }

   rc = vmk_MgmtUserCallbackInvoke(driverHandle, 0LL,
      NVME_MGMT_GLOBAL_CB_LISTADAPTERS,
      &list->count, &list->adapters);
   if (rc) {
      LogError("Failed to invoke mgmt callback, 0x%x.", rc);
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
   int rc;

   assert(handle);
   assert(uio);

   ioctlCmd = cmd;

   LogDebug("%s, ioctl cmd %d, uio %p, ns %d",
            handle->name, cmd, uio, uio->namespaceID);
   rc = vmk_MgmtUserCallbackInvoke(handle->handle, 0LL,
      NVME_MGMT_CB_IOCTL, &ioctlCmd, uio);
   if (rc) {
      LogError("Failed to invoke mgmt callback, 0x%x.", rc);
   } else {
      LogDebug("uio status 0x%x", uio->status);
   }
   return rc;
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
   int rc, i;
   vmk_uint32 *ptr;

   LogDebug("%s, uio %p, opc 0x%x, timeout %lu, addr 0x%lx, length %d",
            handle->name, uio, uio->cmd.cmd.cdw0.opc, uio->timeoutUs,
            uio->addr, uio->length);
   ptr = (vmk_uint32 *)&uio->cmd.cmd;
   for (i = 0; i < sizeof(uio->cmd)/sizeof(vmk_uint32); i += 4) {
       LogDebug("%02x: %08x %08x %08x %08x", i,
                ptr[i], ptr[i+1], ptr[i+2], ptr[i+3]);
   }
   rc = Nvme_Ioctl(handle, NVME_IOCTL_ADMIN_CMD, uio);

   /**
    * If the command has been successfully submitted to driver, the actual
    * return code for the admin command is returned in uio->status field.
    */
   if (rc || uio->status) {
      LogError("Admin passthru failed on %s, opc 0x%x, rc 0x%x, status 0x%x, comp 0x%x.",
               handle->name, uio->cmd.cmd.cdw0.opc, rc, uio->status,
               ((uio->comp.dw3.sct << 8) | uio->comp.dw3.sc));
   }

   if (!rc) {
      rc = uio->status;
   }

   ptr = (vmk_uint32 *)&uio->comp;
   for (i = 0; i < sizeof(uio->comp)/sizeof(vmk_uint32); i += 4) {
       LogDebug("%02x: %08x %08x %08x %08x", i,
                ptr[i], ptr[i+1], ptr[i+2], ptr[i+3]);
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
Nvme_Identify(struct nvme_handle *handle, int cns, int cntId, vmk_uint32 nsId, void *id)
{
   int rc = 0;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));

   uio.cmd.identify.cdw0.opc = VMK_NVME_ADMIN_CMD_IDENTIFY;
   uio.cmd.identify.cdw10.cns = cns;
   uio.cmd.identify.cdw10.cntid = cntId;
   uio.cmd.identify.nsid = nsId;
   uio.direction = XFER_FROM_DEV;

   uio.timeoutUs = adminTimeout;
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
      LogError("Failed to allocate identify data.");
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
Nvme_ValidNsId(struct nvme_handle *handle, vmk_uint32 nsId)
{
   int                       rc = 0;
   vmk_uint32                numNs = 0;
   vmk_NvmeIdentifyController   *idCtrlr;

   idCtrlr = malloc(sizeof(*idCtrlr));
   if (idCtrlr == NULL) {
      LogError("Failed to allocate identify data.");
      return -1;
   }

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, idCtrlr);
   if (rc != 0) {
      rc = -1;
      goto free_id;
   }

   numNs = idCtrlr->nn;

   rc =  (nsId > numNs || nsId < 1) ? 0 : 1;

free_id:
   free(idCtrlr);
   return rc;
}

/**
 * Check if namespace is allocated.
 *
 * @retval return 1 if nsId is allocated.
 *         return 0 if nsId is not allocated.
 *         return -1 if check failure.
 * @Note   Assume nsId is valid
 */
int
Nvme_AllocatedNsId(struct nvme_handle *handle, vmk_uint32 nsId)
{
   int                        i = 0;
   int                        rc = 0;
   int                        entryNum = 0;
   struct nvme_ns_list        *nsList;

   if (nsId == 0) {
      return 0;
   }

   nsList = malloc(sizeof(*nsList));
   if (nsList == NULL) {
      LogError("Failed to allocate ns list.");
      rc = -1;
      goto out;
   }

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_NAMESPACE_IDS,
                      0, nsId - 1, nsList);
   if (rc != 0) {
      /**
       * For NVMe controllers which don't support allocated namespace list,
       * there is no way to check whether the namespace is allocated.
       */
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
Nvme_AttachedNsId(struct nvme_handle *handle, vmk_uint32 nsId)
{
   int                       i = 0;
   int                       rc = 0;
   int                       entryNum = 0;
   struct nvme_ns_list       *nsList;
   vmk_NvmeIdentifyController *idCtrlr;

   if (nsId == 0) {
      return 0;
   }

   idCtrlr = malloc(sizeof(*idCtrlr));
   if (idCtrlr == NULL) {
      LogError("Failed to allocate identify data.");
      return -1;
   }

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, idCtrlr);
   if (rc) {
      rc = -1;
      goto free_id;
   }

   if ((idCtrlr->ver.mjr == 1 && idCtrlr->ver.mnr < 1) ||
       idCtrlr->ver.mjr < 1) {
      /**
       * 1.0 NVMe controllers don't support active namespace list,
       * and there is no definition of active and inactive NSID in
       * spec 1.0, thus assume a valid NSID is an active NSID.
       */
      if (nsId <= idCtrlr->nn) {
         rc = 1;
      } else {
         rc = 0;
      }
      goto free_id;
   }

   nsList = malloc(sizeof(*nsList));
   if (nsList == NULL) {
      LogError("Failed to allocate ns list.");
      rc = -1;
      goto free_id;
   }

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_NAMESPACE_IDS_ACTIVE,
                      0, nsId - 1, nsList);
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
free_id:
   free(idCtrlr);
   return rc;
}

/**
  * Namespace Management Create
  *
  * @param [in] handle handle to a device
  * @param [in] idNs   attributes for the namespace to be created
  *
  * @return nsId if creating namespace successfully, otherwise return 0
  */
vmk_uint32
Nvme_NsMgmtCreate(struct nvme_handle *handle,
                  vmk_NvmeIdentifyNamespace *idNs,
                  int *cmdStatus)
{
   int rc = 0;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));

   uio.cmd.nsMgmt.cdw0.opc = VMK_NVME_ADMIN_CMD_NAMESPACE_MANAGEMENT;
   uio.direction = XFER_TO_DEV;
   uio.timeoutUs = adminTimeout;
   uio.cmd.nsMgmt.cdw10.sel = VMK_NVME_NS_MGMT_CREATE;
   uio.addr = (vmk_uintptr_t)idNs;
   uio.length = sizeof(*idNs);

   rc = Nvme_AdminPassthru(handle, &uio);
   if (cmdStatus) {
      *cmdStatus = (uio.comp.dw3.sct << 8) | uio.comp.dw3.sc;
   }
   if (rc != 0) {
      return 0;
   }

   return uio.comp.dw0;
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
Nvme_NsMgmtDelete(struct nvme_handle *handle, vmk_uint32 nsId)
{
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));

   uio.cmd.nsMgmt.cdw0.opc = VMK_NVME_ADMIN_CMD_NAMESPACE_MANAGEMENT;
   uio.cmd.nsMgmt.nsid = nsId;
   uio.direction = XFER_NO_DATA;
   uio.timeoutUs = adminTimeout;
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
Nvme_NsAttach(struct nvme_handle *handle, int sel, vmk_uint32 nsId,
              struct nvme_ctrlr_list *ctrlrList, int *cmdStatus)
{
   NvmeUserIo uio;
   int rc;

   memset(&uio, 0, sizeof(uio));

   uio.cmd.nsAttach.cdw0.opc = VMK_NVME_ADMIN_CMD_NAMESPACE_ATTACHMENT;
   uio.cmd.nsAttach.nsid = nsId;
   uio.direction = XFER_TO_DEV;
   uio.timeoutUs = adminTimeout;
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
Nvme_NsUpdate(struct nvme_handle *handle, vmk_uint32 nsId)
{
   int rc = 0;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));
   /* Before ESXi 8.0U3, driver uses uio.namespaceID. To make this tool work on older
    * ESXi releases, also assign nsID to uio.namespaceID.
    */
   if (nsId <= VMK_UINT8_MAX) {
      uio.namespaceID = nsId;
   }
   uio.cmd.cmd.nsid = nsId;

   rc = Nvme_Ioctl(handle, NVME_IOCTL_UPDATE_NS, &uio);
   if (!rc) {
      rc = uio.status;
   }

   return rc;
}

int
Nvme_NsListUpdate(struct nvme_handle *handle, int sel, vmk_uint32 nsId)
{
   int rc = 0;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));
   /* Before ESXi 8.0U3, driver uses uio.namespaceID. To make this tool work on older
    * ESXi releases, also assign nsID to uio.namespaceID.
    */
   if (nsId <= VMK_UINT8_MAX) {
      uio.namespaceID = nsId;
   }
   uio.cmd.nsAttach.nsid = nsId;
   uio.cmd.nsAttach.cdw10.sel = sel;

   rc = Nvme_Ioctl(handle, NVME_IOCTL_UPDATE_NS_LIST, &uio);
   if (!rc) {
      rc = uio.status;
   }

   return rc;
}

int
Nvme_NsGetStatus(struct nvme_handle *handle, vmk_uint32 nsId, int *status)
{
   int rc = 0;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));
   /* Before ESXi 8.0U3, driver uses uio.namespaceID. To make this tool work on older
    * ESXi releases, also assign nsID to uio.namespaceID.
    */
   if (nsId <= VMK_UINT8_MAX) {
      uio.namespaceID = nsId;
   }
   uio.cmd.cmd.nsid = nsId;

   rc = Nvme_Ioctl(handle, NVME_IOCTL_GET_NS_STATUS, &uio);
   if (!rc) {
      *status = uio.status;
   }

   return rc;
}

int
Nvme_NsSetStatus(struct nvme_handle *handle, vmk_uint32 nsId, int status)
{
   int rc = 0;
   int cmd;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));
   /* Before ESXi 8.0U3, driver uses uio.namespaceID. To make this tool work on older
    * ESXi releases, also assign nsID to uio.namespaceID.
    */
   if (nsId <= VMK_UINT8_MAX) {
      uio.namespaceID = nsId;
   }
   uio.cmd.cmd.nsid = nsId;

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
                         vmk_uint32 ns,
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
   uio.timeoutUs = adminTimeout;

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
int Nvme_DeleteNamespace_IDT(struct nvme_handle *handle, vmk_uint32 ns)
{
   int rc = 0;
   NvmeUserIo uio;
   memset(&uio, 0, sizeof(uio));
   uio.cmd.vendorSpecificCmd.cdw0.opc = IDT_SYSTEM_CONFIG;
   uio.cmd.vendorSpecificCmd.cdw12 = IDT_DELETE_NAMESPACE;
   uio.cmd.vendorSpecificCmd.nsid = ns;
   uio.timeoutUs = adminTimeout;
   rc = Nvme_AdminPassthru(handle, &uio);
   return rc;
}


/**
 * Load the firmware image from the specified path and download it to a device.
 *
 * @param [in] handle handle to a device
 * @param [in] fwPath Path of the firmware image
 * @param [in] fwOffset Offset of the firmware image
 * @param [in] xferSize Transfer size of each firmware dowonload command
 *
 * return 0 if successful
 */
int Nvme_FWLoadAndDownload(struct nvme_handle *handle,
                           char *fwPath,
                           int fwOffset,
                           int xferSize)
{
   int fd;
   struct stat	sb;
   vmk_uint32 fwSize, size, fwBufSize;
   int offset;
   void* fwBuf = NULL;
   int rc = 0;

   assert (fwPath);

   if (fwOffset % 0x3) {
      LogError("Invalid offset 0x%x.", fwOffset);
      return -EINVAL;
   }

   /* get fw binary */
   fd = open (fwPath, O_RDONLY);
   if (fd == -1) {
      LogError("Failed to open firmware image %s.", fwPath);
      return -ENOENT;
   }

   if (fstat(fd, &sb) == -1) {
      LogError("Failed to call fstat.");
      if (close (fd) == -1) {
         LogError("Failed to close fd: %d.", fd);
         return -EBADF;
      }
      return -EPERM;
   }

   if (!S_ISREG (sb.st_mode)) {
      LogError("%s is not a file.", fwPath);
      if (close (fd) == -1) {
         LogError("Failed to close fd: %d.", fd);
         return -EBADF;
      }
      return -EPERM;
   }

   fwSize = (vmk_uint32)sb.st_size;
   if ((fwSize == 0) || (fwSize & 0x3)) {
      LogError("Invalid firmware image size 0x%x.", fwSize);
      if (close (fd) == -1) {
         LogError("Failed to close fd: %d.", fd);
         return -EBADF;
      }
      return -EINVAL;
   }

   if (fwSize < xferSize) {
      LogDebug("Adjust xferSize %u to %u", xferSize, fwSize);
      xferSize = fwSize;
   }
   fwBufSize = fwSize;
   /* If allocation fails, try smaller buffer, but guarantee xfersize align.*/
   while (fwBufSize >= xferSize) {
      fwBuf = malloc(fwBufSize);
      if (fwBuf != NULL) {
         break;
      }
      if (fwBufSize > xferSize && fwBufSize / 2 < xferSize) {
         fwBufSize = xferSize;
      } else {
         fwBufSize = fwBufSize / 2;
         fwBufSize = fwBufSize / xferSize * xferSize;
      }
   }
   if (fwBuf == NULL) {
      LogError("Failed to malloc %d bytes.", fwBufSize);
      if (close (fd) == -1) {
         LogError("Failed to close fd: %d.", fd);
         return -EBADF;
      }
      return -ENOMEM;
   }

   LogDebug("firmware size %u, buf size %u, xferSize %u",
            fwSize, fwBufSize, xferSize);
   //read fw image from file
   for (offset = 0; offset < fwSize; offset += fwBufSize) {
      size = fwSize - offset;
      if (size > fwBufSize) {
         size = fwBufSize;
      }
      if (read(fd, fwBuf, size) < 0) {
         LogError("Failed to read firmware data at offset 0x%x, size 0x%x.",
                  offset, size);
         rc = -EIO;
         break;
      }
      LogDebug("Dump fw image:");
      int i, j;
      for(i = 0; i < size; i += 16) {
         for(j = 0; j < 16 && (i+j<size); j++)
            LogDebug("%04x  ", *(unsigned char *)(fwBuf+i+j));
      }
      rc = Nvme_FWDownload(handle, fwBuf, size, offset + fwOffset, xferSize);
      if (rc) {
         LogError("Failed to download firmware data at offset 0x%x, size 0x%x.",
                  offset, size);
         break;
      }
   }

   free(fwBuf);
   if (close (fd) == -1) {
      LogError("Failed to close fd: %d.", fd);
      return -EBADF;
   }

   return rc;
}


/**
 * Download all or  portion of an firmware image to a device.
 *
 * @param [in] handle handle to a device
 * @param [in] fwBuf Data of the firmware image
 * @param [in] fwSize Size of the firmware image
 * @param [in] fwOffset Offset of the firmware image
 * @param [in] xferSize Transfer size of each firmware dowonload command
 *
 * return 0 if successful
 */
int
Nvme_FWDownload(struct nvme_handle *handle,
                unsigned char *fwBuf,
                int fwSize,
                int fwOffset,
                int xferSize)
{
   NvmeUserIo uio;
   vmk_uint32 offset, size;
   int rc;
   void *chunk;
   vmk_uint64 timeout = FIRMWARE_DOWNLOAD_TIMEOUT;

   if (adminTimeout > timeout) {
      timeout = adminTimeout;
   }

   if ((chunk = malloc(xferSize)) == NULL) {
      LogError("Failed to malloc %d bytes.", xferSize);
      return -ENOMEM;
   }

   for (offset = 0; offset < fwSize; offset += xferSize) {
      size = ((fwSize-offset) >= xferSize) ? xferSize : (fwSize-offset);
      memcpy(chunk, fwBuf+offset, size);
      memset(&uio, 0, sizeof(uio));
      uio.cmd.firmwareDownload.cdw0.opc =
         VMK_NVME_ADMIN_CMD_FIRMWARE_DOWNLOAD;
      uio.cmd.firmwareDownload.nsid = 0;
      uio.direction = XFER_TO_DEV;
      uio.timeoutUs = timeout;
      uio.cmd.firmwareDownload.cdw10.numd =
         (size / sizeof(vmk_uint32)) - 1;
      uio.cmd.firmwareDownload.cdw11.ofst = (fwOffset + offset) / sizeof(vmk_uint32);
      uio.addr = (vmk_uintptr_t)chunk;
      uio.length = size;

      rc = Nvme_AdminPassthru(handle, &uio);
      if (rc) {
         /* Failed to execute download firmware command. */
         LogError("Failed to download firmware data at offset 0x%x, size 0x%x.",
                  offset, size);
         free(chunk);
         return rc;
      }
   }

   free(chunk);
   return 0;
}

int Nvme_FWFindSlot(struct nvme_handle *handle, int *slot)
{
   vmk_NvmeFirmwareSlotInfo fwSlotLog;
   unsigned char fw_rev_slot[MAX_FW_SLOT][FW_REV_LEN];
   int rc = -1;
   int i;

   assert(handle && slot);
   assert(*slot > 0 && *slot < 8);

   rc = Nvme_GetLogPage(handle, VMK_NVME_LID_FW_SLOT, VMK_NVME_DEFAULT_NSID,
                        &fwSlotLog, sizeof(vmk_NvmeFirmwareSlotInfo),
                        0, 0, 0, 0, 0);
   if (rc) {
      return -EIO;
   }

   /* copy firmware revision info from log.fwSlotLog.frs. */
   memcpy(fw_rev_slot, fwSlotLog.frs, sizeof(fw_rev_slot));

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
   if (FIRMWARE_ACTIVATE_TIMEOUT < adminTimeout) {
      uio.timeoutUs = adminTimeout;
   } else {
      uio.timeoutUs = FIRMWARE_ACTIVATE_TIMEOUT;
   }
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
               vmk_uint32 ns)
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
   /* Set timeout as 30mins, we do see some devices need
    * ~20 minutes to format.
    */
   if (FORMAT_TIMEOUT >= adminTimeout) {
      uio.timeoutUs = FORMAT_TIMEOUT;
   } else {
      uio.timeoutUs = adminTimeout;
   }
   /* NVMe driver will check the namespace status before executing format command.
    * Before ESXi 8.0U3, driver uses uio.namespaceID. To make this tool work on older
    * ESXi releases, also assign nsID to uio.namespaceID.
    */
   if (ns <= VMK_UINT8_MAX) {
      uio.namespaceID = ns;
   }
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

/**
 * Download Telemetry Data to specified path
 *
 * @param [in] handle handle to a device
 * @param [in] telemetryPath path to download the telemetry data
 * @param [in] lid log page identifier
 * @param [in] dataArea data area to get telemetry data
 *
 * @return 0 if successful
 */
int
Nvme_GetTelemetryData(struct nvme_handle *handle,
                      char *telemetryPath,
                      int lid,
                      int dataArea)
{

   int fd;
   int rc = -1;
   vmk_uint16 data1LB, data2LB, data3LB, dataLB;
   vmk_uint32 size;
   int ctrlRetry = 0;
   int genNumStart = -1;
   int genNumEnd = -1;
   int lsp = 0;
   vmk_NvmeTelemetryEntry telemetryEntry;
   vmk_NvmeTelemetryEntry *telemetryLog;

   assert(telemetryPath);
   assert(lid == VMK_NVME_LID_TELEMETRY_HOST_INITIATED ||
          lid == VMK_NVME_LID_TELEMETRY_CONTROLLER_INITIATED);

retry:
   if (lid == VMK_NVME_LID_TELEMETRY_CONTROLLER_INITIATED &&
       ctrlRetry > 3) {
      LogError("Telemetry Controller-Initiated data is not"
               " stable, please try later.");
      return -1;
   }

   /**
    * Create Telemetry data for Host-Initiated request
    * Get Generation Number for Controller-Initiated request
    */
   if (lid == VMK_NVME_LID_TELEMETRY_HOST_INITIATED) {
      lsp = 1;
   }
   rc = Nvme_GetLogPage(handle, lid, VMK_NVME_DEFAULT_NSID, &telemetryEntry,
                        sizeof(vmk_NvmeTelemetryEntry), 0, 1, lsp, 0, 0);
   if (rc) {
      if (lid == VMK_NVME_LID_TELEMETRY_HOST_INITIATED) {
         LogError("Failed to create Telemetry Host-Initiated data, 0x%x.", rc);
      } else {
         LogError("Failed to get Telemetry Controller-Initiated log header, 0x%x.", rc);
      }
      return rc;
   }

   genNumStart = telemetryEntry.dataGenNum;

   data1LB = telemetryEntry.dataArea1LB;
   data2LB = telemetryEntry.dataArea2LB;
   data3LB = telemetryEntry.dataArea3LB;
   assert(data3LB >= data2LB && data2LB >= data1LB);
   switch (dataArea) {
      case 1:
         dataLB = data1LB;
         break;
      case 2:
         dataLB = data2LB;
         break;
      case 3:
         dataLB = data3LB;
         break;
      default:
         return -EINVAL;
   }

   fd = open(telemetryPath, O_WRONLY|O_CREAT|O_TRUNC, 0666);
   if (fd == -1) {
      LogError("Failed to open telemetry path %s.", telemetryPath);
      return -ENOENT;
   }
   /* Write telemetry log header */
   if (write(fd, (void *)&telemetryEntry, sizeof(vmk_NvmeTelemetryEntry)) !=
       sizeof(vmk_NvmeTelemetryEntry)) {
      LogError("Failed to write telemetry log header.");
      close(fd);
      return -EIO;
   }

   size = dataLB * NVME_TELEMETRY_DATA_BLK_SIZE;
   if (size == 0) {
      close(fd);
      return 0;
   }

   if ((telemetryLog = (vmk_NvmeTelemetryEntry *)malloc(
                        sizeof(vmk_NvmeTelemetryEntry) + size)) == NULL) {
      LogError("Failed to malloc %u bytes.", size);
      if (close (fd) == -1) {
         LogError("Failed to close fd: %d.", fd);
         return -EBADF;
      }
      return -ENOMEM;
   }

   rc = Nvme_GetLogPage(handle, lid, VMK_NVME_DEFAULT_NSID,
                        telemetryLog->dataBlocks, size,
                        sizeof(vmk_NvmeTelemetryEntry), 1, 0, 0, 0);
   if (rc) {
      /* Failed to execute get telemetry log command. */
      LogError("Failed to get telemetry log.");
      free(telemetryLog);
      close(fd);
      return rc;
   }

   if (write(fd, (void *)telemetryLog->dataBlocks, size) != size) {
      LogError("Failed to write telemetry log to file.");
      free(telemetryLog);
      close(fd);
      return -EIO;
   }

   free(telemetryLog);
   if (close (fd) == -1) {
      LogError("Failed to close fd: %d.", fd);
      return -EBADF;
   }

   /**
    * For Telemetry Controller-Initiated, ensure Data Generation Number
    * matches the original value read.
    */
   if (lid == VMK_NVME_LID_TELEMETRY_CONTROLLER_INITIATED) {
      rc = Nvme_GetLogPage(handle, lid, VMK_NVME_DEFAULT_NSID, &telemetryEntry,
                           sizeof(vmk_NvmeTelemetryEntry), 0, 1, 0, 0, 0);
      if (rc) {
         LogError("Failed to get"
                  " Data Generation Number after data collection is done.");
         return rc;
      }
      genNumEnd = telemetryEntry.dataGenNum;
      if (genNumEnd != genNumStart) {
         LogError("Telemetry Controller-Initiated is not stable.");
         ctrlRetry ++;
         goto retry;
      }
   }

   return 0;
}

/**
 * Download Persistent Event Log to specified path
 *
 * @param [in] handle handle to a device
 * @param [in] logPath path to download the persistent event log
 * @param [in] action action the controller shall take
 *
 * @return 0 if successful
 */
int
Nvme_GetPersistentEventLog(struct nvme_handle *handle,
                           char *logPath,
                           int action)
{

   int fd;
   int rc = -1;
   vmk_uint32 offset, size;
   nvme_persistent_event_log_header logHeader;
   vmk_uint8 *logData = NULL;

   if (action > NVME_PEL_ACTION_RELEASE) {
      return -EINVAL;
   }
   if (action != NVME_PEL_ACTION_RELEASE && logPath == NULL) {
      return -EINVAL;
   }

   rc = Nvme_GetLogPage(handle, NVME_LID_PERSISTENT_EVENT,
                        VMK_NVME_DEFAULT_NSID, &logHeader,
                        sizeof(logHeader), 0, 0, action, 0, 0);
   if (rc) {
      LogError("Failed to fetch persistent event log header, 0x%x.", rc);
      return rc;
   } else if (action == NVME_PEL_ACTION_RELEASE) {
      return 0;
   }

   fd = open(logPath, O_WRONLY|O_CREAT|O_TRUNC, 0666);
   if (fd == -1) {
      LogError("Failed to open log path.");
      return -ENOENT;
   }

   if (write(fd, (void *)&logHeader, sizeof(logHeader)) !=
       sizeof(logHeader)) {
      LogError("Failed to write telemetry log header.");
      close(fd);
      return -EIO;
   }

   if (logHeader.tll <= sizeof(logHeader)) {
      close(fd);
      return 0;
   }
   size = logHeader.tll - sizeof(logHeader);
   offset = sizeof(logHeader);
   logData = malloc(size);
   if (logData == NULL) {
      LogError("Failed to allocate data buffer.");
      close(fd);
      return -ENOMEM;
   }

   memset(logData, 0, size);
   rc = Nvme_GetLogPage(handle, NVME_LID_PERSISTENT_EVENT,
                        VMK_NVME_DEFAULT_NSID, logData, size,
                        offset, 0, NVME_PEL_ACTION_READ, 0, 0);
   if (rc) {
      LogError("Failed to fetch persistent event log at size %d, status 0x%x.",
               size, rc);
      free(logData);
      close(fd);
      return rc;
   }

   if (write(fd, (void *)logData, size) != size) {
      LogError("Failed to write telemetry log size %d.", size);
      rc = -EIO;
   }

   free(logData);
   close(fd);
   return rc;
}

int Nvme_GetLogPage(struct nvme_handle *handle, int lid, vmk_uint32 nsid, void *logData,
                    int dataLen, vmk_uint64 offset, int rae, int lsp, int lsi, int uuid)
{
   int rc = -1;
   NvmeUserIo uio;
   vmk_uint64 xferOffset;
   vmk_uint32 xferSize, maxXferSize, numd, size;

   if (dataLen % 4) {
      LogError("Invalid log data length %u.", dataLen);
      return -EINVAL;
   }

   if (dataLen <= 4096) {
      maxXferSize = 4096;
   } else {
      memset(&uio, 0, sizeof(uio));
      rc = Nvme_Ioctl(handle, NVME_IOCTL_GET_MAX_XFER_LEN, &uio);
      if (rc || uio.status) {
         LogError("Failed to get max transfer size.");
         if (rc) {
            return rc;
         } else {
            return uio.status;
         }
      }
      maxXferSize = uio.length;
   }

   memset(logData, 0, dataLen);
   xferOffset = 0;
   size = dataLen;
   while (size > 0) {
      xferSize = (size > maxXferSize) ? maxXferSize: size;
      memset(&uio, 0, sizeof(uio));
      numd = xferSize / 4 - 1;
      uio.cmd.getLogPage.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_LOG_PAGE;
      uio.cmd.getLogPage.nsid = nsid;
      uio.direction = XFER_FROM_DEV;
      uio.timeoutUs = adminTimeout;
      uio.cmd.getLogPage.cdw10.lid = lid;
      uio.cmd.getLogPage.cdw10.numdl = (vmk_uint16)(numd & 0xffff);
      uio.cmd.getLogPage.cdw11.numdu = (vmk_uint16)((numd >> 16) & 0xffff);
      uio.cmd.getLogPage.cdw10.lsp = lsp;
      uio.cmd.getLogPage.cdw10.rae = rae;
      uio.cmd.getLogPage.cdw11.lsi = lsi;
      uio.cmd.getLogPage.cdw14.uuid = uuid;
      uio.cmd.getLogPage.lpo = offset + xferOffset;
      uio.length = xferSize;
      uio.addr = (vmk_uintptr_t)(logData + xferOffset);
      rc = Nvme_AdminPassthru(handle, &uio);
      if (rc) {
         LogError("Failed to fetch log %d at offset 0x%lx, "
                  "size %d, status 0x%x.",
                  lid, offset + xferOffset, xferSize, rc);
         break;
      }
      size = size - xferSize;
      xferOffset = xferOffset + xferSize;
   }

   return rc;
}

int Nvme_GetFeature(struct nvme_handle *handle,
                    vmk_uint32 nsId,
                    int fid,
                    int select,
                    vmk_uint32 cdw11,
                    vmk_uint32 cdw12,
                    vmk_uint32 cdw13,
                    vmk_uint32 cdw14,
                    vmk_uint32 cdw15,
                    void *buf,
                    vmk_uint32 len,
                    vmk_uint32 *result)
{
   int rc;
   NvmeUserIo uio;
   memset(&uio, 0, sizeof(uio));

   if ((buf != NULL && len == 0) ||
       (buf == NULL && len != 0) ||
       result == NULL) {
      return -EINVAL;
   }

   uio.cmd.getFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_FEATURES;
   uio.cmd.getFeatures.nsid = nsId;
   uio.cmd.getFeatures.cdw10.fid = fid;
   uio.cmd.getFeatures.cdw10.sel = select;
   uio.cmd.getFeatures.cdw11 = cdw11;
   uio.cmd.getFeatures.cdw12 = cdw12;
   uio.cmd.getFeatures.cdw13 = cdw13;
   uio.cmd.getFeatures.cdw14 = cdw14;
   uio.cmd.getFeatures.cdw15 = cdw15;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = adminTimeout;
   uio.addr = (vmk_uintptr_t)buf;
   uio.length = len;
   rc = Nvme_AdminPassthru(handle, &uio);
   if (!rc) {
      *result = uio.comp.dw0;
      LogDebug("Feature %d value 0x%x", fid, uio.comp.dw0);
   }
   return rc;
}

int Nvme_SetFeature(struct nvme_handle *handle,
                    vmk_uint32 nsId,
                    int fid,
                    int save,
                    vmk_uint32 cdw11,
                    vmk_uint32 cdw12,
                    vmk_uint32 cdw13,
                    vmk_uint32 cdw14,
                    vmk_uint32 cdw15,
                    void *buf,
                    vmk_uint32 len)
{
   int rc;
   NvmeUserIo uio;
   memset(&uio, 0, sizeof(uio));

   if ((buf != NULL && len == 0) ||
       (buf == NULL && len != 0)) {
      return -EINVAL;
   }

   uio.cmd.setFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_SET_FEATURES;
   uio.cmd.setFeatures.nsid = nsId;
   uio.cmd.setFeatures.cdw10.fid = fid;
   uio.cmd.setFeatures.cdw10.sv = save;
   uio.cmd.setFeatures.cdw11.value = cdw11;
   uio.cmd.setFeatures.cdw12 = cdw12;
   uio.cmd.setFeatures.cdw13 = cdw13;
   uio.cmd.setFeatures.cdw14 = cdw14;
   uio.cmd.setFeatures.cdw15 = cdw15;
   uio.direction = XFER_TO_DEV;
   uio.timeoutUs = adminTimeout;
   uio.addr = (vmk_uintptr_t)buf;
   uio.length = len;
   rc = Nvme_AdminPassthru(handle, &uio);
   if (!rc) {
      LogDebug("Feature %d value 0x%x", fid, uio.comp.dw0);
   }
   return rc;
}
