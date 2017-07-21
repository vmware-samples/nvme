/*********************************************************************************
 * Copyright (c) 2013 VMware, Inc. All rights reserved.
 * ******************************************************************************/

#ifndef _NVME_LIB_H
#define _NVME_LIB_H

#include <nvme.h>
#include <nvme_mgmt.h>

/**
 * Command timeout in microseconds
 */
#define ADMIN_TIMEOUT (2 * 1000 * 1000)   /* 2 seconds */

/**
 * for firmware download
 */
//TODO: confirm max transfer size
#define NVME_MAX_XFER_SIZE            (8*1024)
#define MAX_FW_SLOT                   7
#define FW_REV_LEN                    8
#define MAX_ADAPTER_NAME_LEN  64
#define MAX_FW_PATH_LEN       512

/**
 * firmware activate action code
 */
#define NVME_FIRMWARE_ACTIVATE_ACTION_NOACT     0
#define NVME_FIRMWARE_ACTIVATE_ACTION_DLACT     1
#define NVME_FIRMWARE_ACTIVATE_ACTION_ACTIVATE  2
#define NVME_FIRMWARE_ACTIVATE_ACTION_RESERVED  3

/**
 * firmware activate successful but need reboot
 */
#define NVME_NEED_COLD_REBOOT  0x1

/**
 * Adapter instance list
 */
struct nvme_adapter_list {
   vmk_uint32             count;
   struct nvmeAdapterInfo adapters[NVME_MAX_ADAPTERS];
};


/**
 * Device handle
 */
struct nvme_handle {
   /** vmhba name */
   char                 name[VMK_MISC_NAME_MAX];
   /** management handle */
   vmk_MgmtUserHandle   handle;
};

typedef enum
{
   /**
     * @brief System Configuration command is used to change
     * device configurations. Dword12 used to define the subcommand opcode.
     */
   IDT_SYSTEM_CONFIG = 0xc1
}idt_admin_opcodes_e;

typedef enum
{
   /**
     * @brief User applications issue Create Namespace commands to
     * create a new namespace if the specific namespace identifier is not existing.
     */
   IDT_CREATE_NAMESPACE = 0x03,

   /**
     * @brief User applications use Delete Namespace commands to remove an existing
     * namespace in the flash media.
     */
   IDT_DELETE_NAMESPACE = 0x02

}idt_admin_subopcodes_e;

/**
  * @brief This is defined to differentiate vendor name of the controller.
  * TODO: Find a method to automatically acquire vendor name.
  */
typedef enum
{
   IDT_DEVICE = 0x111d
}vendor_device_info;

/**
 * Global data to hold all active NVMe adapters
 */
extern struct nvme_adapter_list adapterList;


/**
 * NVMe management interfaces
 */
struct nvme_handle * Nvme_Open(struct nvme_adapter_list *adapters, const char *name);
void Nvme_Close(struct nvme_handle *handle);
int Nvme_GetAdapterList(struct nvme_adapter_list *list);
int Nvme_AdminPassthru(struct nvme_handle *handle, struct usr_io *uio);
int Nvme_AdminPassthru_error(struct nvme_handle *handle, int cmd, struct usr_io *uio);
int Nvme_Identify(struct nvme_handle *handle, int ns, void *id);
int Nvme_Ioctl(struct nvme_handle *handle, int cmd, struct usr_io *uio);
int Nvme_FormatNvm(struct nvme_handle *handle, int ses, int pil, int pi, int ms, int lbaf, int ns);
int Nvme_SetLogLevel(int loglevel, int debuglevel);
int Nvme_SetTimeout(struct nvme_handle *handle, int timeout);
int Nvme_GetTimeout(struct nvme_handle *handle, int *timeout);

/**
 * NVMe management interfaces, IDT specific
 */
int Nvme_CreateNamespace_IDT(struct nvme_handle *handle, int ns, vmk_uint32 snu, vmk_uint32 nnu);
int Nvme_DeleteNamespace_IDT(struct nvme_handle *handle, int ns);

/**
  * NVMe firmware operation interfaces
  */
int Nvme_FWLoadImage(char *fw_path, void **fw_buf, int *fw_size);
int Nvme_FWDownload(struct nvme_handle *handle, int slot,  unsigned char *rom_buf, int rom_size);
int Nvme_FWFindSlot(struct nvme_handle *handle, int *slot);
int Nvme_FWActivate(struct nvme_handle *handle, int slot, int action);

#endif /* _NVME_LIB_H */
