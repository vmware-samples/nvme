/*******************************************************************************
 * Portions Copyright (c) 2013-2015  VMware, Inc. All rights reserved.
 *
 * Portions Copyright (c) 2012-2014, Micron Technology, Inc.
 *
 * Portions Copyright (C) 2012 Intel Corporation
 * All rights reserved.
 *******************************************************************************/

/*
 * @file: nvme_scsi.c --
 *
 *    Translate between SCSI and NVMe
 */

//#include "../../common/kernel/nvme_debug.h"
#include "oslib.h"
#include "../../common/kernel/nvme_private.h"
//#include "../../common/kernel/nvme_core.h"
//#include "../../common/kernel/nvme_scsi_cmds.h"


/*
 * TODO: this file needs to be reflacted.
 */


#define SCSI_MAX_LUNS (255)


#define SCSI_ASC_LBA_OUT_OF_RANGE   (0x21)

/**
 * _lto2b_16 - Little Endian to Big Endian - 16 bit.
 */
#define _lto2b_16(val_16) (((val_16) >> 8) | (((val_16) & 0xFF) << 8))

/**
 * _lto2b_32 - Little Endian to Big Endian - 32 bit.
 */
#define _lto2b_32(val_32) (((val_32) >> 24) | (((val_32) & 0x00FF0000) >> 8) | (((val_32) & 0x0000FF00) << 8) | (((val_32) & 0x000000FF) << 24))


#define ASCQ_LOGICAL_UNIT_FAILURE       0x01
#define ASCQ_SPACE_ALLOCATION_FAILED    0x07
#define ASCQ_INTERNAL_TARGET_FAILURE    0x00
#define ASCQ_NO_ACCESS_RIGHTS           0x02
#define ASCQ_TARGET_PORT_IN_STANDBY     0x0B
#define ASCQ_TARGET_RESET               0x02
#define ASCQ_CAUSE_NOT_REPORTABLE       0x00
#define ASCQ_FORMAT_IN_PROGRESS         0x04
#define ASCQ_OPERATION_IN_PROGRESS      0x07
#define ASCQ_PARAMETER_VALUE_INVALID    0x02
#define ASCQ_WARNING_TEMP_OUT_OF_RANGE  0x01

#define ASC_SCSI_WARNING              0x0B
#define ASC_SCSI_LBA_OUT_OF_RANGE     0x21


/**
 * NvmeScsiCmd_SetReturnStatus - refer to header file.
 */
VMK_ReturnStatus
NvmeScsiCmd_SetReturnStatus(void *cmdPtr, Nvme_Status nvmeStatus)
{
   VMK_ReturnStatus  vmkStatus = VMK_OK;
   vmk_ScsiSenseData senseData;
   vmk_ScsiCommand  *vmkCmd;
   vmk_Bool          senseValid = VMK_FALSE;
   int               senseKey, senseAsc, senseAscq;
   int               hostStatus, deviceStatus;

   GET_VMK_SCSI_CMD(cmdPtr, vmkCmd);

#if NVME_DEBUG
   if (VMK_UNLIKELY(nvmeStatus)) {
      DPRINT_CMD("Complete vmkCmd %p[%Xh I:%p SN:0x%lx] xfer: %d/%d " "status 0x%x, %s.",
               vmkCmd, vmkCmd->cdb[0], vmkCmd->cmdId.initiator,
               vmkCmd->cmdId.serialNumber,
               vmkCmd->bytesXferred, vmkCmd->requiredDataLen,
               nvmeStatus, NvmeCore_StatusToString(nvmeStatus));
   }
#endif

   switch(nvmeStatus) {
      case NVME_STATUS_SUCCESS:
         hostStatus   = VMK_SCSI_HOST_OK;
         deviceStatus = VMK_SCSI_DEVICE_GOOD;
         break;
      case NVME_STATUS_DEVICE_MISSING:
         hostStatus   = VMK_SCSI_HOST_OK;
         deviceStatus = VMK_SCSI_DEVICE_CHECK_CONDITION;
         senseKey     = VMK_SCSI_SENSE_KEY_ILLEGAL_REQUEST;
         senseAsc     = VMK_SCSI_ASC_LU_NOT_SUPPORTED;
         senseAscq    = 0;
         senseValid   = VMK_TRUE;
         break;
      case NVME_STATUS_NOT_READY:
         hostStatus   = VMK_SCSI_HOST_BUS_BUSY;
         deviceStatus = VMK_SCSI_DEVICE_GOOD;
         break;
      case NVME_STATUS_IN_RESET:
         hostStatus   = VMK_SCSI_HOST_OK;
         deviceStatus = VMK_SCSI_DEVICE_BUSY;
         break;
      case NVME_STATUS_QUIESCED:
         hostStatus   = VMK_SCSI_HOST_NO_CONNECT;
         deviceStatus = VMK_SCSI_DEVICE_GOOD;
         break;
      case NVME_STATUS_FATAL_ERROR:
         hostStatus   = VMK_SCSI_HOST_OK;
         deviceStatus = VMK_SCSI_DEVICE_CHECK_CONDITION;
         senseKey     = VMK_SCSI_SENSE_KEY_HARDWARE_ERROR;
         senseAsc     = VMK_SCSI_ASC_LOGICAL_UNIT_ERROR;
         senseAscq    = 0x01; /* LOGICAL UNIT FAILURE */
         senseValid   = VMK_TRUE;
         break;
      case NVME_STATUS_MEDIUM_ERROR:
         hostStatus   = VMK_SCSI_HOST_OK;
         deviceStatus = VMK_SCSI_DEVICE_CHECK_CONDITION;
         senseKey     = VMK_SCSI_SENSE_KEY_MEDIUM_ERROR;
         senseAsc     = VMK_SCSI_ASC_LOGICAL_UNIT_ERROR;
         senseAscq    = 0x01; /* LOGICAL UNIT FAILURE */
         senseValid   = VMK_TRUE;
         break;
      case NVME_STATUS_QFULL:
         hostStatus   = VMK_SCSI_HOST_OK;
         deviceStatus = VMK_SCSI_DEVICE_QUEUE_FULL;
         break;
      case NVME_STATUS_BUSY:
         hostStatus   = VMK_SCSI_HOST_OK;
         deviceStatus = VMK_SCSI_DEVICE_BUSY;
         break;
      case NVME_STATUS_INVALID_OPCODE:
         hostStatus   = VMK_SCSI_HOST_OK;
         deviceStatus = VMK_SCSI_DEVICE_CHECK_CONDITION;
         senseKey     = VMK_SCSI_SENSE_KEY_ILLEGAL_REQUEST;
         senseAsc     = VMK_SCSI_ASC_INVALID_COMMAND_OPERATION;
         senseAscq    = 0;
         senseValid   = VMK_TRUE;
         break;
      case NVME_STATUS_INVALID_FIELD_IN_CDB:
         hostStatus   = VMK_SCSI_HOST_OK;
         deviceStatus = VMK_SCSI_DEVICE_CHECK_CONDITION;
         senseKey     = VMK_SCSI_SENSE_KEY_ILLEGAL_REQUEST;
         senseAsc     = VMK_SCSI_ASC_INVALID_FIELD_IN_CDB;
         senseAscq    = 0;
         senseValid   = VMK_TRUE;
         break;
      case NVME_STATUS_INVALID_NS_OR_FORMAT:
         hostStatus   = VMK_SCSI_HOST_OK;
         deviceStatus = VMK_SCSI_DEVICE_CHECK_CONDITION;
         senseKey     = VMK_SCSI_SENSE_KEY_ILLEGAL_REQUEST;
         senseAsc     = VMK_SCSI_ASC_LU_NOT_SUPPORTED;
         senseAscq    = 0;
         senseValid   = VMK_TRUE;
         break;
      case NVME_STATUS_NS_NOT_READY:
         hostStatus   = VMK_SCSI_HOST_OK;
         deviceStatus = VMK_SCSI_DEVICE_CHECK_CONDITION;
         senseKey     = VMK_SCSI_SENSE_KEY_NOT_READY;
         senseAsc     = VMK_SCSI_ASC_LU_NOT_READY;
         senseAscq    = VMK_SCSI_ASC_LU_NOT_READY_ASCQ_OPERATION_IN_PROGRESS;
         senseValid   = VMK_TRUE;
         break;
      case NVME_STATUS_NS_OFFLINE:
         hostStatus   = VMK_SCSI_HOST_OK;
         deviceStatus = VMK_SCSI_DEVICE_CHECK_CONDITION;
         senseKey     = VMK_SCSI_SENSE_KEY_ILLEGAL_REQUEST;
         senseAsc     = VMK_SCSI_ASC_LU_NOT_SUPPORTED;
         senseAscq    = 0;
         senseValid   = VMK_TRUE;
         break;
      case NVME_STATUS_IO_ERROR:
         hostStatus   = VMK_SCSI_HOST_OK;
         deviceStatus = VMK_SCSI_DEVICE_CHECK_CONDITION;
         senseKey     = VMK_SCSI_SENSE_KEY_HARDWARE_ERROR;
         if (vmk_ScsiIsReadCdb(vmkCmd->cdb[0])) {
            senseAsc  = VMK_SCSI_ASC_UNRECOVERED_READ_ERROR;
         } else {
            senseAsc  = VMK_SCSI_ASC_WRITE_ERROR;
         }
         senseAscq    = 0;
         senseValid   = VMK_TRUE;
         break;
      case NVME_STATUS_IO_WRITE_ERROR:
         hostStatus   = VMK_SCSI_HOST_OK;
         deviceStatus = VMK_SCSI_DEVICE_CHECK_CONDITION;
         senseKey     = VMK_SCSI_SENSE_KEY_HARDWARE_ERROR;
         senseAsc     = VMK_SCSI_ASC_WRITE_ERROR;
         senseAscq    = 0;
         senseValid   = VMK_TRUE;
         break;
      case NVME_STATUS_IO_READ_ERROR:
         hostStatus   = VMK_SCSI_HOST_OK;
         deviceStatus = VMK_SCSI_DEVICE_CHECK_CONDITION;
         senseKey     = VMK_SCSI_SENSE_KEY_HARDWARE_ERROR;
         senseAsc     = VMK_SCSI_ASC_UNRECOVERED_READ_ERROR;
         senseAscq    = 0;
         senseValid   = VMK_TRUE;
         break;
      case NVME_STATUS_ABORTED:
      case NVME_STATUS_TIMEOUT:
         hostStatus   = VMK_SCSI_HOST_ABORT;
         deviceStatus = VMK_SCSI_DEVICE_COMMAND_TERMINATED;
         break;
      case NVME_STATUS_RESET:
         hostStatus   = VMK_SCSI_HOST_RESET;
         deviceStatus = VMK_SCSI_DEVICE_COMMAND_TERMINATED;
         break;
      case NVME_STATUS_WOULD_BLOCK:
         /**
          * WOULD_BLOCK status code should be handled internally and should not
          * reach here.
          */
         VMK_ASSERT(0);
         hostStatus   = VMK_SCSI_HOST_ERROR;
         deviceStatus = VMK_SCSI_DEVICE_GOOD;
         break;
      case NVME_STATUS_UNDERRUN:
      case NVME_STATUS_OVERRUN:
         hostStatus   = VMK_SCSI_HOST_ERROR;
         deviceStatus = VMK_SCSI_DEVICE_GOOD;
         break;
      case NVME_STATUS_LBA_OUT_OF_RANGE:
         hostStatus   = VMK_SCSI_HOST_OK;
         deviceStatus = VMK_SCSI_DEVICE_CHECK_CONDITION;
         senseKey     = VMK_SCSI_SENSE_KEY_ILLEGAL_REQUEST;
         senseAsc     = SCSI_ASC_LBA_OUT_OF_RANGE;
         senseAscq    = 0;
         senseValid   = VMK_TRUE;
         break;
      case NVME_STATUS_CAPACITY_EXCEEDED:
         hostStatus   = VMK_SCSI_HOST_OK;
         deviceStatus = VMK_SCSI_DEVICE_CHECK_CONDITION;
         senseKey     = VMK_SCSI_SENSE_KEY_MEDIUM_ERROR;
         senseAsc     = 0;
         senseAscq    = 0;
         senseValid   = VMK_TRUE;
         break;
      case NVME_STATUS_WRITE_PROTECT:
         hostStatus    = VMK_SCSI_HOST_NO_CONNECT;
         deviceStatus  = VMK_SCSI_DEVICE_CHECK_CONDITION;
         senseKey      = VMK_SCSI_SENSE_KEY_DATA_PROTECT;
         senseAsc      = VMK_SCSI_ASC_WRITE_PROTECTED;
         senseAscq     = ASCQ_SPACE_ALLOCATION_FAILED;
         senseValid   = VMK_TRUE;
         break;
      case NVME_STATUS_OVERTEMP:
	// PDL state with Hardware Error 00/02/04/3E/01
	hostStatus    = VMK_SCSI_HOST_NO_CONNECT;
	deviceStatus  = VMK_SCSI_DEVICE_CHECK_CONDITION;
	senseKey      = VMK_SCSI_SENSE_KEY_HARDWARE_ERROR;
	senseAsc      = ASC_SCSI_WARNING;
	senseAscq     = ASCQ_WARNING_TEMP_OUT_OF_RANGE;
        senseValid   = VMK_TRUE;
        break;
      case NVME_STATUS_CONFLICT_ATTRIBUTES:
      case NVME_STATUS_INVALID_PI:
      case NVME_STATUS_PROTOCOL_ERROR:
      case NVME_STATUS_FAILURE:
      default:
         /**
          * For generic failures and catch-all failures, since we don't know why
          * the command has failed, just suggest a limited retry to PSA layer.
          */
         hostStatus   = VMK_SCSI_HOST_ERROR;
         deviceStatus = VMK_SCSI_DEVICE_GOOD;
         break;
   }

   vmkCmd->status.host   = hostStatus;
   vmkCmd->status.device = deviceStatus;
   vmkCmd->status.plugin = VMK_SCSI_PLUGIN_GOOD;

   if (VMK_UNLIKELY(senseValid)) {
      vmk_Memset(&senseData, 0, sizeof(senseData));

      senseData.valid = VMK_TRUE;

      senseData.error = VMK_SCSI_SENSE_ERROR_CURCMD;
      senseData.key   = senseKey;
      senseData.asc   = senseAsc;
      senseData.ascq  = senseAscq;

      ScsiCmdSetSenseData(&senseData, vmkCmd, sizeof(senseData));
   }

   SET_SCSI_SENSE_LEGACY(&senseData, cmdPtr, sizeof(senseData));

   return vmkStatus;
}


/**
 * SCSI LUN data structure, Single level LUN structure using peripheral device addressing method
 *
 * sam4r14, table 11
 */
typedef struct {
   /** Bus Identifier (00h) */
   vmk_uint8 busid      : 6;
   /** Address Method (00b) */
   vmk_uint8 addrmethod : 2;
   /** Target or LUN */
   vmk_uint8 lunid;
   /** Null second level LUN (0000h) */
   vmk_uint16 sllid;
   /** Null third level LUN (0000h) */
   vmk_uint16 tllid;
   /** Null fourth level LUN (0000h) */
   vmk_uint16 fllid;
} VMK_ATTRIBUTE_PACKED ScsiLun;


/**
 * SCSI Report LUNs response data
 *
 * spc4r36, table 286
 */
typedef struct ScsiReportLunsData {
   /** Lun list length */
   vmk_uint32 lunListLength;
   /** Reserved */
   vmk_uint32 reserved;
   /** LUN list */
   ScsiLun lunList[SCSI_MAX_LUNS];
} VMK_ATTRIBUTE_PACKED ScsiReportLunsData;

#define MIN_TX_LEN_FOR_REPORT_LUNS_CMD  16

/**
 * Handle SCSI Report LUNs command
 *
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to namespace
 */
static Nvme_Status
NvmeScsiCmd_DoReportLuns(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd, struct NvmeNsInfo *ns)
{
   ScsiReportLunsData response_data;
   vmk_ListLinks *itemPtr;
   struct NvmeNsInfo *nsInfo;
   vmk_uint16                 count       = 0;
   vmk_ScsiReportLunsCommand *cmd         = (vmk_ScsiReportLunsCommand *) vmkCmd->cdb;
   vmk_uint32                 transferLen = _lto2b_32(cmd->len);

   // Validating the Report Luns command cdb and retuning failure when
   // 1. Reserved fields are non zero.
   // 2. Select Report is more than 0x02.
   // 3. Allocation length is set with less than 16, as per SPC3r23 spec.
   // 4. Control byte is non zero
   if (cmd->resv1 != 0 ||
//       cmd->selectReport > 0x02 ||
       cmd->resv2 != 0 || cmd->resv3 != 0 ||
       transferLen < MIN_TX_LEN_FOR_REPORT_LUNS_CMD ||
       cmd->resv4 != 0 || cmd->resv5 != 0 ||
       cmd->control != 0){
      return NVME_STATUS_INVALID_FIELD_IN_CDB;
   }

   vmk_Memset(&response_data, 0, sizeof(response_data));

   VMK_LIST_FORALL(&ctrlr->nsList, itemPtr) {
      nsInfo = VMK_LIST_ENTRY(itemPtr, struct NvmeNsInfo, list);
      if (count >= SCSI_MAX_LUNS) {
         IPRINT("Available LUN counts are exceeding the supported SCSI_MAX_LUNS count of %d", SCSI_MAX_LUNS);
         break;
      }

      if (nsInfo->blockCount != 0) {
         response_data.lunList[count].addrmethod = 0; /* peripheral device addressing method */
         response_data.lunList[count].busid      = 0;
         response_data.lunList[count++].lunid    = nsInfo->id - 1;
         DPRINT_NS("lun %d found, capacity %ld.", nsInfo->id - 1, nsInfo->blockCount);
      } else {
         DPRINT_NS("empty lun %d found, skipping.", nsInfo->id);
      }
   }

   response_data.lunListLength = vmk_CPUToBE32(count * 8);

   // The max data transfer to the host is the value in (count * 8) + 8 (plus 8 is the size of report Luns response data header)
   transferLen = (transferLen < ((count * 8) + 8)) ? transferLen : ((count * 8) + 8);

   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, transferLen);
   vmkCmd->bytesXferred = transferLen;

   return NVME_STATUS_SUCCESS;
}

#define SCSI_INQUIRY_00H 0x00
#define SCSI_INQUIRY_80H 0x80
#define SCSI_INQUIRY_83H 0x83
#define SCSI_INQUIRY_86H 0x86
#define SCSI_INQUIRY_B0H 0xB0
#define SCSI_INQUIRY_B1H 0xB1


/**
 * Handle SCSI Standard Inquiry command
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 */
static Nvme_Status
NvmeScsiCmd_DoInquiryStd(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd, struct NvmeNsInfo *ns)
{
   vmk_ScsiInquiryResponse response_data;
   vmk_ScsiInquiryCmd        *inquiryCmd     = (vmk_ScsiInquiryCmd *)vmkCmd->cdb;
   vmk_uint16                 transferLen    = 0;

   transferLen = _lto2b_16(inquiryCmd->length);

   vmk_Memset(&response_data, 0, sizeof(response_data));

   response_data.ansi = 0x6; /* SPC-4 */
   response_data.dataformat = 0x2; /* SPC-4 */
   response_data.optlen = 0x1f;
   response_data.protect = END2END_DSP_TYPE(ns->dataProtSet) == 0 ? 0 : 1; /* calculated by Identify Namespace Data */
   vmk_Memcpy(response_data.manufacturer, "NVMe    ", sizeof(response_data.manufacturer));
   vmk_Memcpy(response_data.product, ctrlr->model, sizeof(response_data.product));
   vmk_Memcpy(response_data.revision, ctrlr->firmwareRev, sizeof(response_data.revision));

   transferLen = (transferLen < sizeof(response_data)) ? transferLen : sizeof(response_data);

   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, transferLen);
   vmkCmd->bytesXferred = transferLen;

   return NVME_STATUS_SUCCESS;
}

/**
 * Supported VPD Pages
 *
 * Currently only 00h, 80h, 83h, B0h, B1h implemented.
 */
#if NVME_ENABLE_SCSI_DEVICEID
#define MAX_SUPPORTED_VPD_PAGES (5)
#else
#define MAX_SUPPORTED_VPD_PAGES (3)
#endif /* NVME_ENABLE_SCSI_DEVICEID */


/**
 * SCSI Inquiry VPD 00 page response data
 *
 * spc4r36 table 462
 */
typedef struct {
   /** Inquiry VPD response header */
   vmk_ScsiInquiryVPDResponse header;
   /** Supported VPD page list */
   vmk_uint8 vpdList[MAX_SUPPORTED_VPD_PAGES];
} VMK_ATTRIBUTE_PACKED nvme_ScsiInquiryVpd00Response;


/**
 * Handle SCSI Inquiry Supported VPD Pages VPD page command
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 */
static Nvme_Status
NvmeScsiCmd_DoInquiryVpd00(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd, struct NvmeNsInfo *ns)
{
   nvme_ScsiInquiryVpd00Response response_data;
   vmk_ScsiInquiryCmd              *inquiryCmd     = (vmk_ScsiInquiryCmd *)vmkCmd->cdb;
   vmk_uint16                       transferLen    = 0;

   transferLen = _lto2b_16(inquiryCmd->length);

   vmk_Memset(&response_data, 0, sizeof(response_data));

   response_data.header.devclass = VMK_SCSI_CLASS_DISK;
   response_data.header.pqual = VMK_SCSI_PQUAL_CONNECTED;
   response_data.header.pageCode = SCSI_INQUIRY_00H;
   response_data.header.payloadLen = sizeof(vmk_uint8) * MAX_SUPPORTED_VPD_PAGES;

   response_data.vpdList[0] = SCSI_INQUIRY_00H;
#if NVME_ENABLE_SCSI_DEVICEID
   response_data.vpdList[1] = SCSI_INQUIRY_80H;
   response_data.vpdList[2] = SCSI_INQUIRY_83H;
   response_data.vpdList[3] = SCSI_INQUIRY_B0H;
   response_data.vpdList[4] = SCSI_INQUIRY_B1H;
#else
   response_data.vpdList[1] = SCSI_INQUIRY_B0H;
   response_data.vpdList[2] = SCSI_INQUIRY_B1H;
#endif /* NVME_ENABLE_SCSI_DEVICEID */

   transferLen = (transferLen < sizeof(response_data)) ? transferLen : sizeof(response_data);

   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, transferLen);
   vmkCmd->bytesXferred = transferLen;

   return NVME_STATUS_SUCCESS;
}


#if NVME_ENABLE_SCSI_DEVICEID

/**
 * Serial number length
 */
#define MAX_SERIAL_NUMBER_LENGTH (20)


/**
 * SCSI Inquiry Unit Serial Number VPD page response data
 *
 * spc4r36, table 661
 */
typedef struct {
   /** Inquiry VPD response header */
   vmk_ScsiInquiryVPDResponse header;
   /** PRODUCT SERIAL NUMBER */
   char serialNumber[MAX_SERIAL_NUMBER_LENGTH];
} VMK_ATTRIBUTE_PACKED nvme_ScsiInquiryVpd80Response;


#define PCIE_VID_SAMSUNG         (0x144D)
#define SAMSUNG_PRE_PROD_SERIAL  ("1234                ")


/**
 * Handle SCSI Inquiry Unit Serial Number VPD page command
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 */
static Nvme_Status
NvmeScsiCmd_DoInquiryVpd80(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd,
                           struct NvmeNsInfo *ns)
{
   nvme_ScsiInquiryVpd80Response response_data;
   vmk_uint64 eui64 = ns->eui64;
   vmk_uint8 *bytes = (vmk_uint8 *)&eui64;
   vmk_ScsiInquiryCmd              *inquiryCmd     = (vmk_ScsiInquiryCmd *)vmkCmd->cdb;
   vmk_uint16                       transferLen    = 0;
   char buffer[MAX_SERIAL_NUMBER_LENGTH + 1]; /* including trailing '\0' */

   transferLen = _lto2b_16(inquiryCmd->length);

   vmk_Memset(&response_data, 0, sizeof(response_data));

   response_data.header.devclass = VMK_SCSI_CLASS_DISK;
   response_data.header.pqual = VMK_SCSI_PQUAL_CONNECTED;
   response_data.header.pageCode = SCSI_INQUIRY_80H;
   response_data.header.payloadLen = sizeof(char) * MAX_SERIAL_NUMBER_LENGTH;

   if (eui64) {
      vmk_StringFormat(buffer, sizeof(buffer), NULL,
         "%02X%02X_%02X%02X_%02X%02X_%02X%02X",
         bytes[7], bytes[6], bytes[5], bytes[4],
         bytes[3], bytes[2], bytes[1], bytes[0]);
      /**
       * PR 642515:
       * vmk_StringFormat does not translate %X to upper case correctly.
       */
      OsLib_StrToUpper(buffer, sizeof(buffer));

      DPRINT_NS("Generated serial number string: %s.", buffer);
      vmk_Memcpy(response_data.serialNumber, buffer, MAX_SERIAL_NUMBER_LENGTH);
   } else {

      /**
       * Samsung pre-production device ID hack
       *
       * Samsung pre-production devices does not provide EUI64 per namespace,
       * and all pre-production devices share the same serial number. This hack
       * disables serial number report on such devices to prevent device ID
       * collisions.
       */
      if (VMK_UNLIKELY(ctrlr->pcieVID == PCIE_VID_SAMSUNG) &&
          vmk_Strncmp(ctrlr->serial, SAMSUNG_PRE_PROD_SERIAL,
                      sizeof(SAMSUNG_PRE_PROD_SERIAL) - 1) == 0) {
         EPRINT("Samsung pre-production controller detected, "
                "skip SCSI INQUIRY VPD 80.");
         return NVME_STATUS_INVALID_FIELD_IN_CDB;
      }

      vmk_Memcpy(response_data.serialNumber, ctrlr->serial,
                 sizeof(response_data.serialNumber));
      DPRINT_NS("Serial number string: %s.", ctrlr->serial);
   }

   transferLen = (transferLen < sizeof(response_data)) ? transferLen : sizeof(response_data);

   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, transferLen);
   vmkCmd->bytesXferred = transferLen;

   return NVME_STATUS_SUCCESS;
}


/**
 * T10 Vendor Identification, defined by
 * "NVM-Express-SCSI-Translation-Reference-1_1-Gold.pdf"
 */
#define T10_VENDOR_ID            ("NVMe    ")


/**
 * T10 Vendor ID based ID: Vendor Specific Data, using serial number
 */
typedef struct {
   /** T10 VENDOR IDENTIFICATION, should be 'NVMe    ' */
   vmk_uint8            vendor[8];
   /** Model string from Identify Controller */
   vmk_uint8            model[40];
   /** Serial Number string from Identify Controller */
   vmk_uint8            serial[20];
   /** 32-bit Namespace ID in hex */
   vmk_uint8            namespace[8];
} nvme_ScsiT10IdSerial;


/**
 * T10 Vendor ID based ID: Vendor Specific Data, using EUI64
 */
typedef struct {
   /** T10 VENDOR IDENTIFICATION, should be 'NVMe    ' */
   vmk_uint8            vendor[8];
   /**
    * Product ID, defined by NVM-Express-SCSI-Translation-Reference-1_1-Gold
    *
    * Note: the SCSI translation spec suggests we use the first 16 bytes in the
    * Model string for product ID, however, we would prefer taking the whole
    * 40 bytes to make sure that they are unique.
    */
   vmk_uint8            productId[40];
   /**
    * EUI64 in hex, defined by
    * NVM-Express-SCSI-Translation-Reference-1_1-Gold
    */
   vmk_uint8            eui64[16];
} nvme_ScsiT10IdEui64;


/**
 * SCSI Inquiry VPD83 T10 vendor ID based designator format
 *
 * spc4r36e, 7.8.6.4
 */
typedef struct {
   /** Inquiry VPD 83 page response header */
   vmk_ScsiInquiryVPD83Response  header;
   /** Inquiry VPD 83 page device designation descriptor list*/
   vmk_ScsiInquiryVPD83IdDesc    idDesc;
   /** T10 Vendor ID DESIGNATOR field format */
   union {
      /** If using serial number plus namespace ID format */
      nvme_ScsiT10IdSerial       serial;
      /** If using EUI64 format */
      nvme_ScsiT10IdEui64        eui64;
   };
} VMK_ATTRIBUTE_PACKED nvme_ScsiInquiryVpd83Response;


/**
 * Generate SCSI T10 Vendor ID based on Model, Serial Number, Namespace ID, and
 * EUI64
 */
static Nvme_Status
ScsiGenerateT10VPD(nvme_ScsiInquiryVpd83Response *resp,
                   struct NvmeCtrlr *ctrlr, struct NvmeNsInfo *ns,
                   vmk_ByteCount *length)
{
   resp->header.devclass = VMK_SCSI_CLASS_DISK;
   resp->header.pqual = VMK_SCSI_PQUAL_CONNECTED;
   resp->header.pageCode = SCSI_INQUIRY_83H;
   resp->header.payloadLen = 0; /* Need to recalculate later */

   resp->idDesc.protocolId = 0;
   resp->idDesc.codeSet = 2;
   resp->idDesc.piv = 0;
   resp->idDesc.idType = VMK_SCSI_EVPD83_ID_T10;

   if (ns->eui64) {
      /**
       * According to NVM-Express-SCSI-Translation-Reference-1_1-Gold, Section
       * 6.1.4.3, T10 Vendor ID Based Descriptor should be generated by the
       * following rules:
       *
       *    T10 VENDOR IDENTIFICATION:
       *       'NVMe    '
       *    VENDOR SPECIFIC IDENTIFIER:
       *       Shall be set the concatenation of the translation of PRODUCT
       *       IDENTIFICATION field from standard INQUIRY data as specified in
       *       3.9, and the IEEE Extended Unique Identifier (EUI64) field of the
       *       Identify Namespace Data Structure (Note: EUI64 is in the process
       *       of being added and is not present in the NVM Express 1.0c
       *       specification).
       */

      /**
       * A temporary buffer to hold EUI64 identifier. It's a string in hex of
       * the 64-bit EUI64 field in the IDENTIFY NAMESPACE data, with a nul
       * terminator.
       */
      char eui64Id[17];

      vmk_Memcpy(resp->eui64.vendor, T10_VENDOR_ID,
                 sizeof(resp->eui64.vendor));

      vmk_Memcpy(resp->eui64.productId, ctrlr->model,
                 sizeof(resp->eui64.productId));

      /**
       * Generate the string representation of the EUI64.
       */
      vmk_StringFormat(eui64Id, sizeof(eui64Id), NULL, "%016lX", ns->eui64);
      /**
       * PR 642515:
       * vmk_StringFormat does not translate %X to upper case correctly.
       */
      OsLib_StrToUpper(eui64Id, sizeof(eui64Id));
      vmk_Memcpy(resp->eui64.eui64, eui64Id, sizeof(resp->eui64.eui64));

      *length = sizeof(vmk_ScsiInquiryVPD83Response) +
                sizeof(vmk_ScsiInquiryVPD83IdDesc) +
                sizeof(nvme_ScsiT10IdEui64);

      resp->header.payloadLen =
         vmk_CPUToBE16(sizeof(vmk_ScsiInquiryVPD83IdDesc) +
                       sizeof(nvme_ScsiT10IdEui64));
      resp->idDesc.idLen = sizeof(nvme_ScsiT10IdEui64);

      return NVME_STATUS_SUCCESS;

   } else {
      /**
       * If device doesn't report valid per-namespace EUI64 field, then generate
       * the T10 Vendor Specific Data using the combination of Model (40),
       * Serial Number (20), and Namespace ID (8).
       */

      /**
       * A temporary buffer to hold namespace identifier. Namespace identifier
       * is a string in hex of the 32-bit namespace ID, with a nul terminator.
       */
      char nsId[9];

      /**
       * Samsung pre-production device ID hack
       *
       * Samsung pre-production devices does not provide EUI64 per namespace,
       * and all pre-production devices share the same serial number. This hack
       * disables serial number report on such devices to prevent device ID
       * collisions.
       */
      if (VMK_UNLIKELY(ctrlr->pcieVID == PCIE_VID_SAMSUNG) &&
          vmk_Strncmp(ctrlr->serial, SAMSUNG_PRE_PROD_SERIAL,
                      sizeof(SAMSUNG_PRE_PROD_SERIAL) - 1) == 0) {
         EPRINT("Samsung pre-production controller detected, "
                "skip SCSI INQUIRY VPD 83.");
         return NVME_STATUS_INVALID_FIELD_IN_CDB;
      }

      vmk_Memcpy(resp->serial.vendor, T10_VENDOR_ID,
                 sizeof(resp->serial.vendor));

      vmk_Memcpy(resp->serial.model, ctrlr->model,
                 sizeof(resp->serial.model));

      vmk_Memcpy(resp->serial.serial, ctrlr->serial,
                 sizeof(resp->serial.serial));

      /**
       * Generate the string representation of the namespace ID
       */
      vmk_StringFormat(nsId, sizeof(nsId), NULL, "%08X", ns->id);
      /**
       * PR 642515:
       * vmk_StringFormat does not translate %X to upper case correctly.
       */
      OsLib_StrToUpper(nsId, sizeof(nsId));
      vmk_Memcpy(resp->serial.namespace, nsId,
                 sizeof(resp->serial.namespace));

      *length = sizeof(vmk_ScsiInquiryVPD83Response) +
                sizeof(vmk_ScsiInquiryVPD83IdDesc) +
                sizeof(nvme_ScsiT10IdSerial);

      resp->header.payloadLen =
         vmk_CPUToBE16(sizeof(vmk_ScsiInquiryVPD83IdDesc) +
                       sizeof(nvme_ScsiT10IdSerial));

      resp->idDesc.idLen = sizeof(nvme_ScsiT10IdSerial);

      return NVME_STATUS_SUCCESS;
   }
}


/**
 * Handle SCSI Inquiry Device Identification VPD page command
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 */
static Nvme_Status
NvmeScsiCmd_DoInquiryVpd83(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd, struct NvmeNsInfo *ns)
{
   Nvme_Status                   nvmeStatus = NVME_STATUS_SUCCESS;
   nvme_ScsiInquiryVpd83Response response_data;
   vmk_ByteCount                 length = 0;
   vmk_ScsiInquiryCmd              *inquiryCmd     = (vmk_ScsiInquiryCmd *)vmkCmd->cdb;
   vmk_uint16                       transferLen    = 0;

   transferLen = _lto2b_16(inquiryCmd->length);

   vmk_Memset(&response_data, 0, sizeof(response_data));

   nvmeStatus = ScsiGenerateT10VPD(&response_data, ctrlr, ns, &length);

   if (SUCCEEDED(nvmeStatus)) {
      transferLen = (transferLen < length) ? transferLen : length;

      vmk_SgCopyTo(vmkCmd->sgArray, &response_data, transferLen);
      vmkCmd->bytesXferred = transferLen;
   } else {
      vmkCmd->bytesXferred = 0;
   }

   return nvmeStatus;
}


#endif /* NVME_ENABLE_SCSI_DEVICEID */


/**
 * Handle SCSI Inquiry Extended INQUIRY Data VPD page command
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 */
static Nvme_Status
NvmeScsiCmd_DoInquiryVpd86(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd, struct NvmeNsInfo *ns)
{
   return NVME_STATUS_INVALID_FIELD_IN_CDB;
}


/**
 * SCSI Inquiry Block Limits VPD page response data
 * sbc3r35, table 209
 */
typedef struct {
   /** Block Limits VPD page header */
   struct {
      /** PERIPHERAL DEVICE TYPE (VMK_SCSI_CLASS_DISK) */
      vmk_uint8   devclass    : 5;
      /** PERIPHERAL QUALIFIER (VMK_SCSI_PQUAL_CONNECTED) */
      vmk_uint8   pqual       : 3;
      /** PAGE CODE (SCSI_INQUIRY_B0H) */
      vmk_uint8   pagecode;
      /** PAGE LENGTH (003Ch) */
      vmk_uint16  payloadLen;
   } VMK_ATTRIBUTE_PACKED header;
   struct {
      /** WSNZ (write same no zero) */
      vmk_uint8   wsnz        : 1;
      /** Reserved */
      vmk_uint8   reserved    : 7;
      /** MAXIMUM COMPARE AND WRITE LENGTH */
      vmk_uint8   maxCompareWriteLen;
      /** OPTIMAL TRANSFER LENGTH GRANULARITY */
      vmk_uint16  optimalXferLenGranularity;
      /** MAXIMUM TRANSFER LENGTH */
      vmk_uint32  maxXferLen;
      /** OPTIMAL TRANSFER LENGTH */
      vmk_uint32  optimalXferLen;
      /** MAXIMUM PREFETCH LENGTH */
      vmk_uint32  maxPrefetchLen;
      /** MAXIMUM UNMAP LBA COUNT */
      vmk_uint32  maxUnmabLbaCount;
      /** MAXIMUM UNMAP BLOCK DESCRIPTOR COUNT */
      vmk_uint32  maxUnmapBlockDescriptorCount;
      /** OPTIMAL UNMAP GRANULARITY */
      vmk_uint32  optimalUnmapGranularity;
      /** UNMAP GRANULARITY ALIGNMENT */
      vmk_uint32  unmapGranularityAlign    : 31;
      /** UGAVALID */
      vmk_uint32  ugaValid                 : 1;
      /** MAXIMUM WRITE SAME LENGTH */
      vmk_uint64  maxWriteSameLen;
      /** Reserved (2) */
      vmk_uint8   reserved2[20];
   } VMK_ATTRIBUTE_PACKED payload;
} VMK_ATTRIBUTE_PACKED nvme_ScsiInquiryVpdB0Response;


/**
 * Handle SCSI Inquiry Block Limits VPD page command
 *
 * @param [in] ctrlr pointer to the controller instance
 * @param [in] vmkCmd pointer to the scsi command
 * @param [in] ns pointer to the namespace
 */
static Nvme_Status
NvmeScsiCmd_DoInquiryVpdB0(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd, struct NvmeNsInfo *ns)
{
   nvme_ScsiInquiryVpdB0Response response_data;
   vmk_ScsiInquiryCmd              *inquiryCmd     = (vmk_ScsiInquiryCmd *)vmkCmd->cdb;
   vmk_uint16                       transferLen    = 0;

   transferLen = _lto2b_16(inquiryCmd->length);

   VMK_ASSERT_ON_COMPILE(sizeof(response_data.payload) == 0x3C);

   vmk_Memset(&response_data, 0, sizeof(response_data));

   response_data.header.devclass = VMK_SCSI_CLASS_DISK;
   response_data.header.pqual = VMK_SCSI_PQUAL_CONNECTED;
   response_data.header.pagecode = SCSI_INQUIRY_B0H;
   response_data.header.payloadLen = vmk_CPUToBE16(sizeof(response_data.payload));

   response_data.payload.maxUnmabLbaCount = vmk_CPUToBE32((vmk_uint32) - 1);

   transferLen = (transferLen < sizeof(response_data)) ? transferLen : sizeof(response_data);

   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, transferLen);
   vmkCmd->bytesXferred = transferLen;

   return NVME_STATUS_SUCCESS;
}


/**
 * SCSI Inquiry Block Device Characteristics VPD page response data
 *
 * sbc3r35, table 203
 */
typedef struct {
   /** Block Device Characteristics VPD page header */
   struct {
      /** PERIPHERAL DEVICE TYPE (VMK_SCSI_CLASS_DISK) */
      vmk_uint8   devclass   : 5;
      /** PERIPHERAL QUALIFIER (VMK_SCSI_PQUAL_CONNECTED) */
      vmk_uint8   pqual      : 3;
      /** PAGE CODE (SCSI_INQUIRY_B1H) */
      vmk_uint8   pagecode;
      /** PAGE LENGTH (003Ch) */
      vmk_uint16  payloadLen;
   } VMK_ATTRIBUTE_PACKED header;
   struct {
      /** MEDIUM ROTATION RATE (01h) */
      vmk_uint16  rotationRate;
      vmk_uint8   reserved1;
      /** NOMINAL FORM FACTOR */
      vmk_uint8   formFactor : 4;
      vmk_uint8   reserved2  : 4;
      vmk_uint8   reserved3[56];
   } VMK_ATTRIBUTE_PACKED payload;
} VMK_ATTRIBUTE_PACKED nvme_ScsiInquiryVpdB1Response;


/**
 * Handle SCSI Inquiry Block Device Characteristics VPD page command
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 */
static Nvme_Status
NvmeScsiCmd_DoInquiryVpdB1(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd, struct NvmeNsInfo *ns)
{
   nvme_ScsiInquiryVpdB1Response response_data;
   vmk_ScsiInquiryCmd              *inquiryCmd     = (vmk_ScsiInquiryCmd *)vmkCmd->cdb;
   vmk_uint16                       transferLen    = 0;

   transferLen = _lto2b_16(inquiryCmd->length);

   VMK_ASSERT_ON_COMPILE(sizeof(response_data.payload) == 0x3C);

   vmk_Memset(&response_data, 0, sizeof(response_data));

   response_data.header.devclass = VMK_SCSI_CLASS_DISK;
   response_data.header.pqual = VMK_SCSI_PQUAL_CONNECTED;
   response_data.header.pagecode = SCSI_INQUIRY_B1H;
   response_data.header.payloadLen = sizeof(response_data.payload);

   response_data.payload.rotationRate = vmk_CPUToBE16(0x1); /* is SSD */
   response_data.payload.formFactor = 0; /* form factor not reported */

   transferLen = (transferLen < sizeof(response_data)) ? transferLen : sizeof(response_data);

   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, transferLen);
   vmkCmd->bytesXferred = transferLen;

   return NVME_STATUS_SUCCESS;
}

#define MIN_TX_LEN_FOR_STD_INQUIRY  5
#define MIN_TX_LEN_FOR_EVPD_PAGES   4

/**
 * Handle SCSI Inquiry command
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 */
static Nvme_Status
NvmeScsiCmd_DoInquiry(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd, struct NvmeNsInfo *ns)
{
   Nvme_Status         nvmeStatus;
   vmk_ScsiInquiryCmd *inquiryCmd = (vmk_ScsiInquiryCmd *)vmkCmd->cdb;
   vmk_uint16          transferLen  = 0;

   transferLen = _lto2b_16(inquiryCmd->length);

   // Checking the inquiry command cdb. Returning failure for the below cases:
   // 1. When the reserved fields are non zero.
   // 2. When the cmddt(command support data) is non zero.
   // 3. When the lun is non zero.
   // 4. When control is non zero.
   if (inquiryCmd->cmddt != 0 ||
       inquiryCmd->resv12 != 0 ||
       //inquiryCmd->lun != 0 ||
       inquiryCmd->ctrl != 0) {
      nvmeStatus = NVME_STATUS_INVALID_FIELD_IN_CDB;
   } else if (inquiryCmd->evpd) {
      // As per spc3 r23 if the EVPD bit is set to one the Allocation length
      // has to be atleast of 4 bytes.
      if (transferLen < MIN_TX_LEN_FOR_EVPD_PAGES) {
         nvmeStatus = NVME_STATUS_INVALID_FIELD_IN_CDB;
      } else {
      switch(inquiryCmd->pagecode) {
         case SCSI_INQUIRY_00H:
            nvmeStatus = NvmeScsiCmd_DoInquiryVpd00(ctrlr, vmkCmd, ns);
            break;
#if NVME_ENABLE_SCSI_DEVICEID
         case SCSI_INQUIRY_80H:
            nvmeStatus = NvmeScsiCmd_DoInquiryVpd80(ctrlr, vmkCmd, ns);
            break;
         case SCSI_INQUIRY_83H:
            nvmeStatus = NvmeScsiCmd_DoInquiryVpd83(ctrlr, vmkCmd, ns);
            break;
#endif
         case SCSI_INQUIRY_86H:
            nvmeStatus = NvmeScsiCmd_DoInquiryVpd86(ctrlr, vmkCmd, ns);
            break;
         case SCSI_INQUIRY_B0H:
            nvmeStatus = NvmeScsiCmd_DoInquiryVpdB0(ctrlr, vmkCmd, ns);
            break;
         case SCSI_INQUIRY_B1H:
            nvmeStatus = NvmeScsiCmd_DoInquiryVpdB1(ctrlr, vmkCmd, ns);
            break;
         default:
            nvmeStatus = NVME_STATUS_INVALID_FIELD_IN_CDB;
            break;
      }
      }
   } else {
      // As per spc3 r23 if the EVPD bit is set to zero, the Page code has to
      // be zero and the Allocation length has to be atleast of 5 bytes.
      if(inquiryCmd->pagecode != 0 ||
         transferLen < MIN_TX_LEN_FOR_STD_INQUIRY) {
         nvmeStatus = NVME_STATUS_INVALID_FIELD_IN_CDB;
   } else {
      nvmeStatus = NvmeScsiCmd_DoInquiryStd(ctrlr, vmkCmd, ns);
   }
   }

   return nvmeStatus;
}


/**
 * Handle SCSI Read/Write command
 *
 * We handle READ(6), READ(10), READ(12), READ(16), WRITE(6), WRITE(10),
 * WRITE(12), WRITE(16) in a single function.
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 */
static Nvme_Status
NvmeScsiCmd_DoIO(struct NvmeCtrlr *ctrlr, void *cmdPtr, struct NvmeNsInfo *ns)
{
   vmk_ScsiCommand *vmkCmd;

   GET_VMK_SCSI_CMD(cmdPtr, vmkCmd);

   if (vmkCmd->lba + vmkCmd->lbc > ns->blockCount) {
      return NVME_STATUS_LBA_OUT_OF_RANGE;
   }  else if (vmkCmd->lbc == 0) {
      // As per SCSI spec valid LBA and transfer length of 0 is not an error.
      // So returning success from here.
      return NVME_STATUS_SUCCESS;
   }

   return NvmeIo_SubmitIo(ns, cmdPtr);
}


/**
 * Handle SCSI Read Capacity (10) command
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 */
static Nvme_Status
NvmeScsiCmd_DoReadCapacity(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd, struct NvmeNsInfo *ns)
{
   vmk_ScsiReadCapacityResponse response_data;
   vmk_ScsiReadCap10Command     *cmd            = (vmk_ScsiReadCap10Command *)vmkCmd->cdb;

   // Checking all Read Capacity and returning failure when
   // 1. Reserved and obsolute fields set with non zero
   // 2. LBA set with non zero
   // 3. PMI set with non zero
   // 4. Control byte is non zero.
   if (cmd->obs != 0 || cmd->resv1 != 0 || cmd->lba != 0 ||
       cmd->resv2 != 0 || cmd->resv3 != 0 || cmd->pmi != 0 ||
       cmd->resv4 != 0 || cmd->control != 0) {
      return NVME_STATUS_INVALID_FIELD_IN_CDB;
   }

   vmk_Memset(&response_data, 0, sizeof(response_data));

   response_data.lbn = ns->blockCount > VMK_UINT32_MAX ? VMK_UINT32_MAX : (vmk_CPUToBE32(ns->blockCount - 1));
   response_data.blocksize = vmk_CPUToBE32(1 << ns->lbaShift);

   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, sizeof(response_data));
   vmkCmd->bytesXferred = sizeof(response_data);

   return NVME_STATUS_SUCCESS;
}


/**
 * SCSI READ CAPACITY (16) parameter data
 *
 * sbc3r35, table 66
 */
typedef struct {
   /** RETURNED LOGICAL BLOCK ADDRESS */
   vmk_uint64  lbn;
   /** LOGICAL BLOCK LENGTH IN BYTES */
   vmk_uint32  blocksize;
   /** protection enable */
   vmk_uint8   protEnable  :1,
   /** protection type */
               protType    :3,
               reserved1   :4;
   /** LOGICAL BLOCKS PER PHYSICAL BLOCK EXPONENT */
   vmk_uint8   logicalBlockPerPhysicalBlockExponent   :4,
   /** P_I_EXPONENT */
               PIExponent  :4;
   vmk_uint8  lowestAlignedLba_msb   :6,
   /** logical block provisioning read zeros */
               lbprz       :1,
   /** logical block provisioning management enabled */
               lbpme       :1;
   vmk_uint8  lowestAlignedLba_lsb;
   vmk_uint8   reserved2[16];
} VMK_ATTRIBUTE_PACKED nvme_ScsiReadCapacity16Response;


/**
 * Handle SCSI Read Capacity (16) command
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 */
static Nvme_Status
NvmeScsiCmd_DoReadCapacity16(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd, struct NvmeNsInfo *ns)
{
   vmk_ScsiReadCap16Command        *cmd            = (vmk_ScsiReadCap16Command *)vmkCmd->cdb;
   vmk_uint32                       transferLen    = _lto2b_32(cmd->len);
   nvme_ScsiReadCapacity16Response response_data;

   // Validating the service action cdb and returning failure when
   // 1. Reserved field are non zero
   // 2. Service action is other than 0x10 (Read capacity 16 service action)
   // 3. Logical block address is non zero
   // 4. Partial Medium Indicator is non zero
   // 5. Control byte is non zero.
   if (cmd->sa != VMK_SCSI_SAI_READ_CAPACITY16 ||
       cmd->resv1 != 0 || cmd->lba != 0 ||
       cmd->resv2 != 0 || cmd->pmi != 0 ||
       cmd->control != 0) {
      return NVME_STATUS_INVALID_FIELD_IN_CDB;
   }

   transferLen = (transferLen < sizeof(response_data)) ? transferLen : sizeof(response_data);
   if (transferLen == 0) {
      return NVME_STATUS_SUCCESS;
   }

   vmk_Memset(&response_data, 0, sizeof(response_data));

   response_data.lbn = vmk_CPUToBE64(ns->blockCount - 1);
   response_data.blocksize = vmk_CPUToBE32(1 << ns->lbaShift);

   /**
    * Currently, namespace could be formated into two kinds of logical block size:
    * 512B & 4KB. 
    * Driver doesn't know the physical block size of NVMe device. Here we assume the
    * physical block size is 4KB.
    */
   if (ns->lbaShift <= 12) {
      response_data.logicalBlockPerPhysicalBlockExponent = 12 - ns->lbaShift;
   } else {
      response_data.logicalBlockPerPhysicalBlockExponent = 0;
   }

   DPRINT_NS("ns: %d, blockCount: %ld, lbaShift: %d, fmtLbaSize: %d, metaDataCap: %d, "
           "dataProtCap: %d, dataProtSet: %d, metasize: %d.",
           ns->id, ns->blockCount, ns->lbaShift,
           ns->fmtLbaSize, ns->metaDataCap, ns->dataProtCap, ns->dataProtSet, ns->metasize);

   response_data.protEnable = END2END_DSP_TYPE(ns->dataProtSet) == 0 ? 0 : 1;
   /* 000b -> unspecified; 001b -> 000b; 010b -> 001b; 011b -> 010b */
   response_data.protType = END2END_DSP_TYPE(ns->dataProtSet) - 1;
   /**
    * Note: we require lbpme to be set to 1 when device supports UNMAP/DSM.
    */
   response_data.lbpme = (ctrlr->nvmCmdSupport & 0x4) ? 1 : 0;
   response_data.lbprz = 0;

   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, transferLen);
   vmkCmd->bytesXferred = transferLen;

   return NVME_STATUS_SUCCESS;
}


/**
 * Mode parameter header(6)
 *
 * spc4r36e, table 452
 */
typedef struct {
   /** MODE DATA LENGTH */
   vmk_uint8 dataLen;
   /** MEDIUM TYPE */
   vmk_uint8 mediumType;
   /** DEVICE-SPECIFIC PARAMETER */
   vmk_uint8 reserved1:4;
   vmk_uint8 dpoFua:1;
   vmk_uint8 reserved2:2;
   vmk_uint8 wp:1;
   /** BLOCK DESCRIPTOR LENGTH */
   vmk_uint8 blockDescriptorLen;
} VMK_ATTRIBUTE_PACKED nvme_ScsiModeSenseHeader6;

/**
 * Mode parameter header(10)
 *
 * spc3r23, table 240
 */
typedef struct {
   /** MODE DATA LENGTH */
   vmk_uint8 dataLen[2];
   /** MEDIUM TYPE */
   vmk_uint8 mediumType;
   /** DEVICE-SPECIFIC PARAMETER */
   vmk_uint8 reserved1:4;
   vmk_uint8 dpoFua:1;
   vmk_uint8 reserved2:2;
   vmk_uint8 wp:1;
   /** LONG LBA */
   vmk_uint8 longLba:1;
   vmk_uint8 reserved3:7;
   vmk_uint8 reserved4;
   /** BLOCK DESCRIPTOR LENGTH */
   vmk_uint8 blockDescriptorLen[2];
} VMK_ATTRIBUTE_PACKED nvme_ScsiModeSenseHeader8;

/**
 * Caching mode page
 *
 * sbc3r35, table 187
 */
typedef struct {
   /** PAGE CODE (08h) */
   vmk_uint8  pageCode  : 6;
   /** SPF (0b) */
   vmk_uint8  spf       : 1;
   /** PS */
   vmk_uint8  ps        : 1;
   /** PAGE LENGTH (12h) */
   vmk_uint8  pageLen;
   /** RCD */
   vmk_uint8  rcd       : 1;
   /** MF */
   vmk_uint8  mf        : 1;
   /** WCE */
   vmk_uint8  wce       : 1;
   /** SIZE */
   vmk_uint8  size      : 1;
   /** DISC */
   vmk_uint8  disc      : 1;
   /** CAP */
   vmk_uint8  cap       : 1;
   /** ABPF */
   vmk_uint8  abpf      : 1;
   /** IC */
   vmk_uint8  ic        : 1;
   /** WRITE RETENTION PRIORITY */
   vmk_uint8  writeRententionPriority   : 4;
   /** DEMAND READ RETENTION PRIORITY */
   vmk_uint8  readRententionPriority    : 4;
   /** DISABLE PRE-FETCH TRANSFER LENGTH */
   vmk_uint16 disablePrefetchXferLen;
   /** MINIMUM PRE-FETCH */
   vmk_uint16 minPrefetch;
   /** MAXIMUM PRE-FETCH */
   vmk_uint16 maxPrefetch;
   /** MAXIMUM PRE-FETCH CEILING */
   vmk_uint16 maxPrefetchCeil;
   /** NV_DIS */
   vmk_uint8  nvDis     : 1;
   /** SYNC_PROG */
   vmk_uint8  syncProg  : 2;
   /** Vendor Specific */
   vmk_uint8  vendor    : 2;
   /** DRA */
   vmk_uint8  dra       : 1;
   /** LBCSS */
   vmk_uint8  lbcss     : 1;
   /** FSW */
   vmk_uint8  fsw       : 1;
   /** NUMBER OF CACHE SEGMENTS */
   vmk_uint8  numCacheSegs;
   /** CACHE SEGMENT SIZE */
   vmk_uint16 cacheSegSize;
   /** reserved */
   vmk_uint8  reserved;
   /** Obsolete */
   vmk_uint8  obsolate[3];
} VMK_ATTRIBUTE_PACKED nvme_ScsiModeSenseCachingPage;


/**
 * Control mode page
 *
 * spc4r36e, table 457
 */
typedef struct {
   /** PAGE CODE (0Ah) */
   vmk_uint8 pageCode : 6;    /* Byte 1 */
   /** SPF (0b) */
   vmk_uint8 spf      : 1;
   /** PS */
   vmk_uint8 ps       : 1;
   /** PAGE LENGTH (0Ah) */
   vmk_uint8 pageLen;         /* Byte 2 */
   /** RELC */
   vmk_uint8 relc     : 1;    /* Byte 3 */
   /** GLTSD */
   vmk_uint8 gltsd    : 1;
   /** D_SENSE */
   vmk_uint8 dSense   : 1;
   /** DPICZ */
   vmk_uint8 dpicz    : 1;
   /** TMF_ONLY */
   vmk_uint8 tmfOnly  : 1;
   /** TST */
   vmk_uint8 tst      : 3;
   /** Obsolete */
   vmk_uint8 obsolate : 1;    /* Byte 4 */
   /** QERR */
   vmk_uint8 qerr     : 2;
   /** NUAR */
   vmk_uint8 nuar     : 1;
   /** QUEUE ALGORITHM MODIFIER */
   vmk_uint8 qam      : 4;
   /** Obsolete */
   vmk_uint8 obsolate2 : 3;   /* Byte 5 */
   /** SWP */
   vmk_uint8 swp      : 1;
   /** UA_INTLCK_CTRL */
   vmk_uint8 uic      : 2;
   /** RAC */
   vmk_uint8 rac      : 1;
   /** VS */
   vmk_uint8 vs       : 1;
   /** AUTOLOAD MODE */
   vmk_uint8 alMode   : 3;    /* Byte 6 */
   /** Reserved */
   vmk_uint8 reserved : 1;
   /** RWWP */
   vmk_uint8 rwwp     : 1;
   /** ATMPE */
   vmk_uint8 atmpe    : 1;
   /** TAS */
   vmk_uint8 tas      : 1;
   /** ATO */
   vmk_uint8 ato      : 1;
   /** Obsolete */
   vmk_uint8 obsolate3[2];                         /* Byte 7~8 */
   /** BUSY TIMEOUT PERIOD */
   vmk_uint16 busyTimeoutPeriod;                   /* Byte 9~10 */
   /** EXTENDED SELF-TEST COMPLETION TIME */
   vmk_uint16 extSelfTestCompTime;                 /* Byte 11-12 */
} VMK_ATTRIBUTE_PACKED nvme_ScsiModeSenseControlPage;


/**
 * Power Condition mode page
 *
 * spc4r36e, table 468
 */
typedef struct {
  /** PAGE CODE (1Ah) */
   vmk_uint8  pageCode  : 6;    /* Byte 1 */
   /** SPF (0b) */
   vmk_uint8  spf       : 1;
   /** PS */
   vmk_uint8  ps        : 1;
   /** PAGE LENGTH (26h) */
   vmk_uint8  pageLen;         /* Byte 2 */
   /** STANDBY_Y */
   vmk_uint8  standbyY  : 1;   /* Byte 3 */
   /** Reserved */
   vmk_uint8  reserved1 : 5;
   /** PM_BG_PRECEDENCE */
   vmk_uint8  pbp       : 2;
   /** STANDBY_Z */
   vmk_uint8  standbyZ  : 1;   /* Byte 4 */
   /** IDLE_A */
   vmk_uint8  idleA     : 1;
   /** IDLE_B */
   vmk_uint8  idleB     : 1;
   /** IDLE_C */
   vmk_uint8  idleC     : 1;
   /** Reserved */
   vmk_uint8  reserved2 : 4;
   /** IDLE_A CONDITION TIMER */
   vmk_uint32 idleACT;        /* Byte 5~8 */
   /** STANDBY_Z CONDITION TIMER */
   vmk_uint32 standbyZCT;     /* Byte 9~12 */
   /** IDLE_B CONDITION TIMER */
   vmk_uint32 idleBCT;        /* Byte 13~16*/
   /** IDLE_C CONDITION TIMER */
   vmk_uint32 idleCCT;        /* Byte 17~20 */
   /** STANDBY_Y CONDITION TIMER */
   vmk_uint32 standbyYCT;     /* Byte 21-24 */
   /** Reserved */
   vmk_uint8  reserved3[15];  /* Byte 25~39 */
   /** Reserved */
   vmk_uint8  reserved4  : 2; /* Byte 40 */
   /** CCF STOPPED */
   vmk_uint8  ccfStop    : 2;
   /** CCF STANDBY */
   vmk_uint8  ccfStandby : 2;
   /** CCF IDLE */
   vmk_uint8  ccfIdle    : 2;
} VMK_ATTRIBUTE_PACKED nvme_ScsiModeSensePCPage;


typedef struct {
  /** PAGE CODE (01h) */
   vmk_uint8  pageCode  : 6;    /* Byte 1 */
   /** SPF (0b) */
   vmk_uint8  spf       : 1;
   /** PS */
   vmk_uint8  ps        : 1;
   /** PAGE LENGTH (0Ah) */
   vmk_uint8  pageLen;         /* Byte 2 */
   /** DCR */
   vmk_uint8  dcr       : 1;   /* Byte 3 */
   /** DTE */
   vmk_uint8  dte       : 1;
   /** PER */
   vmk_uint8  per       : 1;
   /** EER */
   vmk_uint8  eer       : 1;
   /** RC */
   vmk_uint8  rc        : 1;
   /** TB */
   vmk_uint8  tb        : 1;
   /** ARRE */
   vmk_uint8  arre      : 1;
   /** AWRE */
   vmk_uint8  awre      : 1;
   /** READ RETRY COUNT */
   vmk_uint8  readRetry;       /* Byte 4 */
   /** Reserved */
   vmk_uint8  reserved1[3];    /* Byte 5~7 */
   /** RESTRICTED FOR MMC */
   vmk_uint8  mmc6      : 2;   /* Byte 8 */
   /** Reserved */
   vmk_uint8  reserved2 : 5;
   /** TPERE */
   vmk_uint8  tpere     : 1;
   /** WRITE RETRY COUNT */
   vmk_uint8  writeRetry;      /* Byte 9 */
   /** Reserved */
   vmk_uint8  reserved3;       /* Byte 10 */
   /** RECOVERY TIME LIMIT */
   vmk_uint8  recoveryTime[2]; /* Byte 11~12 */
} VMK_ATTRIBUTE_PACKED nvme_ScsiModeSenseRWErrorRecoveryPage;


/**
 * Informational Exceptions Control mode page
 *
 * spc3 r23, Table 256
 */
typedef struct {
   /** PAGE CODE (1Ch) */
   vmk_uint8 pageCode   : 6;    /* Byte 0 */
   /** SPF */
   vmk_uint8 spf        : 1;
   /** PS */
   vmk_uint8 ps         : 1;
   /** PAGE LENGTH (0Ah) */
   vmk_uint8 pageLen;           /* Byte 1 */
   /** LOGERR */
   vmk_uint8 logErr     : 1;    /* Byte 2 */
   /** Reserved, now used as EBACKERR */
   vmk_uint8 ebackerr   : 1;
   /** TEST */
   vmk_uint8 test       : 1;
   /** DEXCPT*/
   vmk_uint8 dexcpt     : 1;
   /** EWASC */
   vmk_uint8 ewasc      : 1;
   /** EBF */
   vmk_uint8 ebf        : 1;
   /** Reserved */
   vmk_uint8 reseved2   : 1;
   /** PERF */
   vmk_uint8 perf       : 1;
   /** MRIE */
   vmk_uint8 mrie       : 4;    /* Byte 3 */
   /** Reserved */
   vmk_uint8 reserved3  : 4;
   /** INTERVAL TIMER*/
   vmk_uint8 intervalTimer[4];    /* Byte 4,5,6,7 */
   /** REPORT COUNT */
   vmk_uint8 reportCount[4];      /* Byte 8,9,10,11 */
} VMK_ATTRIBUTE_PACKED nvme_ScsiModeSenseIEPage;



/** Caching mode page code */
#define NVME_SCSI_MS_PAGE_CACHE    (0x08)
/** Control mode page code */
#define NVME_SCSI_MS_PAGE_CONTROL  (0x0A)
/** Power Condition mode page code */
#define NVME_SCSI_MS_PAGE_PC       (0x1A)
/** Read-Write error recovery mode page code */
#define NVME_SCSI_MS_PAGE_RWER     (0x01)
/** Return all pages page code */
#define NVME_SCSI_MS_PAGE_ALL      (0x3F)
/** Informational Exceptions Control mode page code*/
#define NVME_SCSI_MS_IE_CONTROL    (0x1C)


/** Caching mode page size */
#define NVME_SCSI_MS_PAGE_CACHE_SZ     (0x12)
/** Control mode page size */
#define NVME_SCSI_MS_PAGE_CONTROL_SZ   (0x0A)
/** Power Condition mode page size */
#define NVME_SCSI_MS_PAGE_PC_SZ        (0x26)
/** Read-Write error recovery mode page size*/
#define NVME_SCSI_MS_PAGE_RWER_SZ      (0x0A)
/** Retry count for recovery mode page */
#define MAX_COMMAND_ISSUE_RETRIES      (0x03)
/** All mode pages data length */
#define TOTAL_MODE_PAGE_DATA_LEN       sizeof(nvme_ScsiModeSenseCachingPage) + \
                                       sizeof(nvme_ScsiModeSenseControlPage) + \
                                       sizeof(nvme_ScsiModeSensePCPage) + \
                                       sizeof(nvme_ScsiModeSenseRWErrorRecoveryPage)
/** Mode sense 6 command cdb length */
#define MODE_SENSE6_CDB_LEN            (6)
/** Mode sense 10 command cdb length */
#define MODE_SENSE10_CDB_LEN           (10)

typedef struct {
   vmk_Scsi4ByteModeSenseParameterHeader header;
   vmk_uint8                 allModePagesData[TOTAL_MODE_PAGE_DATA_LEN];
} VMK_ATTRIBUTE_PACKED modeParameterList_6B;

typedef struct {
   nvme_ScsiModeSenseHeader8 header;
   vmk_uint8                 allModePagesData[TOTAL_MODE_PAGE_DATA_LEN];
} VMK_ATTRIBUTE_PACKED modeParameterList_8B;

union {
   modeParameterList_6B resp6B;
   modeParameterList_8B resp8B;
} VMK_ATTRIBUTE_PACKED modeParameterListResponseData;

nvme_ScsiModeSenseCachingPage defCacheModePage = {
   .pageCode = NVME_SCSI_MS_PAGE_CACHE,
   .pageLen = NVME_SCSI_MS_PAGE_CACHE_SZ,
};

nvme_ScsiModeSenseControlPage defControlModePage = {
   .pageCode = NVME_SCSI_MS_PAGE_CONTROL,
   .pageLen = NVME_SCSI_MS_PAGE_CONTROL_SZ,
   .dpicz = 1,
   .dSense = 0,
   .gltsd = 1,
   .qam = 1,
   .qerr = 0,
   .tas = 1,
   .busyTimeoutPeriod = 0xffff,
};

nvme_ScsiModeSensePCPage defPcModePage = {
   .pageCode = NVME_SCSI_MS_PAGE_PC,
   .pageLen = NVME_SCSI_MS_PAGE_PC_SZ,
};

nvme_ScsiModeSenseRWErrorRecoveryPage defRwErrorRecoveryModePage = {
   .pageCode = NVME_SCSI_MS_PAGE_RWER,
   .pageLen = NVME_SCSI_MS_PAGE_RWER_SZ,

   .awre = 1,
   .readRetry = MAX_COMMAND_ISSUE_RETRIES,
   .writeRetry = MAX_COMMAND_ISSUE_RETRIES,
};


/**
 * Check at compile time for the data structure size of the cache mode pages
 */
VMK_ASSERT_LIST(modeSenseSizeChecker,
   VMK_ASSERT_ON_COMPILE(sizeof(nvme_ScsiModeSenseCachingPage) == (NVME_SCSI_MS_PAGE_CACHE_SZ + 2));
   VMK_ASSERT_ON_COMPILE(sizeof(nvme_ScsiModeSenseControlPage) == (NVME_SCSI_MS_PAGE_CONTROL_SZ + 2));
   VMK_ASSERT_ON_COMPILE(sizeof(nvme_ScsiModeSensePCPage) == (NVME_SCSI_MS_PAGE_PC_SZ + 2));
   VMK_ASSERT_ON_COMPILE(sizeof(nvme_ScsiModeSenseRWErrorRecoveryPage) == (NVME_SCSI_MS_PAGE_RWER_SZ + 2));
)


/**
 * Handle SCSI Mode Sense Caching page
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 */
static Nvme_Status
NvmeScsiCmd_DoModeSenseCache(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd, struct NvmeNsInfo *ns)
{
   vmk_Scsi4ByteModeSenseParameterHeader     *p6Bheader = NULL;
   nvme_ScsiModeSenseHeader8     *p8Bheader = NULL;
   nvme_ScsiModeSenseCachingPage *caching = NULL;

   vmk_ByteCount transferLen = vmk_SgGetDataLen(vmkCmd->sgArray);
   vmk_ByteCount sizeOfData = 0;

   vmk_Memset(&modeParameterListResponseData, 0, sizeof(modeParameterListResponseData));

   if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE10) {
      p8Bheader = (nvme_ScsiModeSenseHeader8 *)&modeParameterListResponseData;
      caching = (nvme_ScsiModeSenseCachingPage *) &p8Bheader[1];
      if (transferLen < 2) {
         vmkCmd->bytesXferred = 0;
         return NVME_STATUS_SUCCESS;
      }
      sizeOfData = sizeof(defCacheModePage)+sizeof(nvme_ScsiModeSenseHeader8);
   } else if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE) {
      p6Bheader = (vmk_Scsi4ByteModeSenseParameterHeader *)&modeParameterListResponseData;
      caching = (nvme_ScsiModeSenseCachingPage *) &p6Bheader[1];
      if (transferLen < 1) {
         vmkCmd->bytesXferred = 0;
         return NVME_STATUS_SUCCESS;
      }
      sizeOfData = sizeof(defCacheModePage)+sizeof(vmk_Scsi4ByteModeSenseParameterHeader);
   }

   vmk_Memcpy(caching,&defCacheModePage, sizeof(defCacheModePage));

   /*
    * TODO: Acquire Volatile Write Cache Feature via GetFeatures, and assign
    * the value to WCE.
    */
   caching->wce = (ns->ctrlr->identify.volWrCache) & 0x01;

   /* Sanity check that the command carries SG buffer with adequate size */
//   VMK_ASSERT(vmk_SgGetDataLen(vmkCmd->sgArray) >= sizeof(response_data));

   /* If the buffersize is not adequate, copy until the buffersize */
   if (transferLen > sizeOfData) {
       transferLen = sizeOfData;
   }

   if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE10) {
      p8Bheader->dataLen[0] = (vmk_uint8)(((transferLen - 2) >> 8)&0xFF);
      p8Bheader->dataLen[1] = (vmk_uint8)((transferLen - 2) &0xFF);
   } else if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE) {
      p6Bheader->modeDataLength = (vmk_uint8)((transferLen - 1)&0xFF);
   }

   vmk_SgCopyTo(vmkCmd->sgArray, &modeParameterListResponseData, transferLen);
   vmkCmd->bytesXferred = transferLen;

   return NVME_STATUS_SUCCESS;
}


/**
 * Handle SCSI Mode Sense Control page
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 */
static Nvme_Status
NvmeScsiCmd_DoModeSenseControl(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd, struct NvmeNsInfo *ns)
{
   vmk_Scsi4ByteModeSenseParameterHeader     *p6Bheader = NULL;
   nvme_ScsiModeSenseHeader8     *p8Bheader = NULL;
   nvme_ScsiModeSenseControlPage *control = NULL;

   vmk_ByteCount transferLen = vmk_SgGetDataLen(vmkCmd->sgArray);
   vmk_ByteCount sizeOfData = 0;

   vmk_Memset(&modeParameterListResponseData, 0, sizeof(modeParameterListResponseData));

   if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE10) {
      p8Bheader = (nvme_ScsiModeSenseHeader8 *)&modeParameterListResponseData;
      control = (nvme_ScsiModeSenseControlPage*) &p8Bheader[1];
      if (transferLen < 2) {
         vmkCmd->bytesXferred = 0;
         return NVME_STATUS_SUCCESS;
      }
      sizeOfData = sizeof(defControlModePage)+sizeof(nvme_ScsiModeSenseHeader8);
   } else if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE) {
      p6Bheader = (vmk_Scsi4ByteModeSenseParameterHeader *)&modeParameterListResponseData;
      control = (nvme_ScsiModeSenseControlPage*) &p6Bheader[1];
      if (transferLen < 1) {
         vmkCmd->bytesXferred = 0;
         return NVME_STATUS_SUCCESS;
       }
      sizeOfData = sizeof(defControlModePage)+sizeof(vmk_Scsi4ByteModeSenseParameterHeader);
   }

   vmk_Memcpy(control, &defControlModePage, sizeof(defControlModePage));
   if (transferLen > sizeOfData) {
       transferLen = sizeOfData;
   }

   if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE10) {
      p8Bheader->dataLen[0] = (vmk_uint8)(((transferLen - 2) >> 8)&0xFF);
      p8Bheader->dataLen[1] = (vmk_uint8)((transferLen - 2) &0xFF);
   } else if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE) {
      p6Bheader->modeDataLength = (vmk_uint8)((transferLen - 1)&0xFF);
   }

   vmk_SgCopyTo(vmkCmd->sgArray, &modeParameterListResponseData, transferLen);
   vmkCmd->bytesXferred = transferLen;

   return NVME_STATUS_SUCCESS;

}


   /**
 * Handle SCSI Mode Sense Power Condition page
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
    */
static Nvme_Status
NvmeScsiCmd_DoModeSensePC(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd, struct NvmeNsInfo *ns)
{
   vmk_Scsi4ByteModeSenseParameterHeader     *p6Bheader = NULL;
   nvme_ScsiModeSenseHeader8     *p8Bheader = NULL;
   nvme_ScsiModeSensePCPage      *pc = NULL;

   vmk_ByteCount transferLen = vmk_SgGetDataLen(vmkCmd->sgArray);
   vmk_ByteCount sizeOfData = 0;

   vmk_Memset(&modeParameterListResponseData, 0, sizeof(modeParameterListResponseData));

   if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE10) {
      p8Bheader = (nvme_ScsiModeSenseHeader8 *)&modeParameterListResponseData;
      pc = (nvme_ScsiModeSensePCPage*) &p8Bheader[1];
      if (transferLen < 2) {
         vmkCmd->bytesXferred = 0;
         return NVME_STATUS_SUCCESS;
      }
      sizeOfData = sizeof(defPcModePage)+sizeof(nvme_ScsiModeSenseHeader8);
   } else if(vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE) {
      p6Bheader = (vmk_Scsi4ByteModeSenseParameterHeader *)&modeParameterListResponseData;
      pc = (nvme_ScsiModeSensePCPage*) &p6Bheader[1];
      if (transferLen < 1) {
         vmkCmd->bytesXferred = 0;
   return NVME_STATUS_SUCCESS;
}
      sizeOfData = sizeof(defPcModePage)+sizeof(vmk_Scsi4ByteModeSenseParameterHeader);
   }

   vmk_Memcpy(pc, &defPcModePage, sizeof(defPcModePage));
   if (transferLen > sizeOfData) {
       transferLen = sizeOfData;
   }

   if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE10) {
      p8Bheader->dataLen[0] = (vmk_uint8)(((transferLen - 2) >> 8)&0xFF);
      p8Bheader->dataLen[1] = (vmk_uint8)((transferLen - 2) &0xFF);
   } else if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE) {
      p6Bheader->modeDataLength = (vmk_uint8)((transferLen - 1)&0xFF);
   }

   vmk_SgCopyTo(vmkCmd->sgArray, &modeParameterListResponseData, transferLen);
   vmkCmd->bytesXferred = transferLen;

   return NVME_STATUS_SUCCESS;
}
/**
 * Handle SCSI Mode Sense RW Error Recovery page
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 * @param [in]  pageAlone pointer to a buffer to hold the page alone without header
 */
static Nvme_Status
NvmeScsiCmd_DoModeSenseRWER(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd, struct NvmeNsInfo *ns)
{
   vmk_Scsi4ByteModeSenseParameterHeader     *p6Bheader = NULL;
   nvme_ScsiModeSenseHeader8     *p8Bheader = NULL;
   nvme_ScsiModeSenseRWErrorRecoveryPage *rwer = NULL;

   vmk_ByteCount transferLen = vmk_SgGetDataLen(vmkCmd->sgArray);
   vmk_ByteCount sizeOfData = 0;

   vmk_Memset(&modeParameterListResponseData, 0, sizeof(modeParameterListResponseData));

   if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE10) {
      p8Bheader = (nvme_ScsiModeSenseHeader8 *)&modeParameterListResponseData;
      rwer = (nvme_ScsiModeSenseRWErrorRecoveryPage*) &p8Bheader[1];
      if (transferLen < 2) {
         vmkCmd->bytesXferred = 0;
         return NVME_STATUS_SUCCESS;
      }
      sizeOfData = sizeof(defRwErrorRecoveryModePage)+sizeof(nvme_ScsiModeSenseHeader8);
   } else if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE) {
      p6Bheader = (vmk_Scsi4ByteModeSenseParameterHeader *)&modeParameterListResponseData;
      rwer = (nvme_ScsiModeSenseRWErrorRecoveryPage*) &p6Bheader[1];
      if (transferLen < 1) {
         vmkCmd->bytesXferred = 0;
         return NVME_STATUS_SUCCESS;
       }
      sizeOfData = sizeof(defRwErrorRecoveryModePage)+sizeof(vmk_Scsi4ByteModeSenseParameterHeader);
   }

   vmk_Memcpy(rwer, &defRwErrorRecoveryModePage, sizeof(defRwErrorRecoveryModePage));
   if (transferLen > sizeOfData) {
       transferLen = sizeOfData;
   }

   if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE10) {
      p8Bheader->dataLen[0] = (vmk_uint8)(((transferLen - 2) >> 8)&0xFF);
      p8Bheader->dataLen[1] = (vmk_uint8)((transferLen - 2) &0xFF);
   } else if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE) {
      p6Bheader->modeDataLength = (vmk_uint8)((transferLen - 1)&0xFF);
   }

   vmk_SgCopyTo(vmkCmd->sgArray, &modeParameterListResponseData, transferLen);
   vmkCmd->bytesXferred = transferLen;

   return NVME_STATUS_SUCCESS;
}


/**
 * Handle SCSI Mode Sense Return All page
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 */
static Nvme_Status
NvmeScsiCmd_DoModeSenseReturnAll(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd, struct NvmeNsInfo *ns)
{
   vmk_Scsi4ByteModeSenseParameterHeader     *p6Bheader = NULL;
   nvme_ScsiModeSenseHeader8     *p8Bheader = NULL;
   nvme_ScsiModeSenseRWErrorRecoveryPage *rwer = NULL;
   nvme_ScsiModeSenseCachingPage *caching = NULL;
   nvme_ScsiModeSenseControlPage *control = NULL;
   nvme_ScsiModeSensePCPage      *pc = NULL;

   vmk_ByteCount transferLen = vmk_SgGetDataLen(vmkCmd->sgArray);
   vmk_ByteCount sizeOfData = 0;

   vmk_Memset(&modeParameterListResponseData, 0, sizeof(modeParameterListResponseData));

   if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE10) {
      p8Bheader = (nvme_ScsiModeSenseHeader8 *)&modeParameterListResponseData;
      rwer = (nvme_ScsiModeSenseRWErrorRecoveryPage*) &p8Bheader[1];
      if (transferLen < 2) {
         vmkCmd->bytesXferred = 0;
         return NVME_STATUS_SUCCESS;
      }
      sizeOfData = TOTAL_MODE_PAGE_DATA_LEN + sizeof(nvme_ScsiModeSenseHeader8);
   } else if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE) {
      p6Bheader = (vmk_Scsi4ByteModeSenseParameterHeader *)&modeParameterListResponseData;
      rwer = (nvme_ScsiModeSenseRWErrorRecoveryPage*) &p6Bheader[1];
      if (transferLen < 1) {
         vmkCmd->bytesXferred = 0;
         return NVME_STATUS_SUCCESS;
       }
      sizeOfData = TOTAL_MODE_PAGE_DATA_LEN + sizeof(vmk_Scsi4ByteModeSenseParameterHeader);
   }
   // Order of response
   // NVME_SCSI_MS_PAGE_RWER, NVME_SCSI_MS_PAGE_CACHE, 
   // NVME_SCSI_MS_PAGE_CONTROL, NVME_SCSI_MS_PAGE_PC
   
   caching = (nvme_ScsiModeSenseCachingPage *) &rwer[1];
   control = (nvme_ScsiModeSenseControlPage*) &caching[1];
   pc = (nvme_ScsiModeSensePCPage*) &control[1];

   vmk_Memcpy(rwer, &defRwErrorRecoveryModePage, sizeof(defRwErrorRecoveryModePage));
   vmk_Memcpy(pc, &defPcModePage, sizeof(defPcModePage));
   vmk_Memcpy(control, &defControlModePage, sizeof(defControlModePage));
   vmk_Memcpy(caching, &defCacheModePage, sizeof(defCacheModePage));
   caching->wce = (ns->ctrlr->identify.volWrCache) & 0x01;

   if (transferLen > sizeOfData) {
       transferLen = sizeOfData;
   }

   if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE10) {
      p8Bheader->dataLen[0] = (vmk_uint8)(((transferLen - 2) >> 8)&0xFF);
      p8Bheader->dataLen[1] = (vmk_uint8)((transferLen - 2) &0xFF);
   } else if (vmkCmd->cdb[0] == VMK_SCSI_CMD_MODE_SENSE) {
      p6Bheader->modeDataLength = (vmk_uint8)((transferLen - 1)&0xFF);
   }

   vmk_SgCopyTo(vmkCmd->sgArray, &modeParameterListResponseData, transferLen);
   vmkCmd->bytesXferred = transferLen;

   return NVME_STATUS_SUCCESS;
}



/**
 * Handle SCSI Mode Sense (6) command
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 */
static Nvme_Status
NvmeScsiCmd_DoModeSense(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd, struct NvmeNsInfo *ns)
{
   Nvme_Status           nvmeStatus;
   vmk_ScsiModeSenseCmd *cdb = (vmk_ScsiModeSenseCmd *)vmkCmd->cdb;
   vmk_Bool              isModeSense6 = (cdb->opcode == VMK_SCSI_CMD_MODE_SENSE);

   // Checking Page Control. Will support only VMK_SCSI_MS_PCF_CURRENT
   // and VMK_SCSI_MS_PCF_SAVED

   if (cdb->pcf != VMK_SCSI_MS_PCF_CURRENT)
      return NVME_STATUS_INVALID_FIELD_IN_CDB;

   // Checking for CDB Length
   if (isModeSense6) {
      if (vmkCmd->cdbLen != MODE_SENSE6_CDB_LEN) {
         return NVME_STATUS_INVALID_FIELD_IN_CDB;
      }
      // Checking for reserved bits. Only Bit3 is valid in the second byte
      // Control field is also assumed to be zero so as the subpage 
      if (((vmkCmd->cdb[1] & (~0x08))!=0) ||
         (cdb->ctrl != 0) ||
         (cdb->subpage != 0)) {
         DPRINT_CMD("Invalid bit is set");
         return NVME_STATUS_INVALID_FIELD_IN_CDB;
      }
   } else {
      if (vmkCmd->cdbLen != MODE_SENSE10_CDB_LEN) {
         return NVME_STATUS_INVALID_FIELD_IN_CDB;
      }
      // Checking for reserved bits/Bytes. Byte1 bits 0-2&5-7, Byte 4-6 are reserved
      // Control field is also assumed to be zero so as the subpage
      if (((vmkCmd->cdb[1] & (0xE7))!=0) ||          // BYTE 1
         (vmkCmd->cdb[4] != 0) ||                   // BYTE 4
         (vmkCmd->cdb[5] != 0) ||                   // BYTE 4
         (vmkCmd->cdb[6] != 0) ||                   // BYTE 4
         (vmkCmd->cdb[9] != 0) ||                   // Control field
         (cdb->subpage != 0)) {
         DPRINT_CMD("Invalid bit is set");
         return NVME_STATUS_INVALID_FIELD_IN_CDB;
      }
   }

   DPRINT_CMD("CdbLength is correct");

   switch(cdb->page) {
      case NVME_SCSI_MS_PAGE_CACHE:
         nvmeStatus = NvmeScsiCmd_DoModeSenseCache(ctrlr, vmkCmd, ns);
         break;
      case NVME_SCSI_MS_PAGE_CONTROL:
         nvmeStatus = NvmeScsiCmd_DoModeSenseControl(ctrlr, vmkCmd, ns);
         break;
      case NVME_SCSI_MS_PAGE_PC:
         nvmeStatus = NvmeScsiCmd_DoModeSensePC(ctrlr, vmkCmd, ns);
         break;
      case NVME_SCSI_MS_PAGE_RWER:
         nvmeStatus = NvmeScsiCmd_DoModeSenseRWER(ctrlr, vmkCmd, ns);
	 break;
      case NVME_SCSI_MS_PAGE_ALL:
         nvmeStatus = NvmeScsiCmd_DoModeSenseReturnAll(ctrlr, vmkCmd, ns);
         break;
      default:
         nvmeStatus = NVME_STATUS_INVALID_FIELD_IN_CDB;
         break;
   }

   return nvmeStatus;
}

typedef enum {
   LOG_SENSE_SUPPORTED_PAGES            = 0x00,
   LOG_SENSE_TEMPERATURE_PAGE           = 0x0d,
   LOG_SENSE_IE_PAGE                    = 0x2f,
} LogSensePageCode;

/**
 * brief Format of Log Page header
 * SPC 4 r32, Section 7.3.2.1
 */
typedef struct ScsiLogPageHeader {
   /** page code, SPF, DS */
   vmk_uint8    pageCode        :6;
   vmk_uint8	spf             :1;
   vmk_uint8    ds              :1;
   /** subpage code */
   vmk_uint8    subpageCode;
   /** page length */
   vmk_uint8   pageLength[2];

}VMK_ATTRIBUTE_PACKED ScsiLogPageHeader;

#define TOTAL_SUPPORTED_LOG_PAGES 0x3

/**
  * brief Format of Supported Log Pages log page
  * SPC 4 r32, Section 7.3.18 table 372
  */
typedef struct ScsiSupportedLogPages {
   ScsiLogPageHeader header;
   /** supported page list */
   vmk_uint8    supportPageList[TOTAL_SUPPORTED_LOG_PAGES];

}VMK_ATTRIBUTE_PACKED ScsiSupportedLogPages;



/**
  * brief Obtain supported log pages.
  * @param[in] vmk_ScsiCommand *vmkCmd scsi command structure
  *
  * @return NVME_STATUS_SUCCESS
  */
static Nvme_Status NvmeScsiCmd_SupportedLogPages(vmk_ScsiCommand *vmkCmd, vmk_uint16 len)
{
   ScsiSupportedLogPages response_data;
   vmk_Memset(&response_data, 0, sizeof(response_data));

   /*PAGE CODE: Set to 00h as specified in SPC-4.*/
   response_data.header.pageCode = LOG_SENSE_SUPPORTED_PAGES;

   /*SPF: Set to 0b as specified in SPC-4.*/
   response_data.header.spf = 0;

   /*DS: Set to 0b as specified in SPC-4.*/
   response_data.header.ds = 0;

   /*SUBPAGE CODE:Set to 00h as specified in SPC-4.*/
   response_data.header.subpageCode = 0x00;

   /*PAGE LENGTH: Shall be set to 6h indicating the length of Supported Pages List.*/
   response_data.header.pageLength[0] = 0x00;
   response_data.header.pageLength[1] = TOTAL_SUPPORTED_LOG_PAGES;

   response_data.supportPageList[0] = LOG_SENSE_TEMPERATURE_PAGE;
   response_data.supportPageList[1] = LOG_SENSE_IE_PAGE;

   vmkCmd->bytesXferred = min_t(vmk_ByteCount, sizeof(response_data), vmk_SgGetDataLen(vmkCmd->sgArray));
   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, vmkCmd->bytesXferred);

   return NVME_STATUS_SUCCESS;
}

/**
 * brief Format of log parameter header
 * SPC 4 r32, Section 7.3.2.2.1
 */
typedef struct ScsiLogParaHeader {
   /** PARAMETER CODE (0000h) */
   vmk_uint16   parameterCode;
   /** Parameter control byte <96> binary format list log parameter (see 7.3.2.2.2.5) */
   vmk_uint8    fal                     :2; //FORMAT AND LINKING
   vmk_uint8    tmc                     :2;
   vmk_uint8    etc                     :1;
   vmk_uint8    tsd                     :1;
   vmk_uint8    obsolete                :1;
   vmk_uint8    du                      :1;
   /** PARAMETER LENGTH (n-3) */
   vmk_uint8    parameterLength;

}VMK_ATTRIBUTE_PACKED ScsiLogParaHeader;
/**
  * brief Format of Informational Exceptions General log parameter
  * SPC 4 r32, Section 7.3.8.2 table 337
  */
typedef struct ScsiIELogPara {
   struct ScsiLogParaHeader header;

   /** INFORMATIONAL EXCEPTION ADDITIONAL SENSE CODE */
   vmk_uint8    IEAsc;
   /** INFORMATIONAL EXCEPTION ADDITIONAL SENSE CODE QUALIFIER */
   vmk_uint8    IEAscq;
   /** MOST RECENT TEMPERATURE READING */
   vmk_uint8    mrtr;
}VMK_ATTRIBUTE_PACKED ScsiIELogPara;



/**
  * brief Format of Infomational Exceptions log page
  * SPC 4 r32, Section 7.3.8.1 table 336
  */
typedef struct ScsiIELogPage {
   struct ScsiLogPageHeader header;

   /** Informational exceptions log parameters */
   struct ScsiIELogPara IELogPara;
}VMK_ATTRIBUTE_PACKED ScsiIELogPage;

/**
  * brief Format of Temperature Log Parameter,
  * SPC 4 r32, Section 7.3.21.2 table 380
  */
typedef struct ScsiTempLogPara {
   struct ScsiLogParaHeader header;
   vmk_uint8    reserved;
   vmk_uint8    temperature;
}VMK_ATTRIBUTE_PACKED ScsiTempLogPara;

/**
  * brief Format of Reference Temperature Log Parameter
  * SPC 4 r32, Section 7.3.21.3 table 381
  */
typedef struct ScsiRefTempLogPara {
   struct ScsiLogParaHeader header;
   vmk_uint8    reserved;
   /** REFERENCE TEMPERATURE*/
   vmk_uint8    refTemp;
}VMK_ATTRIBUTE_PACKED ScsiRefTempLogPara;

/**
  * brief Format of Temperature Log Page,
  * SPC 4 r32, Section 7.3.21 table 379
  */
typedef struct ScsiTempLogPage {
   struct ScsiLogPageHeader     header;
   struct ScsiTempLogPara       tempLogPara;
   struct ScsiRefTempLogPara    refTempLogPara;
}VMK_ATTRIBUTE_PACKED ScsiTempLogPage;

/**
  * brief Format of LOG SENSE COMMAND
  * SPC 4 r32, Section 6.6 table 152
  */
typedef struct {
   /** OPERATION CODE (4Dh) */
   vmk_uint8  opCode;
   /** SP, ect */
   vmk_uint8    sp        : 1;
   vmk_uint8    obselete  : 1;
   vmk_uint8    reserved1 : 6;
   /** PAGE CODE */
   vmk_uint8    pageCode  : 6;
   vmk_uint8    pc        : 2;
   /** SUBPAGE CODE */
   vmk_uint8 subpageCode;
   /** Reserved */
   vmk_uint8 reserved2;
   /** Parameter pointer*/
   vmk_uint16 parameterPointer;
   /** Allocation length*/
   vmk_uint16 allocationLength;
   /** CONTROL */
   vmk_uint8  control;
} VMK_ATTRIBUTE_PACKED nvme_ScsiLogSenseCommand;

/*
 * @brief Information neede by SCSI LOG SENSE command
 */
typedef struct ScsiLogPageInfo {
   struct smart_log     smart;

   /* only LOG_SENSE_TEMPERATURE_PAGE and LOG_SENSE_IE_PAGE are supported now */
   vmk_uint32           pageCode;
}VMK_ATTRIBUTE_PACKED ScsiLogPageInfo;


#define SMART_INVALID_TEMPERATURE 0xff

static void
NvmeScsiCmd_CleanLogPage(struct NvmeQueueInfo *qinfo, struct NvmeCmdInfo *cmdInfo)
{
   if(cmdInfo->cleanupData) {
      Nvme_Free(cmdInfo->cleanupData);
      cmdInfo->cleanupData = NULL;
   }

}

/*
 * @brief Fill proper values to IE Log Page's fields.
 * @para[in] vmkCmd, scsi command
 * @para[in] temp8, temperature value
 * @para[out] IEpage, IE log page to be properly filled.
 */
static void
NvmeScsiCmd_FillIELogPage(struct vmk_ScsiCommand *vmkCmd, vmk_uint8 temp8,
                          struct ScsiIELogPage *IEPage)
{
   VMK_ASSERT(IEPage != NULL);

   vmk_Memset(IEPage, 0, sizeof(*IEPage)); 
   
   /*MOST RECENT TEMPERATURE READING: Shall be supported using Temperature field of Get Log Page
    *SMART/Health Information Log. A conversion to Celsius from Kelvin must occur.
    */
   IEPage->IELogPara.mrtr = temp8;

   /*PAGE CODE: Set to 2Fh as specified in SPC-4.*/
   IEPage->header.pageCode = LOG_SENSE_IE_PAGE;

   /* SPF: Set to 0b as specified in SPC-4.*/
   IEPage->header.spf = 0;

   /* DS: Set to 0b as specified in SPC-4.*/
   IEPage->header.ds = 0;

   /* SUBPAGE CODE: Set to 00h as specified in SPC-4.*/
   IEPage->header.subpageCode = 0x00;

   /* PAGE LENGTH: Shall be set to 8h indicating the length of remaining log page.*/
   IEPage->header.pageLength[0] = 0x00;
   IEPage->header.pageLength[1] = 0x08;

   /* PARAMETER CODE: Set to 0000h as specified by SPC-4.*/
   IEPage->IELogPara.header.parameterCode = 0x0000;

   /* FORMAT AND LINKING: Shall be set to 11b indicating parameter is a binary format list parameter.*/
   IEPage->IELogPara.header.fal = 3;

   /* TMC: Set to 00b as specified in SPC-4.*/
   IEPage->IELogPara.header.tmc = 0x00;

   /* ETC: Set to 0b as specified in SPC-4.*/
   IEPage->IELogPara.header.etc = 0;

   /* TSD: Set to 1b indicating log parameter disabled.*/
   IEPage->IELogPara.header.tsd = 1;

   /* DU: Set to 0b as specified in SPC-4.*/
   IEPage->IELogPara.header.du = 0;

   /* PARAMETER LENGTH: Shall be set to 04h indicating 4 parameters.*/
   IEPage->IELogPara.header.parameterLength = 0x04;

   /* INFORMATIONAL EXCEPTION ADDITIONAL SENSE CODE: Shall be set to 0h.*/
   IEPage->IELogPara.IEAsc = 0;

   /* INFORMATIONAL EXCEPTION ADDITIONAL SENSE CODE QUALIFIER: Shall be set to 0h.*/
   IEPage->IELogPara.IEAscq = 0;
}

/** brief Obtainn temperature log page.
  * @param[in] vmkCmd, scsi command
  * @param[in] temp8, temperature value
  * @param[out] tempPage, temp log page to be filled properly
  *
  * @return NVME_STATUS_SUCCESS or failure codes.
  */
static void NvmeScsiCmd_FillTempLogPage(struct vmk_ScsiCommand *vmkCmd, vmk_uint8 temp8,
                                        struct ScsiTempLogPage *tempPage)
{
   VMK_ASSERT(tempPage != NULL);

   vmk_Memset(tempPage, 0, sizeof(*tempPage));

   /* SPF: Shall be set to 0b as specified in SPC-4.*/
   tempPage->header.spf = 0;

   /* PAGE CODE: Shall be set to 0Dh as specified in SPC-4. */
   tempPage->header.pageCode = 0x0d;

   /* SUBPAGE CODE: Shall be set to 00h as specified by SPC-4.*/
   tempPage->header.subpageCode = 0x00;

   /* PAGE LENGTH: Shall be set to 0Ch as specified by SPC-4.*/
   tempPage->header.pageLength[0] = 0x00;
   tempPage->header.pageLength[1] = 0x0c;

   /* PARAMETER CODE: Shall be set to 0000h as specified in SPC-4.*/
   tempPage->tempLogPara.header.parameterCode = 0x0000;

   /* FORMAT AND LINKING: Shall be set to 01b indicating parameter is in binary format.*/
   tempPage->tempLogPara.header.fal = 1;

   /* TMC: Shall be set to 00b for LOG SENSE as specified by SPC-4 or ignored.*/
   tempPage->tempLogPara.header.tmc = 0;

  /* ETC: Shall be set to 0 for LOG SENSE as specified by SPC-4 or ignored.*/
   tempPage->tempLogPara.header.etc = 0;

   /* TSD: Shall be set to 0 for LOG SENSE as specified by SPC-4 or ignored.*/
   tempPage->tempLogPara.header.tsd = 0;

   /* DU: Shall be set to 0 for LOG SENSE as specified by SPC-4 or ignored.*/
   tempPage->tempLogPara.header.du = 0;

   /* PARAMETER LENGTH: Shall be set to 02h as specified in SPC-4.*/
   tempPage->tempLogPara.header.parameterLength = 0x02;

   /* PARAMETER CODE: Shall be set to 0001h as specified in SPC-4.*/
   tempPage->refTempLogPara.header.parameterCode = 0x0001;

   /* FORMAT AND LINKING: Shall be set to 01b indicating parameter is in binary format.*/
   tempPage->refTempLogPara.header.fal = 1;

   /* TMC: Shall be set to 0 for LOG SENSE as specified by SPC-4 or ignored.*/
   tempPage->refTempLogPara.header.tmc = 0;

   /* ETC: Shall be set to 0 for LOG SENSE as specified by SPC-4 or ignored.*/
   tempPage->refTempLogPara.header.etc = 0;

   /* TSD: Shall be set to 0 for LOG SENSE as specified by SPC-4 or ignored.*/
   tempPage->refTempLogPara.header.tsd = 0;

   /* DU: Shall be set to 0 for LOG SENSE as specified by SPC-4 or ignored.*/
   tempPage->refTempLogPara.header.du = 0;

   /* PARAMETER LENGTH: Shall be set to 02h as specified in SPC-4.*/
   tempPage->refTempLogPara.header.parameterLength = 0x02;

   /* REFERENCE TEMPERATURE: Should be set using Temperature Threshold Feature of Get Features command. This shall require a conversion from Kelvin to Celsius*/
   tempPage->refTempLogPara.refTemp = SMART_TEMPERATURE_DEFAULT_THRESHOLD;

   /*TEMPERATURE: Shall be set to Temperature field of Get Log Page âMART / Health Information Log.
    *This shall require a conversion from Kelvin to Celsius.
    */
   tempPage->tempLogPara.temperature = temp8;
}




/*
 * @brief Complete SCSI LOG SENSE command.
 *
 * Note: queue lock is held by the caller
 */
static void
NvmeScsiCmd_CompleteLogPage(struct NvmeQueueInfo *qinfo, struct NvmeCmdInfo *cmdInfo)
{
   vmk_uint32	           	temp32;
   vmk_uint8    	        temp8;
   vmk_ScsiCommand      	*vmkCmd;
   struct ScsiLogPageInfo	*pageInfo = (ScsiLogPageInfo*)cmdInfo->cleanupData;
   struct ScsiIELogPage		IEPage;
   struct ScsiTempLogPage	tempPage;

   GET_VMK_SCSI_CMD(cmdInfo->cmdPtr, vmkCmd);
   VMK_ASSERT(vmkCmd);

   if (VMK_UNLIKELY(cmdInfo->type == ABORT_CONTEXT)) {
      vmkCmd->bytesXferred = 0;
      goto out;
   } else {
      /*Copy log page data from dma's va*/
      Nvme_Memcpy64(&(pageInfo->smart), (struct smart_log*)cmdInfo->prps, LOG_PG_SIZE/sizeof(vmk_uint64));
      cmdInfo->status = NVME_CMD_STATUS_DONE;
      if(NvmeMgmt_Convert(pageInfo->smart.temperature, 2, &temp32) == VMK_OK) {
         temp32 -= 273;
         temp8 = temp32 & 0xff;
      } else {
         temp8 = SMART_INVALID_TEMPERATURE;
      }
   }

   switch (pageInfo->pageCode) {
      case LOG_SENSE_TEMPERATURE_PAGE:
         NvmeScsiCmd_FillTempLogPage(vmkCmd, temp8, &tempPage);
         vmkCmd->bytesXferred = min_t(vmk_ByteCount, sizeof(tempPage), vmk_SgGetDataLen(vmkCmd->sgArray));
         vmk_SgCopyTo(vmkCmd->sgArray, &tempPage, vmkCmd->bytesXferred);
         break;
      case LOG_SENSE_IE_PAGE:
         NvmeScsiCmd_FillIELogPage(vmkCmd, temp8, &IEPage);
         vmkCmd->bytesXferred = min_t(vmk_ByteCount, sizeof(IEPage), vmk_SgGetDataLen(vmkCmd->sgArray));
         vmk_SgCopyTo(vmkCmd->sgArray, &IEPage, vmkCmd->bytesXferred);
         break;
      default:
         EPRINT("log sense Page code 0x%x not supported.", pageInfo->pageCode);
         break;
   }

out:
   SCSI_CMD_INVOKE_COMPLETION_CB(cmdInfo->cmdPtr);

   if (cmdInfo->cleanup) {
      cmdInfo->cleanup(qinfo, cmdInfo);
   }

   NvmeCore_PutCmdInfo(qinfo, cmdInfo);
   qinfo->timeout[cmdInfo->timeoutId] --;
}



/**
  * @brief Process SCSI LOG SENSE command
  * @param[in] struct NvmeCtrlr *ctrlr controller structure
  * @param[in] vmk_ScsiCommand *vmkCmd SCSI command
  * @param[in] struct NvmeNsInfo *ns namespace information
  *
  * @return NVME_STATUS_SUCCESS or failure codes.
  */
static Nvme_Status
NvmeScsiCmd_DoLogSense(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd,
                       struct NvmeNsInfo *ns)
{
   struct NvmeQueueInfo         *qinfo = &(ctrlr->adminq);
   struct NvmeCmdInfo           *cmdInfo;
   struct ScsiLogPageInfo	*logPageInfo;
   VMK_ReturnStatus             vmkStatus;

   nvme_ScsiLogSenseCommand *logSenseCmd = (nvme_ScsiLogSenseCommand *)vmkCmd->cdb;

   if (logSenseCmd->sp == 1) {
      EPRINT("logSenseCmd->sp is %d", logSenseCmd->sp);
      return NVME_STATUS_INVALID_FIELD_IN_CDB;
   }

   /*PC=01b: Cumulative values shall be returned to application client.
    *PC=others: Command shall be terminated with CHECK CONDITION status, ILLEGAL REQUEST
    *          sense key, and ILLEGAL FIELD IN CDB additional sense code.
    */
   if (logSenseCmd->pc != 1){
      EPRINT("logSenseCmd->pc is %d", logSenseCmd->pc);
      return NVME_STATUS_INVALID_FIELD_IN_CDB;
   }

   switch(logSenseCmd->pageCode) {
      case LOG_SENSE_SUPPORTED_PAGES:
         return NvmeScsiCmd_SupportedLogPages(vmkCmd, logSenseCmd->allocationLength);

      case LOG_SENSE_TEMPERATURE_PAGE:
      case LOG_SENSE_IE_PAGE:
         logPageInfo = Nvme_Alloc(sizeof(*logPageInfo), 0, NVME_ALLOC_ZEROED);
         if (!logPageInfo) {
            EPRINT("failed to allocate ScsiLogPageInfo.");
            return NVME_STATUS_FAILURE;
         }
         /* Obtain cmdInfo, allocate space and set cleanup*/
         LOCK_FUNC(qinfo);
         cmdInfo = NvmeCore_GetCmdInfo(qinfo);
         if (!cmdInfo) {
            UNLOCK_FUNC(qinfo);
            EPRINT("failed to acuqire cmdInfo");
            goto free_logpage;
         }
         UNLOCK_FUNC(qinfo);

         vmk_Memset(logPageInfo, 0, sizeof(*logPageInfo));
         logPageInfo->pageCode = logSenseCmd->pageCode;
         cmdInfo->cleanupData = logPageInfo;
         cmdInfo->done = NvmeScsiCmd_CompleteLogPage;
         cmdInfo->cleanup = NvmeScsiCmd_CleanLogPage;
         cmdInfo->cmdPtr = vmkCmd;

         vmkStatus = NvmeCtrlrCmd_GetSmartLog(ctrlr, ns->id, (void*)(&logPageInfo->smart), cmdInfo, VMK_FALSE);
         if (vmkStatus != VMK_OK) {
            vmkCmd->bytesXferred = 0;
            EPRINT("failed to get smart log");
            goto putcmd;
         } else {
            return NVME_STATUS_WOULD_BLOCK;
         }

      default:
         DPRINT_CMD("logSenseCmd->pageCode %x is INVALID", logSenseCmd->pageCode);
         return NVME_STATUS_INVALID_FIELD_IN_CDB;
   }

putcmd:
   LOCK_FUNC(qinfo);
   NvmeCore_PutCmdInfo(qinfo, cmdInfo);
   qinfo->timeout[cmdInfo->timeoutId] --;
   UNLOCK_FUNC(qinfo);

free_logpage:
   Nvme_Free(logPageInfo);
   return NVME_STATUS_FAILURE;

}


#define TUR_TIMEOUT  (1000 * 1000)

typedef struct
{
   vmk_uint8   opcode;
   vmk_uint32  rsvd;
   vmk_uint8   ctrl;
} VMK_ATTRIBUTE_PACKED scsiTestUnitReadyCmd;

/**
 * Handle SCSI Test Unit Ready command
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 */
static Nvme_Status
NvmeScsiCmd_DoTUR(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd,
                  struct NvmeNsInfo *ns)
{
   scsiTestUnitReadyCmd *cmd = (scsiTestUnitReadyCmd *)vmkCmd->cdb;

   // Validating and returning failure when:
   // 1. reserved field is non zero.
   // 2. control byte is non zero.
   if (cmd->rsvd != 0 || cmd->ctrl != 0) {
      return NVME_STATUS_INVALID_FIELD_IN_CDB;
   }

   return NVME_STATUS_OK;
}


/**
 * Handle SCSI Unamp command
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 */
static Nvme_Status
NvmeScsiCmd_DoUnmap(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd, struct NvmeNsInfo *ns)
{
   VMK_ReturnStatus vmkStatus;
#if NVME_DEBUG
   nvme_ScsiUnmapCommand *cdb = (nvme_ScsiUnmapCommand *)vmkCmd->cdb;
#endif
   nvme_ScsiUnmapParameterList *unmapParamList = NULL;
   /** This is a temporary buffer to hold SCSI UNMAP -> DSM Ranges translation */
   struct nvme_dataset_mgmt_data *dsmData = NULL;
   int i, count;
   Nvme_Status nvmeStatus;

   DPRINT_CMD("Unmap cmd %p: anchor: %d, groupNum: %d, paramListLen: %d.",
           vmkCmd, cdb->anchor, cdb->groupNum, vmk_BE16ToCPU(cdb->parameterListLen));

   vmk_AtomicInc64(&ctrlr->activeUnmaps);
   if (vmk_AtomicRead64(&ctrlr->maxUnmaps) < vmk_AtomicRead64(&ctrlr->activeUnmaps))
      vmk_AtomicWrite64(&ctrlr->maxUnmaps, vmk_AtomicRead64(&ctrlr->activeUnmaps));
   DPRINT_CMD("scsi unmap cmd num: active: %ld, max: %ld, supported: %d.",
           vmk_AtomicRead64(&ctrlr->activeUnmaps), vmk_AtomicRead64(&ctrlr->maxUnmaps),
           max_scsi_unmap_requests);

   unmapParamList = vmk_SlabAlloc(ctrlr->scsiUnmapSlabId);
   if (!unmapParamList) {
      EPRINT("Failed to allocate slab memory for unmapParamList.");
      nvmeStatus = NVME_STATUS_FAILURE;
      goto out_return;
   }

   dsmData = vmk_SlabAlloc(ctrlr->scsiUnmapSlabId);
   if (!dsmData) {
      EPRINT("Failed to allocate slab memory for dsmData");
      nvmeStatus = NVME_STATUS_FAILURE;
      goto out_return;
   }

   vmkStatus = vmk_SgCopyFrom(unmapParamList, vmkCmd->sgArray,
                              min_t(vmk_ByteCount, sizeof(nvme_ScsiUnmapParameterList),
                                    vmk_SgGetDataLen(vmkCmd->sgArray)));
   if (vmkStatus != VMK_OK) {
      EPRINT("failed to acquire unmap parameter lists.");
      VMK_ASSERT(vmkStatus == VMK_OK);
      nvmeStatus = NVME_STATUS_INVALID_FIELD_IN_CDB;
      goto out_return;
   }

   DPRINT_CMD("Unmap cmd %p: unmapDataLen %d, unmapBlockDescriptorDataLen %d.",
           vmkCmd, vmk_BE16ToCPU(unmapParamList->unmapDataLen),
           vmk_BE16ToCPU(unmapParamList->unmapBlockDescriptorDataLen));

   /*
    * Translate UNMAP block descriptor list to DSM ranges.
    *
    * Note that the DSM ranges buffer is temporary; when this
    * buffer is passed to NVM layer, the NVM layer should keep its local
    * copy instead of using it as a persistent store.
    */
   vmk_Memset(dsmData, 0, sizeof(struct nvme_dataset_mgmt_data)*NVME_MAX_DSM_RANGE);
   count = vmk_BE16ToCPU(unmapParamList->unmapBlockDescriptorDataLen)/sizeof(nvme_ScsiUnmapBlockDescriptor);
   if (count >= NVME_MAX_DSM_RANGE) {
      EPRINT("invalid unmap parameter for cmd %p: %d ranges provided (dataLen %d, blockDescriptorLen %d).",
             vmkCmd, count, vmk_BE16ToCPU(unmapParamList->unmapDataLen),
             vmk_BE16ToCPU(unmapParamList->unmapBlockDescriptorDataLen));
      VMK_ASSERT(0);
      nvmeStatus = NVME_STATUS_INVALID_FIELD_IN_CDB;
      goto out_return;
   }

   for (i = 0; i < count; i++) {
      dsmData[i].startLBA = vmk_BE64ToCPU(unmapParamList->unmapBlockDescriptorList[i].unmapLba);
      dsmData[i].numLBA = vmk_BE32ToCPU(unmapParamList->unmapBlockDescriptorList[i].numBlocks);
      DPRINT_CMD("Unmap cmd %p: %d/%d, lba 0x%lx, lbc %d.",
              vmkCmd, i, count, dsmData[i].startLBA, dsmData[i].numLBA);
   }

   nvmeStatus = NvmeIo_SubmitDsm(ns, vmkCmd, dsmData, count);

out_return:
   if (dsmData)
      vmk_SlabFree(ctrlr->scsiUnmapSlabId, dsmData);
   if (unmapParamList)
      vmk_SlabFree(ctrlr->scsiUnmapSlabId, unmapParamList);
   vmk_AtomicDec64(&ctrlr->activeUnmaps);
   return nvmeStatus;
}


/*
 * Handle SCSI SYNCHRONIZE CACHE (10) command
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkPtr pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 *
 * return value: success or error code
 *
 * ref: SBC3r21, 5.21, Table 67
 */
static Nvme_Status
NvmeScsiCmd_DoSyncCache(struct NvmeCtrlr *ctrlr, void *cmdPtr, struct NvmeNsInfo *ns)
{
   Nvme_Status          nvmeStatus;
   int                  qid;
   struct NvmeQueueInfo *qinfo;
   vmk_ScsiCommand      *vmkCmd;

   GET_VMK_SCSI_CMD(cmdPtr, vmkCmd);

   /* Get the queue for submitting I/O*/
   qid = OsLib_GetQueue(ctrlr, vmkCmd);
   if(qid >= ctrlr->numIoQueues) {
      EPRINT("invalid compleiton queue: %d numIoQueues: %d.",
             qid, ctrlr->numIoQueues);
      return NVME_STATUS_QUIESCED;
   }

   /* Check IMMED bit. According to spc4r36, An immediate (IMMED)=0 specifies that the device server
    * shall not return status until the operation has been completed. IMMED=1 specifies that the device
    * server shall return status as soon as the CDB has been validated. However, the issuing path
    * (SCSIStartPathCommands()) disallows blocking in this world (PR159076, PR158746). PSOD would be
    * triggered in debug build otherwise*/
   if (!(vmkCmd->cdb[1]& 0x02)) {
      WPRINT("IMMED=0 is not allowed");
      return NVME_STATUS_INVALID_FIELD_IN_CDB;
   }

   vmkCmd->bytesXferred = vmkCmd->requiredDataLen;

   qinfo = &ctrlr->ioq[qid];

   nvmeStatus = NvmeIo_SubmitFlush(ns, cmdPtr, qinfo);

   /*account for the number of IO requests to the queue*/
   if (nvmeStatus == NVME_STATUS_WOULD_BLOCK) {
      LOCK_FUNC(qinfo);
      qinfo->nrReq ++;
      if (qinfo->maxReq < qinfo->nrReq) {
         qinfo->maxReq = qinfo->nrReq;
      }
      UNLOCK_FUNC(qinfo);
   }

   return nvmeStatus;
}


Nvme_Status HealthDegradedStateHandler (void *clientData, void *cmdPtr, void *deviceData)
{
   struct NvmeCtrlr  *ctrlr = (struct NvmeCtrlr *)clientData;
   vmk_uint64 healthState;
   Nvme_Status nvmeStatus;

   vmk_ScsiCommand      *vmkCmd;

   GET_VMK_SCSI_CMD(cmdPtr, vmkCmd);

   healthState = NvmeCtrlr_AtomicGetHealthState(ctrlr);
   if (healthState &  SMART_GLP_CRIT_WARN_TEMP_ABOV_THRSHLD) {
      nvmeStatus = NVME_STATUS_OVERTEMP;
   }
   else 
      nvmeStatus = NVME_STATUS_WRITE_PROTECT;
   return nvmeStatus;
}


/**
 * Processing SCSI command
 *
 * @param [in]  clientData pointer to the controller instance
 * @param [in]  cmdPtr pointer to the scsi command
 * @param [in]  deviceData pointer to the namespace
 */
VMK_ReturnStatus scsiProcessCommand(void *clientData, void *cmdPtr, void *deviceData)
{
   struct NvmeCtrlr  *ctrlr = (struct NvmeCtrlr *)clientData;
   struct NvmeNsInfo *ns = (struct NvmeNsInfo *)deviceData;
   VMK_ReturnStatus   vmkStatus;
   Nvme_Status        nvmeStatus;
   Nvme_CtrlrState    state;

   vmk_ScsiCommand    *vmkCmd;

   GET_VMK_SCSI_CMD(cmdPtr, vmkCmd);

#if NVME_DEBUG
   if (nvme_dbg & NVME_DEBUG_DUMP_CDB) {
      NvmeDebug_DumpCdb(vmkCmd->cdb);
   }
#endif

   state = NvmeState_GetCtrlrState(ctrlr, VMK_TRUE);

   if (VMK_UNLIKELY(state > NVME_CTRLR_STATE_INRESET)) {
      /**
       * Ctrlr either missing, or in tear down path, or failed
       */
      DPRINT_CTRLR("controller offline, %s.", NvmeState_GetCtrlrStateString(state));
      vmkCmd->bytesXferred = 0;
#if ENABLE_HEALTH_APD
      if (VMK_UNLIKELY(state == NVME_CTRLR_STATE_HEALTH_DEGRADED)) {
         nvmeStatus = HealthDegradedStateHandler(clientData,cmdPtr,deviceData);
      }	
      else 
#endif
      {
	 nvmeStatus = NVME_STATUS_FATAL_ERROR;
      }
      goto out_return;
   } else if (VMK_UNLIKELY(state == NVME_CTRLR_STATE_INRESET)) {
      /**
       * Transient error
       */
      DPRINT_CTRLR("controller in reset.");
      vmkCmd->bytesXferred = 0;
      nvmeStatus           = NVME_STATUS_IN_RESET;
      goto out_return;
   } else if (VMK_UNLIKELY(state != NVME_CTRLR_STATE_OPERATIONAL)) {
      DPRINT_CTRLR("controller not in ready state, %s.", NvmeState_GetCtrlrStateString(state));
      vmkCmd->bytesXferred = 0;
      nvmeStatus           = NVME_STATUS_BUSY;
      goto out_return;
   }

   if (VMK_UNLIKELY(!NvmeCore_IsNsOnline(ns))) {
      /**
       * Check if namespace is offline.
       */
      vmkCmd->bytesXferred = 0;
      nvmeStatus           = NVME_STATUS_NS_OFFLINE;
      goto out_return;
   }

   switch(vmkCmd->cdb[0]) {
      case VMK_SCSI_CMD_REPORT_LUNS:
         nvmeStatus = NvmeScsiCmd_DoReportLuns(ctrlr, vmkCmd, ns);
         break;
      case VMK_SCSI_CMD_INQUIRY:
         nvmeStatus = NvmeScsiCmd_DoInquiry(ctrlr, vmkCmd, ns);
         break;
      case VMK_SCSI_CMD_READ6:
      case VMK_SCSI_CMD_READ10:
      case VMK_SCSI_CMD_READ12:
      case VMK_SCSI_CMD_READ16:
         /* fall through */
      case VMK_SCSI_CMD_WRITE6:
      case VMK_SCSI_CMD_WRITE10:
      case VMK_SCSI_CMD_WRITE12:
      case VMK_SCSI_CMD_WRITE16:
         nvmeStatus = NvmeScsiCmd_DoIO(ctrlr, cmdPtr, ns);
         break;
      case VMK_SCSI_CMD_READ_CAPACITY:
         nvmeStatus = NvmeScsiCmd_DoReadCapacity(ctrlr, vmkCmd, ns);
         break;
      case VMK_SCSI_CMD_SERVICE_ACTION_IN:
         nvmeStatus = NvmeScsiCmd_DoReadCapacity16(ctrlr, vmkCmd, ns);
         break;
      case VMK_SCSI_CMD_MODE_SENSE10:
      case VMK_SCSI_CMD_MODE_SENSE:
         nvmeStatus = NvmeScsiCmd_DoModeSense(ctrlr, vmkCmd, ns);
         break;
      case VMK_SCSI_CMD_LOG_SENSE:
         nvmeStatus = NvmeScsiCmd_DoLogSense(ctrlr, vmkCmd, ns);
         break;
      case VMK_SCSI_CMD_TEST_UNIT_READY:
         nvmeStatus = NvmeScsiCmd_DoTUR(ctrlr, vmkCmd, ns);
         break;
      case VMK_SCSI_CMD_RESERVE_UNIT:
      case VMK_SCSI_CMD_RELEASE_UNIT:
      case VMK_SCSI_CMD_VERIFY:
      case VMK_SCSI_CMD_START_UNIT:
         vmkCmd->bytesXferred = 0;
         nvmeStatus           = NVME_STATUS_SUCCESS;
         break;
      case VMK_SCSI_CMD_UNMAP:
         nvmeStatus = NvmeScsiCmd_DoUnmap(ctrlr, vmkCmd, ns);
         break;
      case VMK_SCSI_CMD_SYNC_CACHE:
         nvmeStatus = NvmeScsiCmd_DoSyncCache(ctrlr, cmdPtr, ns);
         break;
      default:
         vmkCmd->bytesXferred = 0;
         nvmeStatus           = NVME_STATUS_INVALID_OPCODE;
         break;
   }

out_return:
   if (nvmeStatus == NVME_STATUS_WOULD_BLOCK) {
      /**
       * The command has been submitted to nvme core and would be completed
       * asynchronously
       */
      vmkStatus = VMK_OK;
   } else {
      /**
       * The command has completed and needs to be completed inline.
       */
      vmkStatus = NvmeScsiCmd_SetReturnStatus(cmdPtr, nvmeStatus);
      if (vmkStatus == VMK_OK) {
#if NVME_MUL_COMPL_WORLD
         OsLib_IOCOmpletionEnQueue(ctrlr, vmkCmd);
#else
         SCSI_CMD_INVOKE_COMPLETION_CB(cmdPtr);
#endif
      }
   }

   return vmkStatus;
}


/**
 * Handle SCSI task management request
 *
 * @param [in]  clientData pointer to the controller instance
 * @param [in]  taskMgmt pointer to the task management request
 * @param [in]  deviceData pointer to the namespace
 */
VMK_ReturnStatus
ScsiTaskMgmt(void *clientData, vmk_ScsiTaskMgmt *taskMgmt, void *deviceData)
{
   VMK_ReturnStatus vmkStatus;
#if (EXC_HANDLER == 0)
   Nvme_ResetType resetType;
#endif
   struct NvmeCtrlr *ctrlr = (struct NvmeCtrlr *)clientData;
   struct NvmeNsInfo *ns = (struct NvmeNsInfo *)deviceData;

   VPRINT("taskMgmt: %s status %02x:%02x:%02x I:%p SN:0x%lx W:%d.",
          vmk_ScsiGetTaskMgmtTypeName(taskMgmt->type), taskMgmt->status.host,
          taskMgmt->status.device, taskMgmt->status.plugin, taskMgmt->cmdId.initiator,
          taskMgmt->cmdId.serialNumber, taskMgmt->worldId);

   /**
    * Task managements should be serialized.
    */
   vmk_SemaLock(&ctrlr->taskMgmtMutex);


#if EXC_HANDLER
   vmk_Memcpy(&(ctrlr->taskMgmtExcArgs.taskMgmt), taskMgmt, sizeof(vmk_ScsiTaskMgmt));
   ctrlr->taskMgmtExcArgs.ns = ns;
#endif


   switch(taskMgmt->type) {
      case VMK_SCSI_TASKMGMT_ABORT:
         #if (NVME_ENABLE_EXCEPTION_STATS == 1)
            STATS_Increment(ctrlr->statsData.TMAbortReq);
         #endif
#if EXC_HANDLER
         vmkStatus = NvmeExc_SignalExceptionAndWait(ctrlr,  NVME_EXCEPTION_TM_ABORT,  TASKMGMT_TIMEOUT);
#else
         vmkStatus = NvmeCtrlr_DoTaskMgmtAbort(ctrlr, taskMgmt, ns);
#endif
         break;
      case VMK_SCSI_TASKMGMT_VIRT_RESET:
         #if (NVME_ENABLE_EXCEPTION_STATS == 1)
            STATS_Increment(ctrlr->statsData.TMVirtResets);
         #endif
#if EXC_HANDLER
         vmkStatus = NvmeExc_SignalExceptionAndWait(ctrlr,  NVME_EXCEPTION_TM_VIRT_RESET,  TASKMGMT_TIMEOUT);
#else
         vmkStatus = NvmeCtrlr_DoTaskMgmtAbort(ctrlr, taskMgmt, ns);
#endif
         break;
      case VMK_SCSI_TASKMGMT_LUN_RESET:
         #if (NVME_ENABLE_EXCEPTION_STATS == 1)
            STATS_Increment(ctrlr->statsData.TMLunResets);
         #endif
#if EXC_HANDLER
         vmkStatus = NvmeExc_SignalExceptionAndWait(ctrlr,  NVME_EXCEPTION_TM_LUN_RESET,  TASKMGMT_TIMEOUT);
#else
         resetType = NVME_TASK_MGMT_LUN_RESET;
         vmkStatus = NvmeCtrlr_DoTaskMgmtReset(ctrlr, resetType, ns->id);
#endif
         break;
      case VMK_SCSI_TASKMGMT_DEVICE_RESET:
         #if (NVME_ENABLE_EXCEPTION_STATS == 1)
            STATS_Increment(ctrlr->statsData.TMDeviceResets);
         #endif
#if EXC_HANDLER
         vmkStatus = NvmeExc_SignalExceptionAndWait(ctrlr,  NVME_EXCEPTION_TM_DEVICE_RESET,  TASKMGMT_TIMEOUT);
#else
         resetType = NVME_TASK_MGMT_DEVICE_RESET;
         vmkStatus = NvmeCtrlr_DoTaskMgmtReset(ctrlr, resetType, ns->id);
#endif
         break;
      case VMK_SCSI_TASKMGMT_BUS_RESET:
         #if (NVME_ENABLE_EXCEPTION_STATS == 1)
            STATS_Increment(ctrlr->statsData.TMBusResets);
         #endif
#if EXC_HANDLER
         vmkStatus = NvmeExc_SignalExceptionAndWait(ctrlr,  NVME_EXCEPTION_TM_BUS_RESET,  TASKMGMT_TIMEOUT);
#else
         resetType = NVME_TASK_MGMT_BUS_RESET,
         vmkStatus = NvmeCtrlr_DoTaskMgmtReset(ctrlr, resetType, ns->id);
#endif
         break;
      default:
         EPRINT("Invalid task management type: 0x%x.", taskMgmt->type);
         VMK_ASSERT(0);
         vmkStatus = VMK_BAD_PARAM;
         break;
   }

   vmk_SemaUnlock(&ctrlr->taskMgmtMutex);

   VPRINT("vmkStatus = %x", vmkStatus);
   return vmkStatus;
}

/**
 * Do SCSI target discovery
 *
 * @param [in]  clientData pointer to the controller instance
 * @param [in]  action target discovery action
 * @param [in]  channel channel number
 * @param [in]  targetId target id
 * @param [in]  lunId lun id
 * @param [out] deviceData pointer to the device data
 */
VMK_ReturnStatus
ScsiDiscover(void *clientData, vmk_ScanAction action, int channel, int targetId,
   int lunId, void **deviceData)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeCtrlr *ctrlr = (struct NvmeCtrlr *)clientData;
   vmk_ListLinks *itemPtr;
   struct NvmeNsInfo *itr, *ns;

   DPRINT_NS("enter, c:%d, t:%d, l:%d, act: 0x%x", channel, targetId, lunId, action);

   VMK_ASSERT(channel == 0 && targetId == 0);

   switch (action) {
      case VMK_SCSI_SCAN_CREATE_PATH:
         /**
          * TODO: Rescan namespaces here.
          */
         ns = NULL;
         VMK_LIST_FORALL(&ctrlr->nsList, itemPtr) {
            itr = VMK_LIST_ENTRY(itemPtr, struct NvmeNsInfo, list);
            /* Namespace id starts from 1. NSID 1 maps to LUN 0 */
            if ((itr->id - 1) == lunId) {
               /* Found matching namespace */
               ns = itr;
               break;
            }
         }

         /* Return NO_CONNECT if target NS not found */
         if (!ns) {
            DPRINT_NS("No ns found for C%d:T%d:L%d.", channel, targetId, lunId);
            return VMK_NO_CONNECT;
         }

         NvmeCtrlr_GetNs(ns);

         vmkStatus = NvmeCore_ValidateNs(ns);
         if (vmkStatus != VMK_OK) {
            EPRINT("Namespace %d not supported.", ns->id);
            NvmeCtrlr_PutNs(ns);
            *deviceData = NULL;
            vmkStatus = VMK_NOT_READY; //<TBD> changing here to VMK_NOT_READY for CYCTWO-1016 workaround.
            return vmkStatus;
         }

         *deviceData = ns;
         vmkStatus = VMK_OK;
         break;

      case VMK_SCSI_SCAN_CONFIGURE_PATH:
         vmkStatus = VMK_OK;
         break;

      case VMK_SCSI_SCAN_DESTROY_PATH:
         /**
          * Release the ns
          */
         ns = (struct NvmeNsInfo *)(*deviceData);
         NvmeCtrlr_PutNs(ns);
         *deviceData = NULL;

         vmkStatus = VMK_OK;
         break;

      default:
         VMK_ASSERT(0);
         vmkStatus = VMK_BAD_PARAM;
         break;
   }

   return vmkStatus;
}


/**
 * Check if the specified target exists on the adapter and channel specified.
 *
 * @param [in]  clientData handle to the adapter
 * @param [in]  channel the channel to check for target on
 * @param [in]  targetId the target Id to check
 *
 * @return VMK_OK Success - target exists
 * @return VMK_FAILURE Target does not exist
 */
VMK_ReturnStatus
ScsiCheckTarget(void *clientData, int channel, int targetId)
{
   DPRINT_NS("enter, c:%d, t: %d.", channel, targetId);
   return (channel == 0 && targetId == 0) ? VMK_OK : VMK_FAILURE;
}


/**
 * Queue a SCSI command during a core dump on the adapter and LUN specified.
 *
 * Perform the command passed in 'cmd' on the adapter passed in
 * 'clientData' on the LUN passed in 'deviceData' during a core dump.
 *
 * @param [in]  clientData  Handle to the adapter to perform the discover action on.
 * @param [in]  cmd   Pointer to the command to execut.
 * @param [in]  deviceData  Pointer to deviceData returned by 'discover' Create.
 */
VMK_ReturnStatus
ScsiDumpCommand(void *clientData, vmk_ScsiCommand *vmkCmd, void *deviceData)
{
   return ScsiCommand(clientData, vmkCmd, deviceData);
}


/**
 * Log the current adapter queue.
 *
 * @param [in]  clientData  Handle to the adapter.
 */
void
ScsiDumpQueue(void *clientData)
{
   DPRINT_TEMP("enter");
   return;
}


/**
 * Run the adapter's poll handler, called on the dump device during a system dump.
 *
 * @param [in] clientData  Handle to the adapter.
 * @param [in] dumpPollHandlerData  Argument passed to dumpPollHandler
 */
void
ScsiDumpPollHandler(void *clientData)
{
   struct NvmeCtrlr  *ctrlr = (struct NvmeCtrlr *)clientData;
   struct NvmeQueueInfo *qinfo;
   int                   i;

   for (i = 0; i < ctrlr->numIoQueues; i++) {
      qinfo = &ctrlr->ioq[i];
      LOCK_FUNC(qinfo);
      nvmeCoreProcessCq(qinfo, 1);
      UNLOCK_FUNC(qinfo);
   }

   return;
}


/**
 * Driver specific ioctl
 *
 * Deprecated.
 */
VMK_ReturnStatus
ScsiIoctl(void *clientData, void *deviceData, vmk_uint32 fileFlags,
   vmk_uint32 cmd, vmk_VA userArgsPtr, vmk_IoctlCallerSize callerSize,
   vmk_int32 *drvEr)
{
   DPRINT_TEMP("enter");
   return VMK_OK;
}

/**
 * Return the current path queue depth on LUN specified.
 *
 * @param [in]  clientData  Handle to the adapter.
 * @param [in]  deviceData  LUN pointer returned by 'discover' Create.
 *
 * @return 0 if not a valid path.
 * @return the current queue depth for the path.
 */
int
ScsiQueryDeviceQueueDepth(void *clientData, void *deviceData)
{
   struct NvmeCtrlr *ctrlr = (struct NvmeCtrlr *)clientData;

   DPRINT_TEMP("enter");

   return ctrlr->qDepth;
}


/**
 * Close callback
 *
 * Deprecated.
 */
void
ScsiClose(void *clientData)
{
   DPRINT_TEMP("enter");
   return;
}


/**
 * Proc info
 *
 * Deprecated.
 */
VMK_ReturnStatus
ScsiProcInfo(void *clientData, char *buf, vmk_ByteCountSmall offset,
   vmk_ByteCountSmall count, vmk_ByteCountSmall *nbytes, int isWrite)
{
   DPRINT_TEMP("enter");
   return VMK_OK;
}


/**
 * Modify path queue depth on LUN specified if possible.
 *
 * @param [in]  clientData  Handle to the adapter.
 * @param [in]  qDepth   New queue depth to set.
 * @param [in]  deviceData  LUN pointer returned by 'discover' Create.
 *
 * @return the new queue depth in effect, which could be lower than the depth
 *         actually requested if the driver cannot honor the requested (higher)
 *         queue depth.
 * @return 0 if not a valid path.
 */
int
ScsiModifyDeviceQueueDepth(void *clientData, int qDepth, void *deviceData)
{
   DPRINT_TEMP("enter");
   return qDepth;
}
