/*********************************************************************************
 * Copyright (c) 2013 VMware, Inc. All rights reserved.
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
   struct nvmeAdapterInfo *adapter;
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
   signature.numCallbacks = NVME_MGMT_CTRLR_NUM_CALLBACKS;
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
   vmk_MgmtUserHandle driverHandle;
   int rc = 0;

   rc = vmk_MgmtUserInit(&globalSignature, 0LL, &driverHandle);
   if (rc) {
      return rc;
   }

   rc = vmk_MgmtUserCallbackInvoke(driverHandle, 0LL,
      NVME_MGMT_GLOBAL_CB_SETLOGLEVEL,
      &loglevel, &debuglevel);

   vmk_MgmtUserDestroy(driverHandle);
   return rc;
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
Nvme_Ioctl(struct nvme_handle *handle, int cmd, struct usr_io *uio)
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
Nvme_AdminPassthru(struct nvme_handle *handle, struct usr_io *uio)
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
Nvme_AdminPassthru_error(struct nvme_handle *handle,int cmd, struct usr_io *uio)
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
Nvme_Identify(struct nvme_handle *handle, int ns, void *id)
{
   int rc = 0;
   struct usr_io uio;

   memset(&uio, 0, sizeof(uio));

   uio.cmd.header.opCode = NVM_ADMIN_CMD_IDENTIFY;

   if (ns < 0) {
      uio.cmd.cmd.identify.controllerStructure = IDENTIFY_CONTROLLER;
   } else {
      uio.cmd.cmd.identify.controllerStructure = IDENTIFY_NAMESPACE;
      uio.cmd.header.namespaceID = ns;
   }

   uio.namespaceID = 0xff;
   uio.direction = XFER_FROM_DEV;

   uio.timeoutUs = ADMIN_TIMEOUT;
#define PAGE_SIZE 4096
   uio.length = PAGE_SIZE;
   uio.addr = (vmk_uintptr_t)id;

   rc = Nvme_AdminPassthru(handle, &uio);

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
Nvme_CreateNamespace_IDT(struct nvme_handle *handle, int ns, vmk_uint32 snu, vmk_uint32 nnu)
{
   int rc = 0;
   struct usr_io uio;

   memset(&uio, 0, sizeof(uio));

   uio.cmd.header.opCode = IDT_SYSTEM_CONFIG;
   uio.cmd.cmd.vendorSpecific.vndrCDW12 = IDT_CREATE_NAMESPACE;
   uio.cmd.cmd.vendorSpecific.vndrCDW13 = snu;
   uio.cmd.cmd.vendorSpecific.vndrCDW14 = nnu;
   uio.cmd.header.namespaceID = ns;
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
   struct usr_io uio;
   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = IDT_SYSTEM_CONFIG;
   uio.cmd.cmd.vendorSpecific.vndrCDW12 = IDT_DELETE_NAMESPACE;
   uio.namespaceID = ns;
   uio.cmd.header.namespaceID = ns;
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

int Nvme_FWDownload(struct nvme_handle *handle, int slot,  unsigned char *rom_buf, int rom_size)
{
   struct usr_io uio;
   int offset, size;
   int rc;
   void *chunk;

   if ((chunk = malloc(NVME_MAX_XFER_SIZE)) == NULL) {
      fprintf(stderr, "ERROR: Failed to malloc %d bytes.\n", NVME_MAX_XFER_SIZE);
      return -ENOMEM;
   }

   for (offset = 0; offset < rom_size; offset += NVME_MAX_XFER_SIZE) {
      size = ((rom_size-offset) >= NVME_MAX_XFER_SIZE) ?
         NVME_MAX_XFER_SIZE : (rom_size-offset);
      memcpy(chunk, rom_buf+offset, size);

      memset(&uio, 0, sizeof(uio));
      uio.cmd.header.opCode = NVM_ADMIN_CMD_FIRMWARE_DOWNLOAD;
      uio.cmd.header.namespaceID = -1;
      uio.direction = XFER_TO_DEV;
      uio.timeoutUs = ADMIN_TIMEOUT;
      uio.cmd.cmd.firmwareDownload.numDW = (size / sizeof(vmk_uint32)) - 1;
      uio.cmd.cmd.firmwareDownload.offset = offset / sizeof(vmk_uint32);
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
   struct usr_io uio;
   union {
      struct error_log errLog;
      struct smart_log smartLog;
      struct firmware_slot_log fwSlotLog;
   } log;
   unsigned char fw_rev_slot[MAX_FW_SLOT][FW_REV_LEN];
   int rc = -1;
   int i;

   assert(handle && slot);
   assert(*slot > 0 && *slot < 8);

   memset(&uio, 0, sizeof(uio));

   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_LOG_PAGE;
   uio.cmd.header.namespaceID = -1;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getLogPage.LogPageID = GLP_ID_FIRMWARE_SLOT_INFO;
   uio.cmd.cmd.getLogPage.numDW = GLP_LEN_FIRMWARE_SLOT_INFO / 4 - 1;
   uio.length = GLP_LEN_FIRMWARE_SLOT_INFO;
   uio.addr = (vmk_uintptr_t)&log.fwSlotLog;
   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      return -EIO;
   }

   /* copy firmware revision info from log.fwSlotLog.FirmwareRevisionSlotX. */
   memcpy(fw_rev_slot, log.fwSlotLog.FirmwareRevisionSlot1, sizeof(fw_rev_slot));

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

int Nvme_FWActivate(struct nvme_handle *handle, int slot, int action)
{
   struct usr_io uio;
   int rc = -1;

   assert(handle);
   assert(slot > 0 && slot < 8);
   assert(action >= 0 && action < 4);

   memset(&uio, 0, sizeof(uio));

   uio.cmd.header.opCode = NVM_ADMIN_CMD_FIRMWARE_ACTIVATE;
   uio.cmd.header.namespaceID = -1;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.firmwareActivate.slot = slot;
   uio.cmd.cmd.firmwareActivate.action = action;
   rc = Nvme_AdminPassthru(handle, &uio);
   if (rc) {
      /* Fail to activate NVMe firmware: ioctl failed. */
      return rc;
   }
   else if (uio.comp.SCT == SF_SCT_CMD_SPC_ERR &&
       uio.comp.SC == SF_SC_FIRMWARE_REQUIRES_RESET) {
      /* Activate NVMe firmware successful but requests server reboot. */
      return NVME_NEED_COLD_REBOOT;
   }
   else {
      return (uio.comp.SCT << 8 | uio.comp.SC);
   }
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
int Nvme_FormatNvm(struct nvme_handle *handle, int ses, int pil, int pi, int ms, int lbaf, int ns)
{
   int rc = 0;
   struct usr_io uio;

   memset(&uio, 0, sizeof(uio));

   uio.cmd.header.opCode = NVM_ADMIN_CMD_FORMAT_NVM;
   uio.cmd.header.namespaceID = ns;
   uio.cmd.cmd.format.formatOption = ses << FORMAT_SECURITY_SHIFT |
      pil  << FORMAT_PIL_SHIFT  |
      pi   << FORMAT_PI_SHIFT   |
      ms   << FORMAT_META_SHIFT |
      lbaf << FORMAT_LBAF_SHIFT;

   uio.namespaceID = ns;
   /* Set timeout as 30mins, we do see some devices need ~20 minutes to format. */
   uio.timeoutUs = ADMIN_TIMEOUT * 900;
   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      return rc;
   } else {
      return (uio.comp.SCT << 8 | uio.comp.SC);
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
   struct usr_io uio;

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
   struct usr_io uio;

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
