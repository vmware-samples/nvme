/*********************************************************************************
 * Copyright (c) 2013-2021 VMware, Inc. All rights reserved.
 * ******************************************************************************/

#ifndef _NVME_LIB_H
#define _NVME_LIB_H

#include "nvme_mgmt.h"

/**
 * Command timeout in microseconds
 */
#define ADMIN_TIMEOUT (60 * 1000 * 1000)         /* 60 seconds */
#define FORMAT_TIMEOUT (30 * 60 * 1000 * 1000)   /* 30 minutes */
#define FIRMWARE_DOWNLOAD_TIMEOUT (30 * 60 * 1000 * 1000)   /* 30 minutes */
#define FIRMWARE_ACTIVATE_TIMEOUT (30 * 60 * 1000 * 1000)   /* 30 minutes */

/**
 * maximum number of namespaces supported per controller
 */
#define NVME_MAX_NAMESPACE_PER_CONTROLLER (1024)

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
#define NVME_FIRMWARE_ACTIVATE_ACTION_NOACT        0
#define NVME_FIRMWARE_ACTIVATE_ACTION_DLACT        1
#define NVME_FIRMWARE_ACTIVATE_ACTION_ACTIVATE     2
#define NVME_FIRMWARE_ACTIVATE_ACTION_ACT_NORESET  3
#define NVME_FIRMWARE_ACTIVATE_ACTION_RESERVED     4

/**
 * for download telemetry data
 */
#define NVME_TELEMETRY_DATA_BLK_SIZE 512

#define NS_UNALLOCATED 0x0
#define NS_ALLOCATED   0x1
#define NS_INACTIVE    0x2
#define NS_ACTIVE      0x3

#define NVME_LID_PERSISTENT_EVENT 0xd
#define NVME_CTLR_IDENT_LPA_PERSISTENT_EVENT (0x1 << 4)
#define NVME_PEL_ACTION_READ 0x0
#define NVME_PEL_ACTION_ESTABLISH_AND_READ 0x1
#define NVME_PEL_ACTION_RELEASE 0x2
typedef struct nvme_persistent_event_log_header {
   vmk_uint8 lid;
   vmk_uint8 reserved1[3];
   vmk_uint32 tnev;
   vmk_uint64 tll;
   vmk_uint8 revision;
   vmk_uint8 reserved2;
   vmk_uint16 thl;
   vmk_uint64 timestamp;
   vmk_uint8 poh[16];
   vmk_uint64 pcc;
   vmk_uint16 vid;
   vmk_uint16 ssvid;
   vmk_uint8 sn[20];
   vmk_uint8 mn[40];
   vmk_uint8 subnqn[256];
   vmk_uint8 reserved3[108];
   vmk_uint8 bitmap[32];
} VMK_ATTRIBUTE_PACKED nvme_persistent_event_log_header;

/**
 * Adapter instance list
 */
struct nvme_adapter_list {
   vmk_uint32 count;
   NvmeAdapterInfo adapters[NVME_MGMT_MAX_ADAPTERS];
};

struct nvme_ctrlr_list {
   vmk_uint16 ctrlrId[2048];
};

/* Mulitple namesapce management */
struct nvme_ns_list {
   vmk_uint32 nsId[1024];
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
struct
nvme_handle * Nvme_Open(struct nvme_adapter_list *adapters, const char *name);

void
Nvme_Close(struct nvme_handle *handle);

int
Nvme_GetAdapterList(struct nvme_adapter_list *list);

int
Nvme_AdminPassthru(struct nvme_handle *handle, NvmeUserIo *uio);

int
Nvme_AdminPassthru_error(struct nvme_handle *handle, int cmd, NvmeUserIo *uio);

int
Nvme_Identify(struct nvme_handle *handle, int cns, int cntId, int nsId, void *id);

int
Nvme_Ioctl(struct nvme_handle *handle, int cmd, NvmeUserIo *uio);

int Nvme_FormatNvm(struct nvme_handle *handle,
                   int ses,
                   int pil,
                   int pi,
                   int ms,
                   int lbaf,
                   int ns);

int
Nvme_SetLogLevel(int loglevel, int debuglevel);

int
Nvme_SetTimeout(struct nvme_handle *handle, int timeout);

int
Nvme_GetTimeout(struct nvme_handle *handle, int *timeout);

int
Nvme_NsMgmtAttachSupport(struct nvme_handle *handle);

int
Nvme_ValidNsId(struct nvme_handle *handle, int nsId);

int
Nvme_AllocatedNsId(struct nvme_handle *handle, int nsId);

int
Nvme_AttachedNsId(struct nvme_handle *handle, int nsId);

int
Nvme_NsMgmtCreate(struct nvme_handle *handle, vmk_NvmeIdentifyNamespace *idNs,
                  int *cmdStatus);

int
Nvme_NsMgmtDelete(struct nvme_handle *handle, int nsId);

/**
 * NVMe management interfaces, IDT specific
 */
int
Nvme_CreateNamespace_IDT(struct nvme_handle *handle,
                         int ns,
                         vmk_uint32 snu,
                         vmk_uint32 nnu);

int
Nvme_DeleteNamespace_IDT(struct nvme_handle *handle, int ns);

int
Nvme_NsAttach(struct nvme_handle *handle,
              int sel,
              int nsId,
              struct nvme_ctrlr_list *ctrlrList,
              int *cmdStatus);

int
Nvme_NsUpdate(struct nvme_handle *handle, int nsId);

int
Nvme_NsListUpdate(struct nvme_handle *handle, int sel, int nsId);
int
Nvme_NsGetStatus(struct nvme_handle *handle, int nsId, int *status);

int
Nvme_NsSetStatus(struct nvme_handle *handle, int nsId, int status);

/**
  * NVMe firmware operation interfaces
  */
int
Nvme_FWLoadImage(char *fw_path, void **fw_buf, int *fw_size);

int
Nvme_FWDownload(struct nvme_handle *handle,
                unsigned char *rom_buf,
                int rom_size);

int
Nvme_FWFindSlot(struct nvme_handle *handle, int *slot);

int
Nvme_FWActivate(struct nvme_handle *handle, int slot, int action, int *cmdStatus);

int Nvme_GetTelemetryData(struct nvme_handle *handle, char *telemetryPath,
                          int lid, int dataArea);

int Nvme_GetLogPage(struct nvme_handle *handle, int lid, int nsid,
                    void *logData, int dataLen, vmk_uint64 offset, int rae,
                    int lsp, int lsi, int uuid);

int Nvme_GetPersistentEventLog(struct nvme_handle *handle, char *logPath,
                               int action);
int Nvme_WriteRawDataToFile(void *data, int len, char *path);

#endif /* _NVME_LIB_H */
