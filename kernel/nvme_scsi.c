/******************************************************************************
 * Copyright 2013 VMware, Inc. All rights reserved.
 * ****************************************************************************/
/*-
 * Copyright (C) 2012 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * @file: nvme_scsi.c --
 *
 *    Translate between SCSI and NVMe
 */

#include "nvme_private.h"
#include "nvme_debug.h"
#include "nvme_scsi_cmds.h"


/*
 * TODO: this file needs to be reflacted.
 */


#define SCSI_MAX_LUNS (255)


#define SCSI_ASC_LBA_OUT_OF_RANGE   (0x21)

/**
 * NvmeScsiCmd_SetReturnStatus - refer to header file.
 */
VMK_ReturnStatus
NvmeScsiCmd_SetReturnStatus(vmk_ScsiCommand *vmkCmd, Nvme_Status nvmeStatus)
{
   VMK_ReturnStatus  vmkStatus = VMK_OK;
   vmk_ScsiSenseData senseData;
   vmk_Bool          senseValid = VMK_FALSE;
   int               senseKey, senseAsc, senseAscq;
   int               hostStatus, deviceStatus;

#if NVME_DEBUG
   if (VMK_UNLIKELY((nvme_dbg & NVME_DEBUG_DUMP_CE) || nvmeStatus)) {
      Nvme_LogDebug("Complete vmkCmd %p[%Xh I:%p SN:0x%lx] xfer: %d/%d "
                    "status 0x%x, %s.",
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

      vmk_ScsiCmdSetSenseData(&senseData, vmkCmd, sizeof(senseData));
   }

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
   int count;
   vmk_ListLinks *itemPtr;
   struct NvmeNsInfo *nsInfo;

   vmk_Memset(&response_data, 0, sizeof(response_data));

   count = 0;

   VMK_LIST_FORALL(&ctrlr->nsList, itemPtr) {
      nsInfo = VMK_LIST_ENTRY(itemPtr, struct NvmeNsInfo, list);
      if (nsInfo->blockCount != 0) {
         response_data.lunList[count].addrmethod = 0; /* peripheral device addressing method */
         response_data.lunList[count].busid      = 0;
         response_data.lunList[count++].lunid    = nsInfo->id - 1;
         Nvme_LogDebug("lun %d found, capacity %ld.", nsInfo->id - 1, nsInfo->blockCount);
      } else {
         Nvme_LogDebug("empty lun %d found, skipping.", nsInfo->id);
      }
   }

   response_data.lunListLength = vmk_CPUToBE32(count * 8);

   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, sizeof(response_data));
   vmkCmd->bytesXferred = sizeof(vmk_uint32) + sizeof(vmk_uint32) + count * 8;

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

   vmk_Memset(&response_data, 0, sizeof(response_data));

   response_data.ansi = 0x6; /* SPC-4 */
   response_data.dataformat = 0x2; /* SPC-4 */
   response_data.optlen = 0x1f;
   response_data.protect = END2END_DSP_TYPE(ns->dataProtSet) == 0 ? 0 : 1; /* calculated by Identify Namespace Data */
   vmk_Memcpy(response_data.manufacturer, "NVMe    ", sizeof(response_data.manufacturer));
   vmk_Memcpy(response_data.product, ctrlr->model, sizeof(response_data.product));
   vmk_Memcpy(response_data.revision, ctrlr->firmwareRev, sizeof(response_data.revision));

   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, sizeof(response_data));

   vmkCmd->bytesXferred = sizeof(response_data);

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

   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, sizeof(response_data));
   vmkCmd->bytesXferred = sizeof(response_data);

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
   char buffer[MAX_SERIAL_NUMBER_LENGTH + 1]; /* including trailing '\0' */

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

      Nvme_LogDebug("Generated serial number string: %s.", buffer);
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
         Nvme_LogError("Samsung pre-production controller detected, "
                       "skip SCSI INQUIRY VPD 80.");
         return NVME_STATUS_INVALID_FIELD_IN_CDB;
      }

      vmk_Memcpy(response_data.serialNumber, ctrlr->serial,
                 sizeof(response_data.serialNumber));
      Nvme_LogDebug("Serial number string: %s.", ctrlr->serial);
   }

   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, sizeof(response_data));
   vmkCmd->bytesXferred = sizeof(response_data);

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
         Nvme_LogError("Samsung pre-production controller detected, "
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

   vmk_Memset(&response_data, 0, sizeof(response_data));

   nvmeStatus = ScsiGenerateT10VPD(&response_data, ctrlr, ns, &length);

   if (SUCCEEDED(nvmeStatus)) {
      vmk_SgCopyTo(vmkCmd->sgArray, &response_data, length);
      vmkCmd->bytesXferred = length;
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

   VMK_ASSERT_ON_COMPILE(sizeof(response_data.payload) == 0x3C);

   vmk_Memset(&response_data, 0, sizeof(response_data));

   response_data.header.devclass = VMK_SCSI_CLASS_DISK;
   response_data.header.pqual = VMK_SCSI_PQUAL_CONNECTED;
   response_data.header.pagecode = SCSI_INQUIRY_B0H;
   response_data.header.payloadLen = vmk_CPUToBE16(sizeof(response_data.payload));

   response_data.payload.maxUnmabLbaCount = vmk_CPUToBE32((vmk_uint32) - 1);

   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, sizeof(response_data));
   vmkCmd->bytesXferred = sizeof(response_data);

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

   VMK_ASSERT_ON_COMPILE(sizeof(response_data.payload) == 0x3C);

   vmk_Memset(&response_data, 0, sizeof(response_data));

   response_data.header.devclass = VMK_SCSI_CLASS_DISK;
   response_data.header.pqual = VMK_SCSI_PQUAL_CONNECTED;
   response_data.header.pagecode = SCSI_INQUIRY_B1H;
   response_data.header.payloadLen = sizeof(response_data.payload);

   response_data.payload.rotationRate = vmk_CPUToBE16(0x1); /* is SSD */
   response_data.payload.formFactor = 0; /* form factor not reported */

   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, sizeof(response_data));
   vmkCmd->bytesXferred = sizeof(response_data);

   return NVME_STATUS_SUCCESS;
}


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

   if (inquiryCmd->evpd) {
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
   } else {
      nvmeStatus = NvmeScsiCmd_DoInquiryStd(ctrlr, vmkCmd, ns);
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
NvmeScsiCmd_DoIO(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd, struct NvmeNsInfo *ns)
{
   VMK_ASSERT(vmkCmd->lba + vmkCmd->lbc <= ns->blockCount);
   return NvmeIo_SubmitIo(ns, vmkCmd);
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
   vmk_uint8   reserved2   :6,
   /** logical block provisioning read zeros */
               lbprz       :1,
   /** logical block provisioning management enabled */
               lbpme       :1;
   vmk_uint8   reserved3;
   vmk_uint8   reserved4[16];
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
   vmk_ScsiReadCap16Command *cdb = (vmk_ScsiReadCap16Command *)vmkCmd->cdb;
   nvme_ScsiReadCapacity16Response response_data;

   if (cdb->sa != VMK_SCSI_SAI_READ_CAPACITY16) {
      return NVME_STATUS_INVALID_FIELD_IN_CDB;
   }

   vmk_Memset(&response_data, 0, sizeof(response_data));

   response_data.lbn = vmk_CPUToBE64(ns->blockCount - 1);
   response_data.blocksize = vmk_CPUToBE32(1 << ns->lbaShift);

   Nvme_LogDebug("ns: %d, blockCount: %ld, lbaShift: %d, fmtLbaSize: %d, metaDataCap: %d, "
      "dataProtCap: %d, dataProtSet: %d, metasize: %d.",
      ns->id, ns->blockCount, ns->lbaShift,
      ns->fmtLbaSize, ns->metaDataCap, ns->dataProtCap, ns->dataProtSet, ns->metasize);

   response_data.protEnable = END2END_DSP_TYPE(ns->dataProtSet) == 0 ? 0 : 1;
   /* 000b -> unspecified; 001b -> 000b; 010b -> 001b; 011b -> 010b */
   response_data.protType = END2END_DSP_TYPE(ns->dataProtSet) - 1;
   response_data.lbpme = (ns->feature & 0x1) ? 1 : 0;
   /**
    * Note: we require lbpme to be set to 1 to issue UNMAP/DSM to the device.
    */
   response_data.lbpme = 1;
   response_data.lbprz = 0;

   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, sizeof(response_data));
   vmkCmd->bytesXferred = sizeof(response_data);

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
   vmk_uint8 param;
   /** BLOCK DESCRIPTOR LENGTH */
   vmk_uint8 blockDescriptorLen;
} VMK_ATTRIBUTE_PACKED nvme_ScsiModeSenseHeader6;


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



/** Caching mode page code */
#define NVME_SCSI_MS_PAGE_CACHE    (0x08)
/** Control mode page code */
#define NVME_SCSI_MS_PAGE_CONTROL  (0x0A)
/** Power Condition mode page code */
#define NVME_SCSI_MS_PAGE_PC       (0x1A)
/** Return all pages page code */
#define NVME_SCSI_MS_PAGE_ALL      (0x3F)


/** Caching mode page size */
#define NVME_SCSI_MS_PAGE_CACHE_SZ     (0x12)
/** Control mode page size */
#define NVME_SCSI_MS_PAGE_CONTROL_SZ   (0x0A)
/** Power Condition mode page size */
#define NVME_SCSI_MS_PAGE_PC_SZ        (0x26)


/**
 * Check at compile time for the data structure size of the cache mode pages
 */
VMK_ASSERT_LIST(modeSenseSizeChecker,
   VMK_ASSERT_ON_COMPILE(sizeof(nvme_ScsiModeSenseCachingPage) == (NVME_SCSI_MS_PAGE_CACHE_SZ + 2));
   VMK_ASSERT_ON_COMPILE(sizeof(nvme_ScsiModeSenseControlPage) == (NVME_SCSI_MS_PAGE_CONTROL_SZ + 2));
   VMK_ASSERT_ON_COMPILE(sizeof(nvme_ScsiModeSensePCPage) == (NVME_SCSI_MS_PAGE_PC_SZ + 2));
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
   struct
   {
      nvme_ScsiModeSenseHeader6 header;
      nvme_ScsiModeSenseCachingPage caching;
   } response_data;

   vmk_Memset(&response_data, 0, sizeof(response_data));

   response_data.header.dataLen = sizeof(response_data) - 1;
   response_data.header.blockDescriptorLen = 0;
   response_data.caching.pageCode = NVME_SCSI_MS_PAGE_CACHE;
   response_data.caching.pageLen = NVME_SCSI_MS_PAGE_CACHE_SZ;

   /*
    * TODO: Acquire Volatile Write Cache Feature via GetFeatures, and assign
    * the value to WCE.
    */
   response_data.caching.wce = 0;

   /* Sanity check that the command carries SG buffer with adequate size */
   VMK_ASSERT(vmk_SgGetDataLen(vmkCmd->sgArray) >= sizeof(response_data));

   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, sizeof(response_data));
   vmkCmd->bytesXferred = sizeof(response_data);

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
   struct
   {
      nvme_ScsiModeSenseHeader6 header;
      nvme_ScsiModeSenseControlPage control;
   } response_data;

   vmk_Memset(&response_data, 0, sizeof(response_data));

   response_data.header.dataLen = sizeof(response_data) - 1;
   response_data.header.blockDescriptorLen = 0;
   response_data.control.pageCode = NVME_SCSI_MS_PAGE_CONTROL;
   response_data.control.pageLen = NVME_SCSI_MS_PAGE_CONTROL_SZ;

   /**
    * Shall be set to 0b if application client reserves this field otherwise
    * shall be set to 1b indicating protection information disabled when
    * RDPROTECT field is zero.
    */
   response_data.control.dpicz = 1;

   /**
    * Note: not comply to the NVM-Express-SCSI-Translation Reference.
    *
    * Descriptor Sense is not currently supported, so set D_SENSE to 0
    */
   response_data.control.dSense = 0;

   /**
    * Shall be set to 1b indicating that the logical unit does not implicitly
    * save log parameters.
    */
   response_data.control.gltsd = 1;

   /**
    * Shall be set to one indicating commands may be reordered.
    */
   response_data.control.qam = 1;

   /**
    * Note: not comply to the NVM-Express-SCSI-Translation Reference.
    *
    * We need to set qerr to 0 to support DSM (UNMAP).
    */
   response_data.control.qerr = 0;

   /**
    * Shall be set to one indicating that aborted commands shall be completed
    * with TASK ABORTED status.
    */
   response_data.control.tas = 1;

   /**
    *Shall be set to FFFFh indicating unlimited busy timeout.
    */
   response_data.control.busyTimeoutPeriod = 0xffff;

   /* Sanity check that the command carries SG buffer with adequate size */
   VMK_ASSERT(vmk_SgGetDataLen(vmkCmd->sgArray) >= sizeof(response_data));

   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, sizeof(response_data));
   vmkCmd->bytesXferred = sizeof(response_data);

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
   struct {
      nvme_ScsiModeSenseHeader6 header;
      nvme_ScsiModeSensePCPage pc;
   } response_data;

   vmk_Memset(&response_data, 0, sizeof(response_data));

   response_data.header.dataLen = sizeof(response_data) - 1;
   response_data.header.blockDescriptorLen = 0;
   response_data.pc.pageCode = NVME_SCSI_MS_PAGE_PC;
   response_data.pc.pageLen = NVME_SCSI_MS_PAGE_PC_SZ;

   /**
    * PM_BG_PRECENDENCE:
    * Shall be set to 00H, indicating vender specific power management and
    * background interactions.
    */

   /**
    * Timers are not supported in NVM Express. When processing a MODE SENSE
    * command, these fields shall be returned as zero.
    */

   /* Sanity check that the command carries SG buffer with adequate size */
   VMK_ASSERT(vmk_SgGetDataLen(vmkCmd->sgArray) >= sizeof(response_data));

   vmk_SgCopyTo(vmkCmd->sgArray, &response_data, sizeof(response_data));
   vmkCmd->bytesXferred = sizeof(response_data);

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
   return NVME_STATUS_INVALID_FIELD_IN_CDB;
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
      case NVME_SCSI_MS_PAGE_ALL:
         nvmeStatus = NvmeScsiCmd_DoModeSenseReturnAll(ctrlr, vmkCmd, ns);
         break;
      default:
         nvmeStatus = NVME_STATUS_INVALID_FIELD_IN_CDB;
         break;
   }

   return nvmeStatus;
}


/**
 * Handle SCSI Log Sense command
 *
 * @param [in]  ctrlr pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  ns pointer to the namespace
 */
static Nvme_Status
NvmeScsiCmd_DoLogSense(struct NvmeCtrlr *ctrlr, vmk_ScsiCommand *vmkCmd,
                       struct NvmeNsInfo *ns)
{
   /* TODO: this needs to be implemented later. */
   vmkCmd->bytesXferred = 0;

   return NVME_STATUS_INVALID_OPCODE;
}


#define TUR_TIMEOUT  (1000 * 1000)

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

   return NVME_STATUS_OK;
}


/**
 * UNMAP block descriptor
 *
 * sbc3r35, table 98
 */
typedef struct {
   /** UNMAP LOGICAL BLOCK ADDRESS */
   vmk_uint64 unmapLba;
   /** NUMBER OF LOGICAL BLOCKS */
   vmk_uint32 numBlocks;
   /** Reserved */
   vmk_uint32 reserved;
} VMK_ATTRIBUTE_PACKED nvme_ScsiUnmapBlockDescriptor;


/**
 * Maximum number of DSM ranges
 */
#define NVME_MAX_DSM_RANGE (256)


/**
 * Unmap parameter list
 *
 * sbc3r35, table 97
 */
typedef struct {
   /** UNMAP DATA LENGTH (n - 1) */
   vmk_uint16 unmapDataLen;
   /** UNMAP BLOCK DESCRIPTOR DATA LENGTH (n-7) */
   vmk_uint16 unmapBlockDescriptorDataLen;
   /** Reserved */
   vmk_uint32 reserved;
   /** UNMAP block descriptor list */
   nvme_ScsiUnmapBlockDescriptor unmapBlockDescriptorList[NVME_MAX_DSM_RANGE];
} VMK_ATTRIBUTE_PACKED nvme_ScsiUnmapParameterList;


/**
 * UNMAP command
 *
 * sbc3r35, table 96
 */
typedef struct {
   /** OPERATION CODE (42h) */
   vmk_uint8  opcode;
   /** ANCHOR */
   vmk_uint8  anchor     : 1;
   /** Reserved */
   vmk_uint8  reserved1  : 7;
   /** Reserved */
   vmk_uint8  reserved2[4];
   /** GROUP NUMBER */
   vmk_uint8  groupNum   : 5;
   /** Reserved */
   vmk_uint8  reserved3  : 3;
   /** PARAMETER LIST LENGTH */
   vmk_uint16 parameterListLen;
   /** CONTROL */
   vmk_uint8  control;
} VMK_ATTRIBUTE_PACKED nvme_ScsiUnmapCommand;


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
   nvme_ScsiUnmapParameterList unmapParamList;
   /** This is a temporary buffer to hold SCSI UNMAP -> DSM Ranges translation */
   struct nvme_dataset_mgmt_data dsmData[NVME_MAX_DSM_RANGE];
   int i, count;

   DPRINT2("Unmap cmd %p: anchor: %d, groupNum: %d, paramListLen: %d.",
      vmkCmd, cdb->anchor, cdb->groupNum, vmk_BE16ToCPU(cdb->parameterListLen));

   vmkStatus = vmk_SgCopyFrom(&unmapParamList, vmkCmd->sgArray,
      min_t(vmk_ByteCount, sizeof(unmapParamList), vmk_SgGetDataLen(vmkCmd->sgArray)));
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("failed to acquire unmap parameter lists.");
      VMK_ASSERT(vmkStatus == VMK_OK);
      return NVME_STATUS_INVALID_FIELD_IN_CDB;
   }

   DPRINT2("Unmap cmd %p: unmapDataLen %d, unmapBlockDescriptorDataLen %d.",
      vmkCmd, vmk_BE16ToCPU(unmapParamList.unmapDataLen),
      vmk_BE16ToCPU(unmapParamList.unmapBlockDescriptorDataLen));

   /*
    * Translate UNMAP block descriptor list to DSM ranges.
    *
    * Note that the DSM ranges buffer is temporary on the stack; when this
    * buffer is passed to NVM layer, the NVM layer should keep its local
    * copy instead of using it as a persistent store.
    */
   vmk_Memset(&dsmData, 0, sizeof(dsmData));
   count = vmk_BE16ToCPU(unmapParamList.unmapBlockDescriptorDataLen)/sizeof(nvme_ScsiUnmapBlockDescriptor);
   if (count >= NVME_MAX_DSM_RANGE) {
      Nvme_LogError("invalid unmap parameter for cmd %p: %d ranges provided (dataLen %d, blockDescriptorLen %d).",
         vmkCmd, count, vmk_BE16ToCPU(unmapParamList.unmapDataLen),
         vmk_BE16ToCPU(unmapParamList.unmapBlockDescriptorDataLen));
      VMK_ASSERT(0);
      return NVME_STATUS_INVALID_FIELD_IN_CDB;
   }

   for (i = 0; i < count; i++) {
      dsmData[i].startLBA = vmk_BE64ToCPU(unmapParamList.unmapBlockDescriptorList[i].unmapLba);
      dsmData[i].numLBA = vmk_BE32ToCPU(unmapParamList.unmapBlockDescriptorList[i].numBlocks);
      DPRINT2("Unmap cmd %p: %d/%d, lba 0x%lx, lbc %d.",
         vmkCmd, i, count, dsmData[i].startLBA, dsmData[i].numLBA);
   }

   return NvmeIo_SubmitDsm(ns, vmkCmd, dsmData, count);
}


/**
 * Queue SCSI command
 *
 * @param [in]  clientData pointer to the controller instance
 * @param [in]  vmkCmd pointer to the scsi command
 * @param [in]  deviceData pointer to the namespace
 */
static VMK_ReturnStatus
ScsiCommand(void *clientData, vmk_ScsiCommand *vmkCmd, void *deviceData)
{
   struct NvmeCtrlr  *ctrlr = (struct NvmeCtrlr *)clientData;
   struct NvmeNsInfo *ns = (struct NvmeNsInfo *)deviceData;
   VMK_ReturnStatus   vmkStatus;
   Nvme_Status        nvmeStatus;
   Nvme_CtrlrState    state;

#if NVME_DEBUG
   if (nvme_dbg & NVME_DEBUG_DUMP) {
      NvmeDebug_DumpCdb(vmkCmd->cdb);
   }
#endif

   state = NvmeState_GetCtrlrState(ctrlr, VMK_TRUE);

   if (VMK_UNLIKELY(state > NVME_CTRLR_STATE_INRESET)) {
      /**
       * Ctrlr either missing, or in tear down path, or failed
       */
      Nvme_LogDebug("controller offline, %s.", NvmeState_GetCtrlrStateString(state));
      vmkCmd->bytesXferred = 0;
      nvmeStatus           = NVME_STATUS_FATAL_ERROR;
      goto out_return;
   } else if (VMK_UNLIKELY(state == NVME_CTRLR_STATE_INRESET)) {
      /**
       * Transient error
       */
      Nvme_LogDebug("controller in reset.");
      vmkCmd->bytesXferred = 0;
      nvmeStatus           = NVME_STATUS_IN_RESET;
      goto out_return;
   } else if (VMK_UNLIKELY(state != NVME_CTRLR_STATE_OPERATIONAL)) {
      Nvme_LogDebug("controller not in ready state, %s.", NvmeState_GetCtrlrStateString(state));
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
         nvmeStatus = NvmeScsiCmd_DoIO(ctrlr, vmkCmd, ns);
         break;
      case VMK_SCSI_CMD_READ_CAPACITY:
         nvmeStatus = NvmeScsiCmd_DoReadCapacity(ctrlr, vmkCmd, ns);
         break;
      case VMK_SCSI_CMD_SERVICE_ACTION_IN:
         nvmeStatus = NvmeScsiCmd_DoReadCapacity16(ctrlr, vmkCmd, ns);
         break;
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
      vmkStatus = NvmeScsiCmd_SetReturnStatus(vmkCmd, nvmeStatus);
      if (vmkStatus == VMK_OK) {
         NvmeScsiCmd_CompleteCommand(vmkCmd);
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
static VMK_ReturnStatus
ScsiTaskMgmt(void *clientData, vmk_ScsiTaskMgmt *taskMgmt, void *deviceData)
{
   VMK_ReturnStatus vmkStatus;
   Nvme_ResetType resetType;
   struct NvmeCtrlr *ctrlr = (struct NvmeCtrlr *)clientData;
   struct NvmeNsInfo *ns = (struct NvmeNsInfo *)deviceData;

   Nvme_LogVerb("taskMgmt: %s status %02x:%02x:%02x I:%p SN:0x%lx W:%d.",
      vmk_ScsiGetTaskMgmtTypeName(taskMgmt->type), taskMgmt->status.host,
      taskMgmt->status.device, taskMgmt->status.plugin, taskMgmt->cmdId.initiator,
      taskMgmt->cmdId.serialNumber, taskMgmt->worldId);

   switch(taskMgmt->type) {
      case VMK_SCSI_TASKMGMT_ABORT:
         // fall through
      case VMK_SCSI_TASKMGMT_VIRT_RESET:
         vmkStatus = NvmeCtrlr_DoTaskMgmtAbort(ctrlr, taskMgmt, ns);
         break;
      case VMK_SCSI_TASKMGMT_LUN_RESET:
         resetType = NVME_TASK_MGMT_LUN_RESET;
         vmkStatus = NvmeCtrlr_DoTaskMgmtReset(ctrlr, resetType, ns->id);
         break;
      case VMK_SCSI_TASKMGMT_DEVICE_RESET:
         resetType = NVME_TASK_MGMT_DEVICE_RESET;
         vmkStatus = NvmeCtrlr_DoTaskMgmtReset(ctrlr, resetType, ns->id);
         break;
      case VMK_SCSI_TASKMGMT_BUS_RESET:
         resetType = NVME_TASK_MGMT_BUS_RESET,
         vmkStatus = NvmeCtrlr_DoTaskMgmtReset(ctrlr, resetType, ns->id);
         break;
      default:
         Nvme_LogError("Invalid task management type: 0x%x.", taskMgmt->type);
         VMK_ASSERT(0);
         vmkStatus = VMK_BAD_PARAM;
         break;
   }

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
static VMK_ReturnStatus
ScsiDiscover(void *clientData, vmk_ScanAction action, int channel, int targetId,
   int lunId, void **deviceData)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeCtrlr *ctrlr = (struct NvmeCtrlr *)clientData;
   vmk_ListLinks *itemPtr;
   struct NvmeNsInfo *itr, *ns;

   Nvme_LogDebug("enter, c:%d, t:%d, l:%d, act: 0x%x", channel, targetId, lunId, action);

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
            Nvme_LogDebug("No ns found for C%d:T%d:L%d.", channel, targetId, lunId);
            return VMK_NO_CONNECT;
         }

         NvmeCtrlr_GetNs(ns);

         vmkStatus = NvmeCore_ValidateNs(ns);
         if (vmkStatus != VMK_OK) {
            Nvme_LogError("Namespace %d not supported.", ns->id);
            NvmeCtrlr_PutNs(ns);
            *deviceData = NULL;
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
static VMK_ReturnStatus
ScsiCheckTarget(void *clientData, int channel, int targetId)
{
   Nvme_LogDebug("enter, c:%d, t: %d.", channel, targetId);
   return (channel == 0 && targetId == 0) ? VMK_OK : VMK_FAILURE;
}


/**
 * Callback to notify when IO is allowed to adapter.
 *
 * @param [in]  logicalDevice Handle to the logical device.
 * @param [in]  ioAllowed VMK_TRUE if IO is allowed, VMK_FALSE if IO not allowed.
 */
static void
ScsiNotifyIOAllowed(vmk_Device logicalDevice, vmk_Bool ioAllowed)
{
   VMK_ReturnStatus vmkStatus;
   vmk_ScsiAdapter *adapter;
   struct NvmeCtrlr *ctrlr;

   Nvme_LogDebug("entry, ioAllowed %d.", ioAllowed);

   vmkStatus = vmk_DeviceGetRegistrationData(logicalDevice, (vmk_AddrCookie *)&adapter);
   if (vmkStatus != VMK_OK || adapter == NULL) {
      Nvme_LogError("failed to get logical device data, 0x%x.", vmkStatus);
      return;
   }
   ctrlr = (struct NvmeCtrlr *)adapter->clientData;

   if (ioAllowed) {
      vmkStatus = vmk_ScsiStartCompletionQueues(adapter, ctrlr->numIoQueues);
      if (vmkStatus == VMK_OK) {
         Nvme_LogInfo("started %d io queues.", ctrlr->numIoQueues);
      } else {
         Nvme_LogError("failed to start %d io queues, 0x%x.", ctrlr->numIoQueues, vmkStatus);
      }

      NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_OPERATIONAL, VMK_TRUE);

#if NVME_DEBUG_INJECT_STATE_DELAYS
      Nvme_LogInfo("--STARTED to OPERATIONAL--");
      vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   } else {
      NvmeState_SetCtrlrState(ctrlr, NVME_CTRLR_STATE_STARTED, VMK_TRUE);

#if NVME_DEBUG_INJECT_STATE_DELAYS
      Nvme_LogInfo("--OPERATIONAL to STARTED--");
      vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   }

   return;
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
static VMK_ReturnStatus
ScsiDumpCommand(void *clientData, vmk_ScsiCommand *vmkCmd, void *deviceData)
{
   Nvme_LogDebug("enter");
   return VMK_NO_CONNECT;
}


/**
 * Log the current adapter queue.
 *
 * @param [in]  clientData  Handle to the adapter.
 */
static void
ScsiDumpQueue(void *clientData)
{
   Nvme_LogDebug("enter");
   return;
}


/**
 * Run the adapter's poll handler, called on the dump device during a system dump.
 *
 * @param [in] clientData  Handle to the adapter.
 * @param [in] dumpPollHandlerData  Argument passed to dumpPollHandler
 */
static void
ScsiDumpPollHandler(void *clientData)
{
   Nvme_LogDebug("enter");
   return;
}


/**
 * Driver specific ioctl
 *
 * Deprecated.
 */
static VMK_ReturnStatus
ScsiIoctl(void *clientData, void *deviceData, vmk_uint32 fileFlags,
   vmk_uint32 cmd, vmk_VA userArgsPtr, vmk_IoctlCallerSize callerSize,
   vmk_int32 *drvEr)
{
   Nvme_LogDebug("enter");
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
static int
ScsiQueryDeviceQueueDepth(void *clientData, void *deviceData)
{
   struct NvmeCtrlr *ctrlr = (struct NvmeCtrlr *)clientData;

   Nvme_LogDebug("enter");

   return ctrlr->qDepth;
}


/**
 * Close callback
 *
 * Deprecated.
 */
static void
ScsiClose(void *clientData)
{
   Nvme_LogDebug("enter");
   return;
}


/**
 * Proc info
 *
 * Deprecated.
 */
static VMK_ReturnStatus
ScsiProcInfo(void *clientData, char *buf, vmk_ByteCountSmall offset,
   vmk_ByteCountSmall count, vmk_ByteCountSmall *nbytes, int isWrite)
{
   Nvme_LogDebug("enter");
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
static int
ScsiModifyDeviceQueueDepth(void *clientData, int qDepth, void *deviceData)
{
   Nvme_LogDebug("enter");
   return qDepth;
}


/**
 * SCSI DMA Engine constraints
 */
#define SCSI_ADDR_MASK (VMK_ADDRESS_MASK_64BIT)
#define SCSI_MAX_XFER (NVME_DRIVER_PROPS_MAX_PRP_LIST_ENTRIES * VMK_PAGE_SIZE)
#define SCSI_SG_MAX_ENTRIES (NVME_DRIVER_PROPS_MAX_PRP_LIST_ENTRIES)
#define SCSI_SG_ELEM_MAX_SIZE (0)
#define SCSI_SG_ELEM_SIZE_MULT (512)
/**
 * NVMe spec requires that the first PRP entry (DMA address of the first SG
 * element) to have last two bits as '0'.
 */
#define SCSI_SG_ELEM_ALIGNMENT (4)
#define SCSI_SG_ELEM_STRADDLE (VMK_ADDRESS_MASK_32BIT + 1)


/**
 * Initialize SCSI layer
 *
 * @param [in] ctrlr Handle to the controller instance
 *
 * @return VMK_OK if SCSI layer initialization completes successfully
 */
VMK_ReturnStatus
NvmeScsi_Init(struct NvmeCtrlr *ctrlr)
{
   VMK_ReturnStatus vmkStatus;
   vmk_ScsiAdapter *adapter;
   vmk_DMAConstraints scsiConstraints;
   vmk_DMAEngineProps scsiProps;

   Nvme_LogDebug("enter");

   /* TODO: Ideally the queue depth of a controller can hit as large
    * as io_cpl_queue_size * ctrlr->numIoQueues.
    */
   ctrlr->qDepth = io_cpl_queue_size * ctrlr->numIoQueues;

   /* Create a DMA engine for SCSI IO */
   scsiConstraints.addressMask = SCSI_ADDR_MASK;
   scsiConstraints.maxTransfer = SCSI_MAX_XFER;
   scsiConstraints.sgMaxEntries = SCSI_SG_MAX_ENTRIES;
   scsiConstraints.sgElemMaxSize = SCSI_SG_ELEM_MAX_SIZE;
   scsiConstraints.sgElemSizeMult = SCSI_SG_ELEM_SIZE_MULT;
   scsiConstraints.sgElemAlignment = SCSI_SG_ELEM_ALIGNMENT;
   scsiConstraints.sgElemStraddle = SCSI_SG_ELEM_STRADDLE;

   /* Override some of the parameters */
   scsiConstraints.sgMaxEntries = max_prp_list;

   vmk_NameFormat(&scsiProps.name, "%s-scsiDmaEngine", Nvme_GetCtrlrName(ctrlr));
   scsiProps.module = vmk_ModuleCurrentID;
   scsiProps.flags = 0;
   scsiProps.device = ctrlr->device;
   scsiProps.constraints = &scsiConstraints;
   scsiProps.bounce = NULL;

   vmkStatus = vmk_DMAEngineCreate(&scsiProps, &ctrlr->scsiDmaEngine);
   if (vmkStatus != VMK_OK) {
      return vmkStatus;
   }

   /* Now allocate and initialize scsi adapter */
   adapter = vmk_ScsiAllocateAdapter();
   if (!adapter) {
      Nvme_LogError("failed to allocate scsi adapter, out of memory.");
      vmk_DMAEngineDestroy(ctrlr->scsiDmaEngine);
      return VMK_NO_MEMORY;
   }

   vmk_NameInitialize(&adapter->driverName, NVME_DRIVER_NAME);

   adapter->device = ctrlr->device;
   adapter->hostMaxSectors = transfer_size * 1024 / VMK_SECTOR_SIZE;
   adapter->qDepthPtr = &ctrlr->qDepth;

   adapter->command = ScsiCommand;
   adapter->taskMgmt = ScsiTaskMgmt;
   adapter->dumpCommand = ScsiDumpCommand;
   adapter->close = ScsiClose;
   adapter->procInfo = ScsiProcInfo;
   adapter->dumpQueue = ScsiDumpQueue;
   adapter->dumpPollHandler = ScsiDumpPollHandler;
   adapter->ioctl = ScsiIoctl;
   adapter->discover = ScsiDiscover;
   adapter->modifyDeviceQueueDepth = ScsiModifyDeviceQueueDepth;
   adapter->queryDeviceQueueDepth = ScsiQueryDeviceQueueDepth;
   adapter->checkTarget = ScsiCheckTarget;

   adapter->moduleID = vmk_ModuleCurrentID;
   adapter->clientData = ctrlr;
   adapter->channels = 1;
   adapter->maxTargets = 1;
   adapter->targetId = -1;
   adapter->maxLUNs = max_namespaces;
   adapter->paeCapable = VMK_TRUE;
   adapter->maxCmdLen = NVME_DRIVER_PROPS_MAX_CMD_LEN;

   adapter->flags = VMK_SCSI_ADAPTER_FLAG_NO_PERIODIC_SCAN;

   /* TODO: create NVMe transport */
   adapter->mgmtAdapter.transport = VMK_STORAGE_ADAPTER_PSCSI;

   adapter->notifyIOAllowed = ScsiNotifyIOAllowed;
   adapter->engine = ctrlr->scsiDmaEngine;

   ctrlr->scsiAdapter = adapter;

   return VMK_OK;
}


/**
 * Tear down and free SCSI layer resources
 *
 * @param [in] ctrlr Handle to the controller instance
 *
 * @return VMK_OK if SCSI layer cleanup succeed
 */
VMK_ReturnStatus
NvmeScsi_Destroy(struct NvmeCtrlr *ctrlr)
{
   Nvme_LogDebug("enter");

   if (ctrlr->scsiAdapter) {
      vmk_ScsiFreeAdapter(ctrlr->scsiAdapter);
      ctrlr->scsiAdapter = NULL;
   }

   if (ctrlr->scsiDmaEngine) {
      vmk_DMAEngineDestroy(ctrlr->scsiDmaEngine);
   }

   return VMK_OK;
}
