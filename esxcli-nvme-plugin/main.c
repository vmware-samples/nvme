/******************************************************************************
 * Copyright (c) 2014-2015 VMware, Inc. All rights reserved.
 *****************************************************************************/

/**
 * @file: main.c
 *
 *    Entry for NVMe esxcli plug-in.
 */

#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <featureStateSwitch.h>

#include <vmkapi.h>
#include "esxcli_xml.h"
#include "nvme_lib.h"
#include "str.h"

//#define PLUGIN_DEBUG

#ifdef PLUGIN_DEBUG
#define Debug(fmt, ...) \
   do {                             \
      printf(fmt, ##__VA_ARGS__);   \
   } while (0)
#else
#define Debug(fmt, ...)
#endif

#define Error(fmt, ...) \
   do {                             \
      printf("ERROR: ");   \
      printf(fmt, ##__VA_ARGS__);   \
   } while (0)


#define PrintString(fmt, ...)      \
   do {                             \
      printf(fmt, ##__VA_ARGS__);   \
   } while (0)


typedef enum __bool {false = 0, true,} BOOL;
/* We assume the command and device name length is less than 150 and 100 */
#define MAX_CMD_LEN 150
#define MAX_DEV_NAME_LEN 100

#define MAX_ERROR_LOG_ENTRIES 64

static const char *nsStatusString [] = {
"Unallocated",
"Allocated",
"Inactive",
"Active",
};

static const char *nvmNsRelPerf[] = {
   "Best performance",
   "Better performance",
   "Good performance",
   "Degraded performance",
};

#define HEX2CHAR(n) ((n >= 10) ? (n - 10 + 'A') : (n + '0'))

static void
hexdumptoString(char *inbuff, int inlen, char *outbuff, int outlen)
{
   int k, n;
   vmk_uint8 *p = inbuff;
   int i = inlen - 1;

   while (i >=0 ) {
      if (p[i] != '\0') {
         break;
      }
      i --;
   }
   for (k = 0, n = 0; k <= i && n < (outlen - 1); k ++) {
      outbuff[n] = HEX2CHAR((p[k] >> 4));
      outbuff[n + 1] = HEX2CHAR((p[k] & 0xf));
      n += 2;
   }
   outbuff[n] = 0;
}

static int
refineASCIIString(char *p, int len)
{
   int i = len - 1;
   int unprintable = 0;

   while (i >=0 ) {
      if (p[i] == '\0' || p[i] == ' ') {
         p[i] = '\0';
      } else {
         break;
      }
      i --;
   }
   while (i >= 0) {
      if (p[i] == '\0') {
         p[i] = '_';
      }
      if (p[i] < 0x20 || p[i] == 0x7f) {
         p[i] = '?';
         unprintable = 1;
      }
      i --;
   }
   return unprintable;
}

static void
PrintIdentifyCtrlr(struct iden_controller *id)
{
   char* hexdumpbuff;
   char* readablebuff;
   int readbufflen;
   int hexbufflen;

   esxcli_xml_begin_output();
   xml_struct_begin("DeviceInfo");
   PINTS("PCIVID", id->pcieVID);
   PINTS("PCISSVID", id->pcieSSVID);
   xml_field_begin("Serial Number");
   printf("<string>%.20s</string>", id->serialNum);
   xml_field_end();
   xml_field_begin("Model Number");
   printf("<string>%.40s</string>", id->modelNum);
   xml_field_end();
   xml_field_begin("Firmware Revision");
   printf("<string>%.8s</string>", id->firmwareRev);
   xml_field_end();
   PINT("Recommended Arbitration Burst", id->arbBurstSize);
   xml_field_begin("IEEE OUI Identifier");
   printf("<string>%02x%02x%02x</string>", id->ieeeOui[2], id->ieeeOui[1],
          id->ieeeOui[0]);
   xml_field_end();
   PBOOL("Controller Associated with an SR-IOV Virtual Function", id->cmic.sriov);
   PBOOL("Controller Associated with a PCI Function", !id->cmic.sriov);
   PBOOL("NVM Subsystem May Contain Two or More Controllers", id->cmic.mulCtrlrs);
   PBOOL("NVM Subsystem Contains Only One Controller", !id->cmic.mulCtrlrs);
   PBOOL("NVM Subsystem May Contain Two or More PCIe Ports", id->cmic.mulPorts);
   PBOOL("NVM Subsystem Contains Only One PCIe Port", !id->cmic.mulPorts);
   PINT("Max Data Transfer Size", id->mdts);
   PINT("Controller ID", id->cntlId);
   xml_field_begin("Version");
   printf("<string>%d.%d</string>", id->ver.mjr, id->ver.mnr);
   xml_field_end();
   PINT("RTD3 Resume Latency", id->rtd3r);
   PINT("RTD3 Entry Latency", id->rtd3e);
   PBOOL("Optional Firmware Activation Event Support", id->oaes.fwActEvent);
   PBOOL("Optional Namespace Attribute Changed Event Support", id->oaes.nsChgEvent);
   PBOOL("Host Identifier Support", id->ctratt.hostId);
   PBOOL("Namespace Management and Attachment Support", id->adminCmdSup & 0x8);
   PBOOL("Firmware Activate and Download Support", id->adminCmdSup & 0x4);
   PBOOL("Format NVM Support", id->adminCmdSup & 0x2);
   PBOOL("Security Send and Receive Support", id->adminCmdSup & 0x1);
   PINT("Abort Command Limit", id->abortCmdLmt);
   PINT("Async Event Request Limit", id->asyncReqLmt);
   PBOOL("Firmware Activate Without Reset Support", id->firmUpdt & 0x10);
   PINT("Firmware Slot Number", (id->firmUpdt & 0xe) >> 1);
   PBOOL("The First Slot Is Read-only", id->firmUpdt & 0x1);
   PBOOL("Command Effects Log Page Support", id->logPgAttrib & 0x2);
   PBOOL("SMART/Health Information Log Page per Namespace Support",
         id->logPgAttrib & 0x1);
   PINT("Error Log Page Entries", id->errLogPgEntr);
   PINT("Number of Power States Support", id->numPowerSt);
   PBOOL("Format of Admin Vendor Specific Commands Is Same", id->admVendCmdCfg & 0x1);
   PBOOL("Format of Admin Vendor Specific Commands Is Vendor Specific",
         (id->admVendCmdCfg & 0x1) == 0);
   PBOOL("Autonomous Power State Transitions Support", id->apsta.autoPowerStX);
   PINT("Warning Composite Temperature Threshold", id->wcTemp);
   PINT("Critical Composite Temperature Threshold", id->ccTemp);
   PINT("Max Time for Firmware Activation", id->mtfa);
   PINT("Host Memory Buffer Preferred Size", id->hmPre);
   PINT("Host Memory Buffer Min Size", id->hmMin);
   P128BIT("Total NVM Capacity", id->tNVMCap);
   P128BIT("Unallocated NVM Capacity", id->uNVMCap);
   PINT("Access Size", id->rpmbs.accessSize);
   PINT("Total Size", id->rpmbs.accessSize);
   PINT("Authentication Method", id->rpmbs.authMethod);
   PINT("Number of RPMB Units", id->rpmbs.rpmbUnitsNum);
   PINT("Keep Alive Support", id->kas);
   PINT("Max Submission Queue Entry Size", 1 << ((id->subQSize & 0xf0) >> 4));
   PINT("Required Submission Queue Entry Size", 1 << (id->subQSize & 0xf));
   PINT("Max Completion Queue Entry Size", 1 << ((id->compQSize & 0xf0) >> 4));
   PINT("Required Completion Queue Entry Size", 1 << (id->compQSize & 0xf));
   PINT("Max Outstanding Commands", id->maxCmd);
   PINT("Number of Namespaces", id->numNmspc);
   PBOOL("Reservation Support", (id->cmdSupt & 0x20) >> 5);
   PBOOL("Save/Select Field in Set/Get Feature Support", (id->cmdSupt & 0x10) >> 4);
   PBOOL("Write Zeroes Command Support", (id->cmdSupt & 0x8) >> 3);
   PBOOL("Dataset Management Command Support", (id->cmdSupt & 0x4) >> 2);
   PBOOL("Write Uncorrectable Command Support", (id->cmdSupt & 0x2) >> 1);
   PBOOL("Compare Command Support", id->cmdSupt & 0x1);
   PBOOL("Fused Operation Support", id->fuseSupt & 0x1);
   PBOOL("Cryptographic Erase as Part of Secure Erase Support",
         (id->cmdAttrib & 0x4) >> 2);
   PBOOL("Cryptographic Erase and User Data Erase to All Namespaces",
         (id->cmdAttrib & 0x2) >> 1);
   PBOOL("Cryptographic Erase and User Data Erase to One Particular Namespace",
         ((id->cmdAttrib & 0x2) >> 1) == 0);
   PBOOL("Format Operation to All Namespaces", id->cmdAttrib & 0x1);
   PBOOL("Format Opertaion to One Particular Namespace", (id->cmdAttrib & 0x1) == 0);
   PBOOL("Volatile Write Cache Is Present", id->volWrCache & 0x1);
   PINT("Atomic Write Unit Normal", id->atomWrNorm);
   PINT("Atomic Write Unit Power Fail", id->atomWrFail);
   PBOOL("Format of All NVM Vendor Specific Commands Is Same", id->nvmVendCmdCfg & 0x1);
   PBOOL("Format of All NVM Vendor Specific Commands Is Vendor Specific",
         (id->nvmVendCmdCfg & 0x1) == 0);
   PINT("Atomic Compare and Write Unit", id->acwu);
   PBOOL("SGL Address Specify Offset Support", id->sgls.AddrSpcfOffSup);
   PBOOL("MPTR Contain SGL Descriptor Support", id->sgls.useMPTRSup);
   PBOOL("SGL Length Able to Larger than Data Amount", id->sgls.sglsLargerThanData);
   PBOOL("SGL Length Shall Be Equal to Data Amount", (id->sgls.sglsLargerThanData == 0));
   PBOOL("Byte Aligned Contiguous Physical Buffer of Metadata Support",
         id->sgls.byteAlignedContPhyBufSup);
   PBOOL("SGL Bit Bucket Descriptor Support", id->sgls.sglsBitBuckDescSup);
   PBOOL("SGL Keyed SGL Data Block Descriptor Support", id->sgls.keyedSglDataBlockDescSup);
   PBOOL("SGL for NVM Command Set Support", id->sgls.sglsSup);

   readbufflen = sizeof(id->subnqn) + 64;
   readablebuff = malloc(readbufflen);
   if (readablebuff != NULL) {
      memcpy(readablebuff, id->subnqn, sizeof(id->subnqn));
      if (refineASCIIString(readablebuff, sizeof(id->subnqn))) {
         Str_Strcat(readablebuff, "(has unprintable characters)", readbufflen);
      }
      PSTR("NVM Subsystem NVMe Qualified Name", readablebuff);
      free(readablebuff);
   } else {
      PSTR("NVM Subsystem NVMe Qualified Name", id->subnqn);
   }

   hexbufflen = sizeof(id->subnqn) * 2;
   hexdumpbuff = malloc(hexbufflen);
   if (hexdumpbuff != NULL) {
      hexdumptoString((char*)id->subnqn, sizeof(id->subnqn), hexdumpbuff, hexbufflen);
      PSTR("NVM Subsystem NVMe Qualified Name (hex format)", hexdumpbuff);
      free(hexdumpbuff);
   } else {
      PSTR("NVM Subsystem NVMe Qualified Name (hex format)", "NULL");
   }

   xml_struct_end();
   esxcli_xml_end_output();
}

static void
PrintIdentifyNs(struct iden_namespace *idNs)
{
   int lbaIndex;
   esxcli_xml_begin_output();
   xml_struct_begin("NamespaceInfo");
   PULL("Namespace Size", idNs->size);
   PULL("Namespace Capacity", idNs->capacity);
   PULL("Namespace Utilization", idNs->utilization);
   PBOOL("Thin Provisioning Support", idNs->feat & 0x1);
   PBOOL("Namespace Atomic Support", (idNs->feat & 0x2) >> 1);
   PBOOL("Deallocated or Unwritten Logical Block Error Support", (idNs->feat & 0x4) >> 2);
   PINT("Number of LBA Formats", idNs->numLbaFmt);
   PINT("LBA Format", idNs->fmtLbaSize & 0xf);
   PBOOL("Extended Metadata", (idNs->fmtLbaSize & 0x10) >> 4);
   PBOOL("Metadata as Seperate Buffer Support", (idNs->metaDataCap & 0x2) >> 1);
   PBOOL("Metadata as Extended Buffer Support", idNs->metaDataCap & 0x1);
   PBOOL("PI Type 1 Support", idNs->dataProtCap & 0x1);
   PBOOL("PI Type 2 Support", (idNs->dataProtCap & 0x2) >> 1);
   PBOOL("PI Type 3 Support", (idNs->dataProtCap & 0x4) >> 2);
   PBOOL("PI in First Eight Bytes of Metadata Support", (idNs->dataProtCap & 0x8) >> 3);
   PBOOL("PI in Last Eight Bytes of Metadata Support", (idNs->dataProtCap & 0x10) >> 4);
   PINT("PI Enabled Type", idNs->dataProtSet & 0x7);
   if (idNs->dataProtSet & 0x7) {
      PSTR("MetaData Location",
           idNs->dataProtSet & 0x8 ? "First Eight Bytes" : "Last Eight Bytes");
   } else {
      PSTR("MetaData Location", "PI Disabled");
   }
   PBOOL("Namespace Shared by Multiple Controllers", idNs->nmic.sharedNs);
   PBOOL("Persist Through Power Loss Support", idNs->resCap.pstThruPowerLoss);
   PBOOL("Write Exclusive Reservation Type Support", idNs->resCap.wrExcResv);
   PBOOL("Exclusive Access Reservation Type Support", idNs->resCap.excAcsResv);
   PBOOL("Write Exclusive Registrants Only Reservation Type Support",
         idNs->resCap.wrExcRegOnlyResv);
   PBOOL("Exclusive Access Registrants Only Reservation Type Support",
         idNs->resCap.excAcsRegOnlyResv);
   PBOOL("Write Exclusive All Registrants Reservation Type Support",
         idNs->resCap.wrExcAllRegOnlyResv);
   PBOOL("Exclusive Access All Registrants Reservation Type Support",
         idNs->resCap.excAcsAllRegOnlyResv);
   PBOOL("Format Progress Indicator Support", idNs->fpi.fmtProgIndtSup);
   PINT("Percentage Remains to Be Formatted", idNs->fpi.pctRemFmt);
   PINT("Namespace Atomic Write Unit Normal", idNs->nawun);
   PINT("Namespace Atomic Write Unit Power Fail", idNs->nawupf);
   PINT("Namespace Atomic Compare and Write Unit", idNs->nacwu);
   PINT("Namespace Atomic Boundary Size Normal", idNs->nabsn);
   PINT("Namespace Atomic Boundary Offset", idNs->nabo);
   PINT("Namespace Atomic Boundary Size Power Fail", idNs->nabspf);
   P128BIT("NVM Capacity", idNs->NVMCap);
   PID("Namespace Globally Unique Identifier", (vmk_uint8 *)&idNs->nguid, 16);
   PID("IEEE Extended Unique Identifier", (vmk_uint8 *)&idNs->eui64, 8);
   xml_field_begin("LBA Format Support");
   xml_list_begin("structure");
      for (lbaIndex = 0; lbaIndex <= idNs->numLbaFmt; lbaIndex ++) {
         xml_struct_begin("LBAFormatSupport");
         PINT("Format ID", lbaIndex);
         PINT("Metadata Size", idNs->lbaFmtSup[lbaIndex].metaSize);
         PINT("LBA Data Size", 1 << idNs->lbaFmtSup[lbaIndex].dataSize);
         PSTR("Relative Performance", nvmNsRelPerf[idNs->lbaFmtSup[lbaIndex].relPerf]);
         xml_struct_end();
   }
   xml_list_end();
   xml_field_end();
   xml_struct_end();
   esxcli_xml_end_output();
}

static void
PrintErrLog(struct error_log * errLog)
{
   xml_struct_begin("ErrorInfo");
   PULL("Error Count", errLog->errorCount);
   PINT("Submission Queue ID", errLog->sqID);
   PINT("Command ID", errLog->cmdID);
   PINT("Status Field", errLog->status);
   PINT("Byte in Command That Contained the Error", errLog->errorByte);
   PINT("Bit in Command That Contained the Error", errLog->errorBit);
   PULL("LBA", errLog->lba);
   PINT("Namespace", errLog->nameSpace);
   PINT("Vendor Specific Information Available", errLog->vendorInfo);
   xml_struct_end();
}

static void
PrintSmartLog(struct smart_log * smartLog)
{
   esxcli_xml_begin_output();
   xml_struct_begin("SMARTInfo");
   PBOOL("Available Spare Space Below Threshold", smartLog->criticalError & 0x1);
   PBOOL("Temperature Warning", (smartLog->criticalError & 0x2) >> 1);
   PBOOL("NVM Subsystem Reliability Degradation", (smartLog->criticalError & 0x4) >> 2);
   PBOOL("Read Only Mode", (smartLog->criticalError & 0x8) >> 3);
   PBOOL("Volatile Memory Backup Device Failure", (smartLog->criticalError & 0x10) >> 4);
   PINT("Composite Temperature",*(vmk_uint16 *)smartLog->temperature);
   PINT("Available Spare", smartLog->availableSpace);
   PINT("Available Spare Threshold", smartLog->availableSpaceThreshold);
   PINT("Percentage Used", smartLog->percentageUsed);
   P128BIT("Data Units Read", smartLog->dataUnitsRead);
   P128BIT("Data Units Written", smartLog->dataUnitsWritten);
   P128BIT("Host Read Commands", smartLog->hostReadCommands);
   P128BIT("Host Write Commands", smartLog->hostWriteCommands);
   P128BIT("Controller Busy Time", smartLog->controllerBusyTime);
   P128BIT("Power Cycles", smartLog->powerCycles);
   P128BIT("Power On Hours", smartLog->powerOnHours);
   P128BIT("Unsafe Shutdowns", smartLog->unsafeShutdowns);
   P128BIT("Media Errors", smartLog->mediaErrors);
   P128BIT("Number of Error Info Log Entries", smartLog->numberOfErrorInfoLogs);
   PINT("Warning Composite Temperature Time", smartLog->warningCompositeTempTime);
   PINT("Critical Composite Temperature Time", smartLog->criticalCompositeTempTime);
   PINT("Temperature Sensor 1", smartLog->tempSensor1);
   PINT("Temperature Sensor 2", smartLog->tempSensor2);
   PINT("Temperature Sensor 3", smartLog->tempSensor3);
   PINT("Temperature Sensor 4", smartLog->tempSensor4);
   PINT("Temperature Sensor 5", smartLog->tempSensor5);
   PINT("Temperature Sensor 6", smartLog->tempSensor6);
   PINT("Temperature Sensor 7", smartLog->tempSensor7);
   PINT("Temperature Sensor 8", smartLog->tempSensor8);
   xml_struct_end();
   esxcli_xml_end_output();
}

static void
PrintFwSlotLog(struct firmware_slot_log * fwSlotLog)
{
   esxcli_xml_begin_output();
   xml_struct_begin("FirmwareSlotInfo");
   PINT("Firmware Slot to Be Activated at Next Controller Reset",
        (fwSlotLog->activeFirmwareInfo & 0x70) >> 4);
   PINT("Firmware Slot Being Activated", fwSlotLog->activeFirmwareInfo & 0x7);
   P8BYTE("Firmware Revision for Slot 1", fwSlotLog->FirmwareRevisionSlot1);
   P8BYTE("Firmware Revision for Slot 2", fwSlotLog->FirmwareRevisionSlot2);
   P8BYTE("Firmware Revision for Slot 3", fwSlotLog->FirmwareRevisionSlot3);
   P8BYTE("Firmware Revision for Slot 4", fwSlotLog->FirmwareRevisionSlot4);
   P8BYTE("Firmware Revision for Slot 5", fwSlotLog->FirmwareRevisionSlot5);
   P8BYTE("Firmware Revision for Slot 6", fwSlotLog->FirmwareRevisionSlot6);
   P8BYTE("Firmware Revision for Slot 7", fwSlotLog->FirmwareRevisionSlot7);
   xml_struct_end();
   esxcli_xml_end_output();
}

/**
 * Get the device name via runtime name.
 *
 * @param [in] runtime_name        like: vmhba1:C0:T0:L0
 * @param [out] device_name        like: naa..., t10...
 * @param [in] device_name_length  max length of device name
 *
 * @retval return VMK_OK when success to get device name
 *         return VMK_NOT_FOUND when device has no name, like device is offline
 *         return VMK_FAILURE when popen executed fails
 *
 **/
static VMK_ReturnStatus
GetDeviceName(const char* runtimeName, char* deviceName, int deviceNameLength)
{
   char cmd[MAX_CMD_LEN];
   char buf[200];
   FILE *p;
   char *s1, *s2;
   int nameLen;
   VMK_ReturnStatus status;

   if (runtimeName == NULL || strlen(runtimeName) == 0 || deviceName == NULL) {
      return VMK_FAILURE;
   }

   snprintf(cmd, MAX_CMD_LEN, "esxcfg-mpath -L -P %s", runtimeName);
   p = popen(cmd, "r");
   if (!p) {
      return VMK_FAILURE;
   }

   status = VMK_NOT_FOUND;
   if (fgets(buf, sizeof(buf), p) == NULL) {
      goto out;
   }

   /* The output format should be "runtimename status devicename ..."
    * Check if the first word is the runtime name.*/
   s1 = strstr(buf, runtimeName);
   if (s1 != buf) {
      goto out;
   }

   /* The path is unclaimed. */
   s1 = strstr(buf, "no device");
   if (s1 != NULL) {
      goto out;
   }

   /* The path is not unclaimed, it may be active or dead. */
   s1 = strstr(buf, "state");
   if (s1 == NULL) {
      goto out;
   }

   /* Search for the device name which is between the second and the third blank.*/
   s1 = strstr(s1, " ");
   if (s1 == NULL) {
      goto out;
   }
   s2 = strstr(s1 + 1, " ");
   if (s2 == NULL) {
      goto out;
   }
   nameLen = s2 - s1 -1;
   if (nameLen > 0 && nameLen < deviceNameLength) {
      memcpy(deviceName, s1 + 1, nameLen);
      deviceName[nameLen] = '\0';
      status = VMK_OK;
   }

out:
   pclose(p);
   return status;
}

enum ExecuteCmdStatus{
   ExecuteWithoutOutput = 0,
   ExecuteWithOutput = 1,
   ExecuteError = 2,
};

static int
ExecuteCommand(const char* cmd)
{
   int rc = ExecuteWithoutOutput;
   FILE *p;

   if (cmd == NULL) {
      return ExecuteError;
   }

   p = popen(cmd, "r");
   if (!p) {
      return ExecuteError;
   }

   if (fgetc(p) != EOF) {
      rc = ExecuteWithOutput;
   } else {
      rc = ExecuteWithoutOutput;
   }

   pclose(p);
   return rc;
}

/**
 * Convert hex string to integer.
 *
 * @param [in] str
 * @param [out] value
 *
 * @retval return 0 when successful.
 *         return -1 for failure cases, e.g. input string has illegal characters.
 */
static int
htoi(const char* str, int *value)
{
   int i = 0;
   int n = 0;
   int v = 0;
   int tmp = 0;

   if (str == NULL || value == NULL) {
      return -1;
   }

   n = strlen(str);
   if (n > 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
      i = 2;
   }
   if (n - i > sizeof(int) * 2 || n - i == 0) {
      return -1;
   }

   while (i < n) {
      if (str[i] >= '0' && str[i] <= '9') {
         v = str[i] - '0';
      } else if (str[i] >= 'a' && str[i] <= 'f') {
         v = str[i] - 'a' + 10;
      } else if (str[i] >= 'A' && str[i] <= 'F') {
         v = str[i] - 'A' + 10;
      } else {
         return -1;
      }
      tmp = (tmp << 4) | (v & 0xf);
      i = i + 1;
   }
   *value = tmp;
   return 0;
}

static int
GetCtrlrId(struct nvme_handle *handle)
{
   struct iden_controller   *idCtrlr;
   int                      rc = 0;

   idCtrlr = malloc(sizeof(*idCtrlr));
   if (idCtrlr == NULL) {
      return -1;
   }

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, idCtrlr);
   if (rc != 0) {
      free(idCtrlr);
      return -1;
   }

   rc = idCtrlr->cntlId;
   free(idCtrlr);
   return rc;
}


void
NvmePlugin_DeviceList(int argc, const char *argv[])
{
   struct nvme_adapter_list list;
   int    rc;
   int    i;

   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      if (rc == ENODEV) {
         /* No kernel instance for the given management API was found.*/
         list.count = 0;
      } else {
         Error("Failed to get adapter list: 0x%x.", rc);
         return;
      }
   }

   esxcli_xml_begin_output();
   xml_list_begin("structure");
   for (i = 0; i < list.count; i++) {
      xml_struct_begin("DeviceList");
      PSTR("HBA Name", list.adapters[i].name);
      PSTR("Status", list.adapters[i].status == ONLINE ? "Online" : "Offline");
      PSTR("Signature", list.adapters[i].signature);
      xml_struct_end();
   }
   xml_list_end();
   esxcli_xml_end_output();
}

void
NvmePlugin_DeviceNsCreate(int argc, const char *argv[])
{
   int                      ch;
   int                      rc;
   int                      nsId;
   struct nvme_adapter_list list;
   const char              *vmhba = NULL;
   struct nvme_handle      *handle;
   struct iden_namespace   *idNs;
   vmk_uint64               size        = 0;
   vmk_uint64               capacity    = 0;
   vmk_uint8                fmtLbaSize  = -1;
   vmk_uint8                dataProtSet = -1;
   vmk_uint8                nmic        = -1;
   int                      cmdStatus   = 0;

   while ((ch = getopt(argc, (char *const*)argv, "A:s:c:f:p:m:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;
         case 's':
            size = (vmk_uint64)atoll(optarg);
            break;
         case 'c':
            capacity = (vmk_uint64)atoll(optarg);
            break;
         case 'f':
            fmtLbaSize = atoi(optarg);
            break;
         case 'p':
            dataProtSet = atoi(optarg);
            break;
         case 'm':
            nmic = atoi(optarg);
            break;

         default:
            Error("Invalid parameter.");
            return;
      }
   }

   if (vmhba == NULL || size == 0 || capacity == 0 || fmtLbaSize  == -1 ||
       dataProtSet == -1 || nmic == -1) {
      Error("Invalid parameter.");
      return;
   }

   // Disable creating namespace with "nmic = 1" until driver supports this.
   if (nmic == 1) {
      Error("Multi-path I/O and Namespace Sharing Capabilities (NMIC) are not supported "
            "by ESXi.");
      return;
   }

   // do stuff for nvme device namespace list -A vmhbax.
   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   rc = Nvme_NsMgmtAttachSupport(handle);
   if (rc == -1) {
      Error("Failed to check capability of namespace management and attachment.");
      goto out;
   }
   if (rc == 0) {
      Error("Controller doesn't support this feature.");
      goto out;
   }

   idNs = malloc(sizeof(*idNs));
   if (idNs == NULL) {
      Error("Out of memory.");
      goto out;
   }

   memset(idNs, 0, sizeof(*idNs));

   idNs->size = size;
   idNs->capacity = capacity;
   idNs->fmtLbaSize = fmtLbaSize;
   idNs->dataProtSet = dataProtSet;
   idNs->nmic.sharedNs = nmic & 0x1;

   nsId = Nvme_NsMgmtCreate(handle, idNs, &cmdStatus);
   if (nsId == -1) {
      switch (cmdStatus) {
         case 0x0:
            Error("Failed to execute create namespace request.");
            break;
         case 0x10a:
            Error("The LBA Format specified is not supported.");
            break;
         case 0x115:
            Error("Creating the namespace requires more free space than is currently available.");
            break;
         case 0x116:
            Error("The number of namespaces supported has been exceeded.");
            break;
         case 0x11b:
            Error("Thin provisioning is not supported by the controller.");
            break;
         default:
            Error("Failed to create namespace, 0x%x.", cmdStatus);
            break;
      }
      goto out_free;
   }

   esxcli_xml_begin_output();
   xml_list_begin("string");
   printf("<string>Namespace %d is created successfully.</string>", nsId);
   xml_list_end();
   esxcli_xml_end_output();

out_free:
   free(idNs);
out:
   Nvme_Close(handle);
}


void
NvmePlugin_DeviceNsDelete(int argc, const char *argv[])
{
   int                      ch;
   int                      rc;
   const char              *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   vmk_uint32               nsId = 0;
   int                      status;

   while ((ch = getopt(argc, (char *const*)argv, "A:n:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;

         case 'n':
            nsId = atoi(optarg);
            break;

         default:
            Error("Invalid parameter.");
            return;
      }
   }

   if (vmhba == NULL) {
      Error("Invalid parameter.");
      return;
   }

   // do stuff for nvme device namespace list -A vmhbax.
   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   rc = Nvme_NsMgmtAttachSupport(handle);
   if (rc == -1) {
      Error("Failed to check capability of namespace management and attachment.");
      goto out;
   }
   if (rc == 0) {
      Error("Controller doesn't support this feature.");
      goto out;
   }

   rc = Nvme_ValidNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to validate nsId %d.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Invalid namespace Id %d.", nsId);
      goto out;
   }

   rc = Nvme_AllocatedNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to check Namespace Id %d is created.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Namespace %d is not created.", nsId);
      goto out;
   }

   rc = Nvme_AttachedNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to check Namespace Id %d is attached.", nsId);
      goto out;
   }
   if (rc == 1) {
      Error("Please detach the namespace %d before deleting it.", nsId);
      goto out;
   }

   /* Check the namespace status.*/
   rc = Nvme_NsGetStatus(handle, nsId, &status);
   if (rc) {
      Error("Failed to get device status of namespace %d.", nsId);
      goto out;
   }
   /* If namespace is online, Offline namespace if the namespace is not busy.*/
   if (status == NS_ONLINE) {
      Error("Please offline the namespace %d before deleting it.", nsId);
      goto out;
   }

   rc = Nvme_NsMgmtDelete(handle, nsId);
   if (rc != 0) {
      Error("Failed to delete namespace, 0x%x.", rc);
      goto out;
   }

   esxcli_xml_begin_output();
   xml_list_begin("string");
   printf("<string>Namespace %d is deleted successfully.</string>", nsId);
   xml_list_end();
   esxcli_xml_end_output();

out:
   Nvme_Close(handle);
}

void
NvmePlugin_DeviceNsAttach(int argc, const char *argv[])
{
   int                      ch;
   int                      rc;
   const char              *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct ctrlr_list       *ctrlrList;
   vmk_uint32               nsId = 0;
   vmk_uint32               ctrlrId = 0;
   int                      cmdStatus = 0;

   while ((ch = getopt(argc, (char *const*)argv, "A:n:c:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;

         case 'n':
            nsId = atoi(optarg);
            break;

         case 'c':
            ctrlrId = atoi(optarg);
            break;

         default:
            Error("Invalid parameter.");
            return;
      }
   }

   if (vmhba == NULL) {
      Error("Invalid parameter.");
      return;
   }

   // do stuff for nvme device namespace list -A vmhbax.
   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   rc = GetCtrlrId(handle);
   if (rc == -1) {
      Error("Failed to get this controller ID.");
      goto out;
   }
   if (rc != ctrlrId) {
      Error("This controller ID is %d. Attaching other controllers is not supported.",
            rc);
      goto out;
   }

   rc = Nvme_NsMgmtAttachSupport(handle);
   if (rc == -1) {
      Error("Failed to check capability of namespace management and attachment.");
      goto out;
   }
   if (rc == 0) {
      Error("Controller doesn't support this feature.");
      goto out;
   }

   rc = Nvme_ValidNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to validate nsId %d.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Invalid namespace Id %d.", nsId);
      goto out;
   }

   rc = Nvme_AllocatedNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to check Namespace Id %d is created.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Namespace %d is not created.", nsId);
      goto out;
   }

   rc = Nvme_AttachedNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to check Namespace Id %d is attached.", nsId);
      goto out;
   }
   if (rc == 1) {
      Error("Namespace %d is already attached.", nsId);
      goto out;
   }

   ctrlrList = malloc(sizeof(*ctrlrList));
   if (ctrlrList == NULL) {
      Error("Out of memory.");
      goto out;
   }

   // Currently 1 attach command only supports attaching the namesapce to 1 controller
   memset(ctrlrList, 0, sizeof(*ctrlrList));
   ctrlrList->ctrlrId[0] = 1; //number of controllers
   ctrlrList->ctrlrId[1] = ctrlrId; //1st controller id

   rc = Nvme_NsAttach(handle, NS_ATTACH, nsId, ctrlrList, &cmdStatus);
   if (rc != 0) {
      switch (cmdStatus) {
         case 0x0:
            Error("Failed to execute attach request, 0x%x.", rc);
            break;
         case 0x118:
            Error("Controller %d is already attached to namespace %d.", ctrlrId, nsId);
            break;
         case 0x119:
            Error("Namespace %d is private.", nsId);
            break;
         case 0x11c:
            Error("The controller list provided is invalid.");
            break;
         default:
            Error("Failed to attach namespace %d to controller %d, 0x%x", nsId, ctrlrId, cmdStatus);
            break;
      }
      goto out_free;
   }

   rc = Nvme_NsListUpdate(handle, NS_ATTACH, nsId);
   if (rc != 0) {
      Error("Attach namespace successfully, but failed to update namespace list after"
            " attach. Offline namespace.");
      goto out_free;
   }

   esxcli_xml_begin_output();
   xml_list_begin("string");
   printf("<string>Namespace %d is attached to Controller %d successfully.</string>",
          nsId, ctrlrId);
   xml_list_end();
   esxcli_xml_end_output();

out_free:
   free(ctrlrList);

out:
   Nvme_Close(handle);
}

void
NvmePlugin_DeviceNsDetach(int argc, const char *argv[])
{
   int                      ch;
   int                      rc;
   const char              *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   int                      status;
   struct ctrlr_list       *ctrlrList;
   vmk_uint32               nsId = 0;
   vmk_uint32               ctrlrId = 0;
   char                     cmd[MAX_CMD_LEN];
   char                     runtimeName[MAX_DEV_NAME_LEN];
   char                     deviceName[MAX_DEV_NAME_LEN];
   int                      cmdStatus = 0;

   while ((ch = getopt(argc, (char *const*)argv, "A:n:c:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;

         case 'n':
            nsId = atoi(optarg);
            break;

         case 'c':
            ctrlrId = atoi(optarg);
            break;

         default:
            Error("Invalid parameter.");
            return;
      }
   }

   if (vmhba == NULL) {
      Error("Invalid parameter.");
      return;
   }

   // do stuff for nvme device namespace list -A vmhbax.
   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Failed to open device.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Adapter not found.");
      return;
   }

   rc = GetCtrlrId(handle);
   if (rc == -1) {
      Error("Failed to get this controller ID.");
      goto out;
   }
   if (rc != ctrlrId) {
      Error("This controller ID is %d. Detaching other controllers is not supported.",
            rc);
      goto out;
   }

   rc = Nvme_NsMgmtAttachSupport(handle);
   if (rc == -1) {
      Error("Failed to check capability of namespace management and attachment.");
      goto out;
   }
   if (rc == 0) {
      Error("Controller doesn't support this feature.");
      goto out;
   }

   rc = Nvme_ValidNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to validate nsId %d.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Invalid namespace Id %d.", nsId);
      goto out;
   }

   rc = Nvme_AllocatedNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to check Namespace Id %d is created.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Namespace %d is not created.", nsId);
      goto out;
   }

   rc = Nvme_AttachedNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to check Namespace Id %d is attached.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Namespace %d is already detached.", nsId);
      goto out;
   }

   /* Check the namespace status.*/
   rc = Nvme_NsGetStatus(handle, nsId, &status);
   if (rc) {
      Error("Failed to get device status of namespace %d.", nsId);
      goto out;
   }

   /* If namespace is online, Offline namespace if the namespace is not busy.*/
   if (status == NS_ONLINE) {
      snprintf(runtimeName, MAX_DEV_NAME_LEN, "%s:C0:T0:L%d", vmhba, nsId-1);
      status = GetDeviceName(runtimeName, deviceName, MAX_DEV_NAME_LEN);
      if (status == VMK_FAILURE) {
         Error("Failed to get device name of namespace %d.", nsId);
         goto out;
      }
      /**
       * If GetDeviceName returns VMK_NOT_FOUND, it indicates that the path is dead or
       * cannot be seen by upper layer for some reason. It should be OK to directly do
       * offline operation under this case.
       */
      if (status == VMK_OK) {
         snprintf(cmd, MAX_CMD_LEN,
                  "esxcli storage core claiming unclaim -t path -p %s", runtimeName);
         rc = ExecuteCommand(cmd);
         if (rc) {
            Error("Failed to detach namespace since it is still in use.");
            goto out;
         }
      }

      rc = Nvme_NsSetStatus(handle, nsId, NS_OFFLINE);
      if (rc) {
         Error("Failed to offline namespace.");
         goto out_reclaim;
      }
   }

   ctrlrList = malloc(sizeof(*ctrlrList));
   if (ctrlrList == NULL) {
      Error("Out of memory.");
      goto out;
   }

   // Currently 1 detach command only supports detaching the namesapce to 1 controller
   memset(ctrlrList, 0, sizeof(*ctrlrList));
   ctrlrList->ctrlrId[0] = 1; //number of controllers
   ctrlrList->ctrlrId[1] = ctrlrId; //1st controller id

   rc = Nvme_NsAttach(handle, NS_DETACH, nsId, ctrlrList, &cmdStatus);
   if (rc != 0) {
      switch (cmdStatus) {
         case 0x0:
            Error("Failed to execute detach request, 0x%x.", rc);
            break;
         case 0x119:
            Error("Namespace %d is private.", nsId);
            break;
         case 0x11a:
            Error("Controller %d is not attached to the namespace %d", ctrlrId, nsId);
            break;
         case 0x11c:
            Error("The controller list provided is invalid.");
            break;
         default:
            Error("Failed to detach namespace %d from controller %d, 0x%x.", nsId, ctrlrId, cmdStatus);
            break;
      }
      goto out_free;
   }

   rc = Nvme_NsListUpdate(handle, NS_DETACH, nsId);
   if (rc != 0) {
      Error("Detach namespace successfully, but failed to update namespace list after"
            " detach. Offline namespace.");
      goto out_free;
   }

   esxcli_xml_begin_output();
   xml_list_begin("string");
   printf("<string>Namespace %d is detached from Controller %d successfully.</string>",
          nsId, ctrlrId);
   xml_list_end();
   esxcli_xml_end_output();

out_free:
   free(ctrlrList);

out_reclaim:
   snprintf(cmd, MAX_CMD_LEN, "esxcfg-rescan -a %s", vmhba);
   rc = ExecuteCommand(cmd);

out:
   Nvme_Close(handle);
}


void
NvmePlugin_DeviceNsOnline(int argc, const char *argv[])
{
   int                      ch;
   int                      rc;
   const char              *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   vmk_uint32               nsId = 0;
   int                      status;

   while ((ch = getopt(argc, (char *const*)argv, "A:n:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;

         case 'n':
            nsId = atoi(optarg);
            break;

         default:
            Error("Invalid parameter.");
            return;
      }
   }


   if (vmhba == NULL) {
      Error("Invalid parameter.");
      return;
   }

   // do stuff for nvme device namespace list -A vmhbax.
   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   rc = Nvme_ValidNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to validate nsId %d.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Invalid namespace Id %d.", nsId);
      goto out;
   }

   rc = Nvme_AllocatedNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to check Namespace Id %d is created.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Namespace %d is not created.", nsId);
      goto out;
   }

   rc = Nvme_AttachedNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to check Namespace Id %d is attached.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Namespace %d is not attached.", nsId);
      goto out;
   }

   /* Check the namespace status.*/
   rc = Nvme_NsGetStatus(handle, nsId, &status);
   if (rc) {
      Error("Failed to get device status of namespace %d.", nsId);
      goto out;
   }

   /* If namespace is offline, Online namespace. */
   if (status == NS_OFFLINE) {
      rc = Nvme_NsSetStatus(handle, nsId, NS_ONLINE);
      if (rc) {
         Error("Failed to online namespace.");
         goto out;
      }
   } else {
      Error("Namespace is already online.");
      goto out;
   }

   esxcli_xml_begin_output();
   xml_list_begin("string");
   printf("<string>Namespace %d online successfully.</string>", nsId);
   xml_list_end();
   esxcli_xml_end_output();

out:
   Nvme_Close(handle);
}

void
NvmePlugin_DeviceNsOffline(int argc, const char *argv[])
{
   int                      ch;
   int                      rc;
   int                      status = 0;
   const char              *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   vmk_uint32               nsId = 0;

   while ((ch = getopt(argc, (char *const*)argv, "A:n:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;

         case 'n':
            nsId = atoi(optarg);
            break;

         default:
            Error("Invalid parameter.");
            return;
      }
   }


   if (vmhba == NULL) {
      Error("Invalid parameter.");
      return;
   }

   // do stuff for nvme device namespace list -A vmhbax.
   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   /* Check the namespace status.*/
   rc = Nvme_NsGetStatus(handle, nsId, &status);
   if (rc) {
      Error("Failed to get device status of namespace %d.", nsId);
      goto out;
   }

   rc = Nvme_ValidNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to validate nsId %d.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Invalid namespace Id %d.", nsId);
      goto out;
   }

   rc = Nvme_AllocatedNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to check Namespace Id %d is created.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Namespace %d is not created.", nsId);
      goto out;
   }

   rc = Nvme_AttachedNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to check Namespace Id %d is attached.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Namespace %d is not attached.", nsId);
      goto out;
   }

   if (status == NS_ONLINE) {
      rc = Nvme_NsSetStatus(handle, nsId, NS_OFFLINE);
      if (rc) {
         Error("Failed to offline namespace.");
         goto out;
      }
   } else {
      Error("Namespace is already offline.");
      goto out;
   }

   esxcli_xml_begin_output();
   xml_list_begin("string");
   printf("<string>Namespace %d offline successfully.</string>", nsId);
   xml_list_end();
   esxcli_xml_end_output();

out:
   Nvme_Close(handle);
}

void
NvmePlugin_DeviceListController(int argc, const char *argv[])
{
   int                      ch, i, rc;
   const char              *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct ctrlr_list       *ctrlrList;
   vmk_uint32               nsId = 0;
   BOOL                     setNs = false;

   while ((ch = getopt(argc, (char *const*)argv, "A:n:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;

         case 'n':
            setNs = true;
            nsId = atoi(optarg);
            break;

         default:
            Error("Invalid parameter.");
            return;
      }
   }

   if (vmhba == NULL) {
      Error("Invalid parameter.");
      return;
   }

   // do stuff for nvme device namespace list -A vmhbax.
   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   rc = Nvme_NsMgmtAttachSupport(handle);
   if (rc == -1) {
      Error("Failed to check capability of namespace management and attachment.");
      goto out;
   }
   if (rc == 0) {
      Error("Controller doesn't support this feature.");
      goto out;
   }

   ctrlrList = malloc(sizeof(*ctrlrList));
   if (ctrlrList == NULL) {
      Error("Out of memory.");
      goto out;
   }

   if (setNs) {
      rc = Nvme_ValidNsId(handle, nsId);
      if (rc == -1) {
         Error("Failed to validate nsId %d.", nsId);
         goto out_free;
      }
      if (rc == 0) {
         Error("Invalid namespace Id %d.", nsId);
         goto out_free;
      }

      rc = Nvme_AllocatedNsId(handle, nsId);
      if (rc == -1) {
         Error("Failed to check Namespace Id %d is created.", nsId);
         goto out_free;
      }
      if (rc == 0) {
         Error("Namespace %d is not created.", nsId);
         goto out_free;
      }
      rc = Nvme_Identify(handle, ATTACHED_CONTROLLER_LIST, 0, nsId, ctrlrList);
      if (rc != 0) {
         Error("Failed to get attached controller list, 0x%x.", rc);
         goto out_free;
      }
   } else {
      rc = Nvme_Identify(handle, ALL_CONTROLLER_LIST, 0, 0, ctrlrList);
      if (rc != 0) {
         Error("Failed to get all controller list, 0x%x.", rc);
         goto out_free;
      }
   }

   esxcli_xml_begin_output();
   xml_list_begin("structure");
   for (i = 0; i < ctrlrList->ctrlrId[0]; i++) {
      xml_struct_begin("ControllerList");
      PINT("Controller ID", ctrlrList->ctrlrId[i+1]);
      xml_struct_end();
   }
   xml_list_end();
   esxcli_xml_end_output();

out_free:
   free(ctrlrList);
out:
   Nvme_Close(handle);
}

void
NvmePlugin_DeviceNsList(int argc, const char *argv[])
{
   int                      ch, rc;
   vmk_uint32               i, j, k, numNs;
   const char              *vmhba = NULL;
   struct nvme_adapter_list list;
   struct ns_list          *nsAllocatedList = NULL;
   struct ns_list          *nsActiveList = NULL;
   struct nvme_handle      *handle;
   struct iden_controller  *idCtrlr;
   char runtimeName[MAX_DEV_NAME_LEN];
   char (*devNames)[MAX_DEV_NAME_LEN] = NULL;
   int  *statusFlags = NULL;
   int   nsStatus;
   VMK_ReturnStatus status;
   BOOL nsMgmtSupt = false;

   while ((ch = getopt(argc, (char *const*)argv, "A:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;

         default:
            Error("Invalid parameter.");
            return;
      }
   }

   if (vmhba == NULL) {
      Error("Invalid parameter.");
      return;
   }

   // do stuff for nvme device namespace list -A vmhbax.
   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   idCtrlr = malloc(sizeof(*idCtrlr));
   if (idCtrlr == NULL) {
      Error("Out of memory.");
      goto out;
   }

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      goto out_free;
   }

   rc = Nvme_NsMgmtAttachSupport(handle);
   if (rc == -1) {
      Error("Failed to check capability of namespace management and attachment.");
      goto out_free;
   } else if (rc == 1) {
      nsMgmtSupt = true;
      nsAllocatedList = malloc(sizeof(*nsAllocatedList));
      if (nsAllocatedList == NULL) {
         Error("Out of memory.");
         goto out_free;
      }

      nsActiveList = malloc(sizeof(*nsActiveList));
      if (nsActiveList == NULL) {
         Error("Out of memory.");
         goto out_free_allocated;
      }

      rc = Nvme_Identify(handle, ALLOCATED_NAMESPACE_LIST, 0, 0, nsAllocatedList);
      if (rc != 0) {
         Error("Failed to get allocated namespace list, 0x%x.", rc);
         goto out_free_active;
      }

      rc = Nvme_Identify(handle, ACTIVE_NAMESPACE_LIST, 0, 0, nsActiveList);
      if (rc != 0) {
         Error("Failed to attached namespace list, 0x%x.", rc);
         goto out_free_active;
      }
   }

   numNs = idCtrlr->numNmspc < NVME_MAX_NAMESPACE_PER_CONTROLLER ?
           idCtrlr->numNmspc : NVME_MAX_NAMESPACE_PER_CONTROLLER;

   devNames = (char(*)[MAX_DEV_NAME_LEN])malloc(numNs * sizeof(char) * MAX_DEV_NAME_LEN);
   statusFlags = (int *)malloc(numNs * sizeof(int));
   memset(devNames, 0, numNs * sizeof(*devNames));
   memset(statusFlags, 0, numNs * sizeof(*statusFlags)); // Init as NS_UNALLOCATED

   for (i = 1; i <= numNs; i++) {
      if (nsMgmtSupt) {
         for (j = 0; j < numNs; j ++) {
            if (nsActiveList->nsId[j] == 0 || nsActiveList->nsId[j] > i) {
               break;
            }
            if (nsActiveList->nsId[j] == i) {
               statusFlags[i-1] = NS_ACTIVE;
               break;
            }
         }
         if (statusFlags[i-1] != NS_ACTIVE) {
            for (k = 0; k < numNs; k ++) {
               if (nsAllocatedList->nsId[k] == 0 || nsAllocatedList->nsId[k] > i) {
                  break;
               }
               if (nsAllocatedList->nsId[k] == i) {
                  statusFlags[i-1] = NS_ALLOCATED;
                  snprintf(devNames[i-1], MAX_DEV_NAME_LEN, "N/A");
                  break;
               }
            }
            continue;
         }
      } else {
         statusFlags[i-1] = NS_ACTIVE;
      }
      snprintf(runtimeName, MAX_DEV_NAME_LEN, "%s:C0:T0:L%d", vmhba, i-1);
      status = GetDeviceName(runtimeName, devNames[i-1], MAX_DEV_NAME_LEN);
      if (status == VMK_FAILURE) {
         Error("Failed to get device name of namespace %d.", i);
         goto out_free_active;
      }

      rc = Nvme_NsGetStatus(handle, i, &nsStatus);
      if (rc) {
         Error("Failed to get device status of namespace %d.", i);
         goto out_free_active;
      }

      if (status == VMK_NOT_FOUND && nsStatus == NS_ONLINE) {
         /* The path is unclaimed by upper layer.*/
         snprintf(devNames[i-1], MAX_DEV_NAME_LEN, "N/A (Unclaimed)");
      }

      if (statusFlags[i-1] == NS_ACTIVE && nsStatus == NS_OFFLINE) {
         /* Invalid format, namespace is not supported or namespace is offline.*/
         snprintf(devNames[i-1], MAX_DEV_NAME_LEN,
                  "N/A (Unsupported Format or Namespace Offline)");
      }
   }

   esxcli_xml_begin_output();
   xml_list_begin("structure");
   for (i = 0; i < numNs; i++) {
      if (statusFlags[i] > NS_UNALLOCATED) {
         xml_struct_begin("NamespaceList");
         PINT("Namespace ID", i+1);
         PSTR("Status", nsStatusString[statusFlags[i]]);
         PSTR("Device Name", devNames[i]);
         xml_struct_end();
      }
   }
   xml_list_end();
   esxcli_xml_end_output();

out_free_active:
   if (nsActiveList) {
      free(nsActiveList);
   }
out_free_allocated:
   if (nsAllocatedList) {
      free(nsAllocatedList);
   }
out_free:
   free(idCtrlr);
   if (devNames) {
      free(devNames);
   }
   if (statusFlags) {
      free(statusFlags);
   }
out:
   Nvme_Close(handle);
}

void
NvmePlugin_DeviceNsGet(int argc, const char *argv[])
{
   int                      ch, rc, nsId = 0;
   const char              *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct iden_namespace    *idNs;

   while ((ch = getopt(argc, (char *const*)argv, "A:n:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;

         case 'n':
            nsId = atoi(optarg);
            break;

         default:
            Error("Invalid parameter.");
            return;
      }
   }

   if (vmhba == NULL || nsId <= 0) {
      Error("Invalid parameter.");
      return;
   }

   // do stuff for nvme device namespace get -A vmhbax -n numNs.
   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   rc = Nvme_ValidNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to validate nsId %d.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Invalid namespace Id %d.", nsId);
      goto out;
   }

   rc = Nvme_AllocatedNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to check Namespace Id %d is created.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Namespace %d is not created.", nsId);
      goto out;
   }

   rc = Nvme_AttachedNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to check Namespace Id %d is attached.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Namespace %d is not attached.", nsId);
      goto out;
   }

   idNs = malloc(sizeof(*idNs));
   if (idNs == NULL) {
      Error("Out of memory.");
      goto out;
   }

   rc = Nvme_Identify(handle, IDENTIFY_NAMESPACE, 0, nsId, idNs);
   if (rc) {
      Error("Failed to get identify data for namespace %d, %s.", nsId, strerror(rc));
   } else {
      PrintIdentifyNs(idNs);
   }

   free(idNs);
out:
   Nvme_Close(handle);
}

void
NvmePlugin_DeviceGet(int argc, const char *argv[])
{
   int                      ch;
   int                      rc;
   const char              *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct iden_controller  *id;

   while ((ch = getopt(argc, (char *const*)argv, "A:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;

         default:
            Error("Invalid parameter.");
            return;
      }
   }

   if (vmhba == NULL) {
      Error("Invalid parameter.");
      return;
   }

   // do stuff for nvme device get -A vmhbax.
   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   id = malloc(sizeof(*id));
   if (id == NULL) {
      Error("Out of memory.");
      goto out;
   }

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, id);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      goto out_free;
   }

   PrintIdentifyCtrlr(id);

out_free:
   free(id);
out:
   Nvme_Close(handle);
}

void
NvmePlugin_DeviceNsFormat(int argc, const char *argv[])
{
   char *vmhba = NULL;
   int  nsId = -1;
   int  f = -1, s = -1, l = -1, p = -1, m = -1;
   int  rc;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct iden_controller  *idCtrlr;
   struct iden_namespace   *idNs;
   char cmd[MAX_CMD_LEN];
   char runtimeName[MAX_DEV_NAME_LEN];
   int ch;
   int nsStatus = 0;
   VMK_ReturnStatus status;
   char deviceName[MAX_DEV_NAME_LEN];
   int mdSize = 0;

   while ((ch = getopt(argc, (char *const*)argv, "A:n:f:s:p:l:m:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;
         case 'n':
            nsId = atoi(optarg);
            break;
         case 'f':
            f = atoi(optarg);
            break;
         case 's':
            s = atoi(optarg);
            break;
         case 'p':
            p = atoi(optarg);
            break;
         case 'l':
            l = atoi(optarg);
            break;
         case 'm':
            m = atoi(optarg);
            break;
         default:
            Error("Invalid parameter.");
            return;
      }
   }

   if (vmhba == NULL      ||
       nsId < 1           ||
       (f < 0 || f > 16)  ||
       (s < 0 || s > 2)   ||
       (p < 0 || p > 3)   ||
       (l < 0 || l > 1)   ||
       (m < 0 || m > 1))
   {
      Error("Invalid parameter.");
      return;
   }

   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   rc = Nvme_ValidNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to validate nsId %d.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Invalid namespace Id %d.", nsId);
      goto out;
   }

   rc = Nvme_AllocatedNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to check Namespace Id %d is created.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Namespace %d is not created.", nsId);
      goto out;
   }

   rc = Nvme_AttachedNsId(handle, nsId);
   if (rc == -1) {
      Error("Failed to check Namespace Id %d is attached.", nsId);
      goto out;
   }
   if (rc == 0) {
      Error("Namespace %d is not attached.", nsId);
      goto out;
   }

   idCtrlr = malloc(sizeof(*idCtrlr));
   if (idCtrlr == NULL) {
      Error("Out of memory.");
      goto out;
   }

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      goto out_free_idCtrlr;
   }

   if((idCtrlr->adminCmdSup & 0x2) == 0) {
      Error("NVM Format command is not supported.");
      goto out_free_idCtrlr;
   }

   idNs = malloc(sizeof(*idNs));
   if (idNs == NULL) {
      Error("Out of memory.");
      goto out_free_idCtrlr;
   }

   rc = Nvme_Identify(handle, IDENTIFY_NAMESPACE, 0, nsId, idNs);
   if (rc != 0) {
      Error("Failed to get namespace identify information, 0x%x.", rc);
      goto out_free_idNs;
   }

   if (idNs->numLbaFmt < f) {
      Error("Invalid parameter: format %d exceeds supported format number %d.",
             f, idNs->numLbaFmt);
      goto out_free_idNs;
   }

   mdSize = idNs->lbaFmtSup[f].metaSize;

   if ((idNs->metaDataCap & 0x1) == 0 && m == 1 && mdSize > 0) {
      Error("Invalid parameter: ms, namespace doesn't support metadata being tranferred"
            " as part of an extended data buffer.");
      goto out_free_idNs;
   }

   if ((idNs->metaDataCap & 0x2) == 0 && m == 0 && mdSize > 0) {
      Error("Invalid parameter: ms, namespace doesn't support metadata being tranferred"
            " as part of a separate buffer.");
      goto out_free_idNs;
   }

   if (mdSize == 0 && p > 0) {
      Error("Invalid parameter: pi, PI cannot be enabled with zero metadata size.");
      goto out_free_idNs;
   }

   if ((idNs->dataProtCap & 0x1) == 0 && p == 1) {
      Error("Invalid parameter: pi, namespace doesn't support PI Type 1.");
      goto out_free_idNs;
   }

   if ((idNs->dataProtCap & 0x2) == 0 && p == 2) {
      Error("Invalid parameter: pi, namespace doesn't support PI Type 2.");
      goto out_free_idNs;
   }

   if ((idNs->dataProtCap & 0x4) == 0 && p == 3) {
      Error("Invalid parameter: pi, namespace doesn't support PI Type 3.");
      goto out_free_idNs;
   }

   if ((idNs->dataProtCap & 0x8) == 0 && l == 1 && p > 0) {
      Error("Invalid parameter: pil, namespace doesn't support PI data being transferred"
            " as first eight bytes of metadata.");
      goto out_free_idNs;
   }

   if ((idNs->dataProtCap & 0x10) == 0 && l == 0 && p > 0) {
      Error("Invalid parameter: pil, namespace doesn't support PI data being transferred"
            " as last eight bytes of metadata.");
      goto out_free_idNs;
   }

   if ((idCtrlr->cmdAttrib & 0x4) == 0 && s == 2) {
      Error("Invalid parameter: ses, crytographic erase is not supported.");
      goto out_free_idNs;
   }

   /* Check the namespace status.*/
   rc = Nvme_NsGetStatus(handle, nsId, &nsStatus);
   if (rc) {
      Error("Failed to get device status of namespace %d.", nsId);
      goto out_free_idNs;
   }

   /* If namespace is online, Offline namespace if the namespace is not busy.*/
   if (nsStatus == NS_ONLINE) {
      snprintf(runtimeName, MAX_DEV_NAME_LEN, "%s:C0:T0:L%d", vmhba, nsId-1);
      status = GetDeviceName(runtimeName, deviceName, MAX_DEV_NAME_LEN);
      if (status == VMK_FAILURE) {
         Error("Failed to get device name of namespace %d.", nsId);
         goto out_free_idNs;
      }
      /* If GetDeviceName returns VMK_NOT_FOUND, it indicates that the path is dead or cannot be seen
       * by upper layer for some reason. It should be OK to directly do offline operation under this case.*/
      if (status == VMK_OK) {
         snprintf(cmd, MAX_CMD_LEN, "esxcli storage core claiming unclaim -t path -p %s", runtimeName);
         rc = ExecuteCommand(cmd);
         if (rc) {
            Error("Failed to format since the namespace is still in use.");
            goto out_free_idNs;
         }
      }
      rc = Nvme_NsSetStatus(handle, nsId, NS_OFFLINE);
      if (rc) {
         Error("Failed to offline namespace.");
         goto out_reclaim;
      }
   }

   rc = Nvme_FormatNvm(handle, s, l, p, m, f, nsId);
   if (rc) {
      Error("Format fails or timeout, 0x%x. Offline namespace.", rc);
      goto out_free_idNs;
   } else {
      rc = Nvme_NsUpdate(handle, nsId);
      if (rc) {
         Error("Format successfully, but failed to update namespace attributes after"
               " format. Offline namespace.");
         goto out_free_idNs;
      }
   }

   rc = Nvme_NsSetStatus(handle, nsId, NS_ONLINE);
   if (rc) {
      Error("Format and update namespace attributes successfully,"
            " but failed to online namespace.");
      goto out_free_idNs;
   }

   snprintf(cmd, MAX_CMD_LEN, "esxcli storage filesystem rescan");
   rc = ExecuteCommand(cmd);
   if (rc) {
      Error("Format, update namesapce attributes and online namespace successfully,"
            " but failed to rescan the filesystem. A stale entry may exist.");
      goto out_free_idNs;
   }

   esxcli_xml_begin_output();
   xml_list_begin("string");
   xml_format("string", "Format successfully!");
   xml_list_end();
   esxcli_xml_end_output();

out_reclaim:
   snprintf(cmd, MAX_CMD_LEN, "esxcfg-rescan -a %s", vmhba);
   rc = ExecuteCommand(cmd);

out_free_idNs:
   free(idNs);

out_free_idCtrlr:
   free(idCtrlr);

out:
   Nvme_Close(handle);
}

void
NvmePlugin_DeviceLogGet(int argc, const char *argv[])
{
   int ch;
   char *vmhba = NULL;
   int  lid = -1;
   int  nsId = -1;
   int  elpe = -1;
   BOOL setNsid = 0;
   BOOL setElpe = 0;
   int  i, rc;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct iden_controller  *idCtrlr;
   struct usr_io uio;
   int maxErrorLogEntries;
   union {
      struct error_log errLog[MAX_ERROR_LOG_ENTRIES];
      struct smart_log smartLog;
      struct firmware_slot_log fwSlotLog;
   } log;

   while ((ch = getopt(argc, (char *const*)argv, "A:l:n:e:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;
         case 'l':
            lid = atoi(optarg);
            break;
         case 'n':
            nsId = atoi(optarg);
            setNsid = 1;
            break;
         case 'e':
            elpe = atoi(optarg);
            setElpe = 1;
            break;
         default:
            Error("Invalid parameter.");
            return;
      }
   }

   if (vmhba == NULL      ||
       (lid < 1 || lid > 3))
   {
      Error("Invalid parameter.");
      return;
   }

   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   idCtrlr = malloc(sizeof(*idCtrlr));
   if (idCtrlr == NULL) {
      Error("Out of memory.");
      goto out;
   }

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      goto out_free;
   }
   maxErrorLogEntries = (int)idCtrlr->errLogPgEntr + 1;
   if (maxErrorLogEntries > MAX_ERROR_LOG_ENTRIES) {
      maxErrorLogEntries = MAX_ERROR_LOG_ENTRIES;
   }

   /* Check the optional parameters: nsId and eple.*/
  if (setNsid) {
      if (lid == GLP_ID_SMART_HEALTH && (idCtrlr->logPgAttrib & 0x1)) {
         if (nsId < 1 || nsId > (int)idCtrlr->numNmspc) {
            rc = Nvme_AllocatedNsId(handle, nsId);
            if (rc == -1) {
               Error("Failed to check Namespace Id %d is created.", nsId);
               goto out_free;
            }
            if (rc == 0) {
               Error("Namespace %d is not created.", nsId);
               goto out_free;
            }

            rc = Nvme_AttachedNsId(handle, nsId);
            if (rc == -1) {
               Error("Failed to check Namespace Id %d is attached.", nsId);
               goto out_free;
            }
            if (rc == 0) {
               Error("Namespace %d is not attached.", nsId);
               goto out_free;
            }
         }
      } else {
         Error("This log page is not supported on a per namespace basis.");
         goto out_free;
      }
   }
   if (setElpe) {
      if (lid == GLP_ID_ERR_INFO) {
         if (elpe < 1 || elpe > maxErrorLogEntries) {
            Error("Invalid error log page entries. The supported range is [1, %d].",
                  maxErrorLogEntries);
            goto out_free;
         }
      } else {
         Error("Invalid parameter.");
         goto out_free;
      }
   } else {
      if (lid == GLP_ID_ERR_INFO) {
         Error("Missing required parameter -e when using -l 1");
         goto out_free;
      }
   }

   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_LOG_PAGE;
   uio.cmd.header.namespaceID = -1;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getLogPage.LogPageID = lid;

   switch (lid)
   {
      case GLP_ID_ERR_INFO:
         uio.cmd.cmd.getLogPage.numDW = GLP_LEN_ERR_INFO * elpe / 4 - 1;
         uio.length = GLP_LEN_ERR_INFO * elpe;
         uio.addr = (vmk_uintptr_t)&log.errLog;
         break;
      case GLP_ID_SMART_HEALTH:
         uio.cmd.header.namespaceID = nsId;
         uio.cmd.cmd.getLogPage.numDW = GLP_LEN_SMART_HEALTH / 4 - 1;
         uio.length = GLP_LEN_SMART_HEALTH;
         uio.addr = (vmk_uintptr_t)&log.smartLog;
         break;
      case GLP_ID_FIRMWARE_SLOT_INFO:
         uio.cmd.cmd.getLogPage.numDW = GLP_LEN_FIRMWARE_SLOT_INFO / 4 - 1;
         uio.length = GLP_LEN_FIRMWARE_SLOT_INFO;
         uio.addr = (vmk_uintptr_t)&log.fwSlotLog;
         break;
      default:
         Error("Invalid parameter.");
         goto out_free;
   }

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get log info, %s.", strerror(rc));
      goto out_free;
   }

   switch (lid)
   {
      case GLP_ID_ERR_INFO:
         esxcli_xml_begin_output();
         xml_list_begin("structure");
         for (i = 0; i < elpe; i++) {
            PrintErrLog(&log.errLog[i]);
         }
         xml_list_end();
         esxcli_xml_end_output();
         break;
      case GLP_ID_SMART_HEALTH:
         PrintSmartLog(&log.smartLog);
         break;
      case GLP_ID_FIRMWARE_SLOT_INFO:
         PrintFwSlotLog(&log.fwSlotLog);
         break;
      default:
         Error("Invalid log page.");
   }

out_free:
   free(idCtrlr);

out:
   Nvme_Close(handle);
}


static int
LookupSelect(const char *sel)
{
   int i;
   const char* selectStr[] = {
      "current",
      "default",
      "saved",
   };
   if (sel == NULL) {
      // "current" is selected by default
      return 0;
   }
   for(i = 0; i < 3; i++) {
      if (strncmp(sel, selectStr[i], strlen(selectStr[i]) + 1) == 0) {
         return i;
      }
   }
   return -1;
}

typedef void (*GetFeatureFunc)(struct nvme_handle*, int, int);
typedef void (*SetFeatureFunc)(struct nvme_handle*, int, int, int, const char**);

struct Feature {
   vmk_uint8      fid;
   const char    *desc;
   vmk_uint32     useBufferLen;
   GetFeatureFunc getFeature;
   SetFeatureFunc setFeature;
};

const char* strFeatErr(vmk_uint32 code) {
   switch(code) {
      case 0x2:   return "Not supported";
      case 0x5:   return "Aborted";
      case 0x10d: return "Feature Identifier Not Saveable";
      case 0x10e: return "Feature Not Changeable";
      case 0x10f: return "Feature Not Namespace Specific";
      case 0x114: return "Overlapping Range";
      default: return "Error";
   }
}
#define NVME_FEATURE_ERROR_STR strFeatErr((uio.comp.SCT << 8) | uio.comp.SC)

void issueSetFeature(struct nvme_handle *handle,
                     int nsId,
                     int fid,
                     int save,
                     vmk_uint32 dw11,
                     vmk_uint32 dw12,
                     vmk_uint32 dw13,
                     vmk_uint32 dw14,
                     vmk_uint32 dw15,
                     vmk_uint8 *buf,
                     vmk_uint32 len)
{
   int rc;
   struct usr_io uio;
   memset(&uio, 0, sizeof(uio));

   uio.cmd.header.opCode = NVM_ADMIN_CMD_SET_FEATURES;
   uio.cmd.header.namespaceID = nsId;
   uio.cmd.cmd.setFeatures.featureID = fid;
   uio.cmd.cmd.setFeatures.save = save;
   uio.cmd.dw[11] = dw11;
   uio.cmd.dw[12] = dw12;
   uio.cmd.dw[13] = dw13;
   uio.cmd.dw[14] = dw14;
   uio.cmd.dw[15] = dw15;
   uio.direction = XFER_TO_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.addr = (vmk_uintptr_t)buf;
   uio.length = len;
   rc = Nvme_AdminPassthru(handle, &uio);
   if (rc) {
      Error("Failed to set feature info, %s.", NVME_FEATURE_ERROR_STR);
   } else {
      esxcli_xml_begin_output();
      xml_list_begin("string");
      xml_format("string", "Feature set successfully!");
      xml_list_end();
      esxcli_xml_end_output();
   }
}

void getFeature_01h(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   struct usr_io uio;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getFeatures.featureID = FTR_ID_ARBITRATION;
   uio.cmd.cmd.getFeatures.select = select;

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.param.cmdSpecific;
   Debug("value = %x\n", value);
   esxcli_xml_begin_output();
   xml_struct_begin("Arbitration");
   PINT("Arbitration Burst", value & 0x7);
   PINT("Low Priority Weight", (value & 0xff00) >> 8);
   PINT("Medium Priority Weight", (value & 0xff0000) >> 16);
   PINT("High Priority Weight", (value & 0xff000000) >> 24);
   xml_struct_end();
   esxcli_xml_end_output();
}

void setFeature_01h(struct nvme_handle *handle, int save, int nsId, int argc, const char **argv)
{
   int   ch, rc, burst = 0, low = 0, mid = 0, high = 0;
   char *burstStr = NULL;
   char *lowStr = NULL;
   char *midStr = NULL;
   char *highStr = NULL;
   struct usr_io uioReg;
   vmk_uint64 regs;
   vmk_uint32 dw11;

   optind = 1;
   while ((ch = getopt(argc, (char *const*)argv, ":v:x:y:z:")) != -1) {
      switch (ch) {
         case 'v':
            burstStr = optarg;
            break;

         case 'x':
            lowStr = optarg;
            break;

         case 'y':
            midStr = optarg;
            break;

         case 'z':
            highStr = optarg;
            break;
      }
   }
   if (burstStr == NULL || lowStr == NULL || midStr == NULL || highStr == NULL) {
      Error("Missing parameter.");
      return;
   }
   errno = 0;
   burst = strtol(burstStr, NULL, 0);
   if (errno) {
      Error("Invalid burst value format.");
      return;
   }
   errno = 0;
   low = strtol(lowStr, NULL, 0);
   if (errno) {
      Error("Invalid low value format.");
      return;
   }
   errno = 0;
   mid = strtol(midStr, NULL, 0);
   if (errno) {
      Error("Invalid mid value format.");
      return;
   }
   errno = 0;
   high = strtol(highStr, NULL, 0);
   if (errno) {
      Error("Invalid high value format.");
      return;
   }
   if ((burst >> 3 | low >> 8 | mid >> 8 | high >> 8) != 0) {
      Error("Invalid parameter.");
      return;
   }

   memset(&uioReg, 0, sizeof(uioReg));
   uioReg.addr = (vmk_uintptr_t)&regs;
   uioReg.length = sizeof(regs);

   rc = Nvme_Ioctl(handle, NVME_IOCTL_DUMP_REGS, &uioReg);
   if (rc) {
      Error("Failed to get controller registers, 0x%x.", rc);
      return;
   }

   if ((regs & NVME_CAP_AMS_MSK64) >> NVME_CAP_AMS_LSB == 0) {
      if (low || mid || high) {
         Error("Invalid operation: Controller only support Round Robin arbitration"
               " mechanism, Low/Medium/High Priority Weight must be set to 0.");
         return;
      }
   }
   dw11 = burst | (low << 8) | (mid << 16) | (high << 24);
   issueSetFeature(handle, 0, FTR_ID_ARBITRATION, save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_02h(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   struct usr_io uio;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getFeatures.featureID = FTR_ID_PWR_MANAGEMENT;
   uio.cmd.cmd.getFeatures.select = select;

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.param.cmdSpecific;
   Debug("value = %x\n", value);
   esxcli_xml_begin_output();
   xml_struct_begin("PowerManagement");
   PINT("Power State", value & 0x1f);
   PINT("Workload Hint", (value & 0xe0) >> 5);
   xml_struct_end();
   esxcli_xml_end_output();
}

void setFeature_02h(struct nvme_handle *handle, int save, int nsId, int argc, const char **argv)
{
   int   ch, rc, workload = 0, powerState = 0;
   char *workloadStr = NULL;
   char *powerStateStr = NULL;
   struct iden_controller idCtrlr;
   vmk_uint32 dw11;

   optind = 1;
   while ((ch = getopt(argc, (char *const*)argv, ":v:w:")) != -1) {
      switch (ch) {
         case 'v':
            powerStateStr = optarg;
            break;

         case 'w':
            workloadStr = optarg;
            break;
      }
   }
   if (powerStateStr == NULL) {
      Error("Missing parameter.");
      return;
   }
   errno = 0;
   powerState = strtol(powerStateStr, NULL, 0);
   if (errno) {
      Error("Invalid power state value format.");
      return;
   }
   if (workloadStr != NULL) {
      errno = 0;
      workload = strtol(workloadStr, NULL, 0);
      if (errno) {
         Error("Invalid workload hint value format.");
         return;
      }
   }
   if ((powerState >> 5 | workload >> 3) != 0) {
      Error("Invalid parameter.");
      return;
   }
   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }

   if (powerState > idCtrlr.numPowerSt || powerState < 0) {
      Error("Invalid parameter: power state setting is beyond supported: %d!",
            idCtrlr.numPowerSt);
      return;
   }
   if (!((idCtrlr.ver.mjr == 1 && idCtrlr.ver.mnr >= 2) || (idCtrlr.ver.mjr >= 2))) {
      if (workloadStr != 0) {
         Error("Invalid parameter: 'Workload Hint' is only supported by the device whose version >= 1.2.");
         return;
      }
   }
   dw11 = powerState | (workload << 5);
   issueSetFeature(handle, 0, FTR_ID_PWR_MANAGEMENT, save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_03h(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc, numRanges, i;
   struct usr_io uio;
   vmk_uint8 buf[4096];

   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getFeatures.featureID = FTR_ID_LBA_RANGE_TYPE;
   uio.cmd.cmd.getFeatures.select = select;
   uio.addr = (vmk_uintptr_t)buf;
   uio.length = 4096;
   uio.cmd.header.namespaceID = nsId;

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.param.cmdSpecific;
   Debug("value = %x\n", value);
   esxcli_xml_begin_output();
   numRanges = value & 0x3f;
   xml_list_begin("structure");
   for (i = 0; i < numRanges + 1; i++) {
      const char *typeStr = NULL;
      switch(buf[64 *i + 0]) {
      case 0x0:
         typeStr = "Reserved";
         break;
      case 0x1:
         typeStr = "Filesystem";
         break;
      case 0x2:
         typeStr = "RAID";
         break;
      case 0x3:
         typeStr = "Cache";
         break;
      case 0x4:
         typeStr = "Page/swap file";
         break;
      default:
         typeStr = "Reserved";
         break;
      };
      xml_struct_begin("LbaRangeList");
      PINT("Range Number", i);
      PSTR("Type", typeStr);
      PBOOL("Attr:Overwritten", buf[64 * i + 1] & 0x1);
      PBOOL("Attr:Hidden", buf[64 * i + 1] & 0x2);
      PULL("Starting LBA", *((vmk_uint64*)&buf[64 * i + 16]));
      PULL("Number of Logical Blocks", *((vmk_uint64*)&buf[64 * i + 24]));
      printf("<field name=\"Unique Identifier\"><string>%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x</string></field>\n",
             buf[64 * i + 32], buf[64 * i + 33], buf[64 * i + 34], buf[64 * i + 35], buf[64 * i + 36], buf[64 * i + 37], buf[64 * i + 38], buf[64 * i + 39],
             buf[64 * i + 40], buf[64 * i + 41], buf[64 * i + 42], buf[64 * i + 43], buf[64 * i + 44], buf[64 * i + 45], buf[64 * i + 46], buf[64 * i + 47]);
      xml_struct_end();
   }
   xml_list_end();
   esxcli_xml_end_output();
}

int getSmartLog(struct nvme_handle *handle, struct smart_log *smartLog)
{
   int rc;
   struct usr_io uio;
   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_LOG_PAGE;
   uio.cmd.header.namespaceID = -1;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getLogPage.LogPageID = GLP_ID_SMART_HEALTH;
   uio.cmd.cmd.getLogPage.numDW = GLP_LEN_SMART_HEALTH / 4 - 1;
   uio.length = GLP_LEN_SMART_HEALTH;
   uio.addr = (vmk_uintptr_t)smartLog;
   rc = Nvme_AdminPassthru(handle, &uio);
   return rc;
}

void getFeature_04h(struct nvme_handle *handle, int select, int nsId)
{
   int rc;
   vmk_uint32 sensor = 0, overThreshold = 0, underThreshold = 0;
   struct iden_controller idCtrlr;
   struct smart_log smartLog;

   struct usr_io uio;
   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getFeatures.featureID = FTR_ID_TEMP_THRESHOLD;
   uio.cmd.cmd.getFeatures.select = select;

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }

   rc = getSmartLog(handle, &smartLog);
   if (rc) {
      Error("Failed to get log info, %s.", strerror(rc));
      return;
   }

   esxcli_xml_begin_output();
   xml_list_begin("structure");
   for (sensor = 0; sensor < 9; sensor++) {
      if (sensor != 0) {
         vmk_uint16 temp = (&smartLog.tempSensor1)[sensor - 1];
         if (temp == 0) {
            // The temperature sensor is not implemented
            continue;
         }
      }
      if (sensor != 0 || idCtrlr.wcTemp != 0) {
         uio.cmd.cmd.getFeatures.numCplQReq = sensor | 0x10;
         rc = Nvme_AdminPassthru(handle, &uio);
         if (rc) {
            continue;
         }
         underThreshold = uio.comp.param.cmdSpecific & 0xffff;
      }

      uio.cmd.cmd.getFeatures.numCplQReq = sensor;
      rc = Nvme_AdminPassthru(handle, &uio);
      if (rc) {
         continue;
      }
      overThreshold = uio.comp.param.cmdSpecific & 0xffff;

      xml_struct_begin("TemperatureThreshold");
      if (sensor == 0) {
         PSTR("Threshold Temperature Select", "Composite Temperature");
      } else {
         printf("<field name=\"Threshold Temperature Select\"><string>Temperature Sensor %d</string></field>\n", sensor);
      }
      if (sensor == 0 && idCtrlr.wcTemp == 0) {
         PSTR("Under Temperature Threshold", "N/A");
      } else {
         printf("<field name=\"Under Temperature Threshold\"><string>%d K</string></field>\n", underThreshold);
      }
      printf("<field name=\"Over Temperature Threshold\"><string>%d K</string></field>\n", overThreshold);
      xml_struct_end();
   }
   xml_list_end();
   esxcli_xml_end_output();
}

void setFeature_04h(struct nvme_handle *handle, int save, int nsId, int argc, const char **argv)
{
   int   ch, rc, sensor = 0, under = 0, threshold = 0;
   char *sensorStr = NULL;
   char *thresholdStr = NULL;
   struct iden_controller idCtrlr;
   struct smart_log smartLog;
   vmk_uint32 dw11;

   optind = 1;
   while ((ch = getopt(argc, (char *const*)argv, ":s:v:u")) != -1) {
      switch (ch) {
         case 's':
            sensorStr = optarg;
            break;

         case 'v':
            thresholdStr = optarg;
            break;

         case 'u':
            under = 1;
            break;
      }
   }
   if (thresholdStr == NULL) {
      Error("Missing parameter.");
      return;
   }
   if (sensorStr != NULL) {
      errno = 0;
      sensor = strtol(sensorStr, NULL, 0);
      if (errno) {
         Error("Invalid threshold temperature select value format.");
         return;
      }
   }
   errno = 0;
   threshold = strtol(thresholdStr, NULL, 0);
   if (errno) {
      Error("Invalid temperature threshold value format.");
      return;
   }
   if ((threshold >> 16 | sensor >> 4) != 0) {
      Error("Invalid parameter.");
      return;
   }

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }

   rc = getSmartLog(handle, &smartLog);
   if (rc) {
      Error("Failed to get log info, %s.", strerror(rc));
      return;
   }

   if (sensor == 0 && under == 1 && idCtrlr.wcTemp == 0) {
      Error("Invalid operation: The under temperature threshold Feature is not implemented for Composite Temperature.");
      return;
   }
   if (sensor != 0 && (&smartLog.tempSensor1)[sensor - 1] == 0) {
      Error("Invalid operation: The Temperature sensor %d is not implemented.", sensor);
      return;
   }
   dw11 = threshold | (sensor << 16) | under << 20;
   issueSetFeature(handle, 0, FTR_ID_TEMP_THRESHOLD, save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_05h(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   struct usr_io uio;
   struct iden_controller idCtrlr;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getFeatures.featureID = FTR_ID_ERR_RECOVERY;
   uio.cmd.cmd.getFeatures.select = select;
   uio.cmd.header.namespaceID = nsId;

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }
   if ((idCtrlr.ver.mjr == 1 && idCtrlr.ver.mnr >= 2) || (idCtrlr.ver.mjr >= 2)) {
      if (nsId == 0) {
         Error("Invalid parameter: Must specify a valid namespace ID for the device whose version >= 1.2.");
         return;
      }
   } else {
      if (nsId != 0) {
         Error("Invalid parameter: Shouldn't specify namespace ID for a device whose version < 1.2.");
         return;
      }
   }

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.param.cmdSpecific;
   Debug("value = %x\n", value);
   esxcli_xml_begin_output();
   xml_struct_begin("ErrorRecovery");
   PINT("Time Limited Error Recovery", value & 0xffff);
   PBOOL("Deallocated or Unwritten Logical Block Error Enable", value & 0x10000);
   xml_struct_end();
   esxcli_xml_end_output();
}

void setFeature_05h(struct nvme_handle *handle, int save, int nsId, int argc, const char **argv)
{
   int   ch, rc, dulbe = 0, time = 0;
   char *dulbeStr = NULL;
   char *timeStr = NULL;
   struct iden_controller idCtrlr;
   struct iden_namespace idNs;
   vmk_uint32 dw11;

   optind = 1;
   while ((ch = getopt(argc, (char *const*)argv, ":e:v:")) != -1) {
      switch (ch) {
         case 'e':
            dulbeStr = optarg;
            break;

         case 'v':
            timeStr = optarg;
            break;
      }
   }
   if (timeStr == NULL) {
      Error("Missing parameter.");
      return;
   }
   errno = 0;
   time = strtol(timeStr, NULL, 0);
   if (errno) {
      Error("Invalid retry timeout time value format.");
      return;
   }
   if (dulbeStr != NULL) {
      errno = 0;
      dulbe = strtol(dulbeStr, NULL, 0);
      if (errno) {
         Error("Invalid DULBE enable value format.");
         return;
      }
   }

   if ((time >> 16 | dulbe >> 1) != 0) {
      Error("Invalid parameter.");
      return;
   }

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }
   if ((idCtrlr.ver.mjr == 1 && idCtrlr.ver.mnr >= 2) || (idCtrlr.ver.mjr >= 2) ) {
      if (nsId == 0) {
         Error("Invalid parameter: Must specify a valid namespace ID for the device whose version >= 1.2.");
         return;
      }
   } else {
      if (nsId != 0) {
         Error("Invalid parameter: Shouldn't specify namespace ID for a device whose version < 1.2.");
         return;
      }
      if (dulbe) {
         Error("Invalid parameter: Can't enable 'Deallocated or Unwritten Logical Block Error'. It is not supported for a device whose version < 1.2.");
         return;
      }
   }

   if (dulbe) {
      rc = Nvme_Identify(handle, IDENTIFY_NAMESPACE, 0, nsId, &idNs);
      if (rc) {
         Error("Failed to get identify data for namespace %d, %s.", nsId, strerror(rc));
         return;
      }
      if ((idNs.feat & 0x4) == 0) {
         Error("Invalid operation: Can't enable Deallocated or Unwritten Logical Block Error, it's not supported for the namespace.");
         return;
      }
   }
   dw11 = time | (dulbe << 16);
   issueSetFeature(handle, nsId, FTR_ID_ERR_RECOVERY, save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_06h(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   struct usr_io uio;
   struct iden_controller idCtrlr;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getFeatures.featureID = FTR_ID_WRITE_CACHE;
   uio.cmd.cmd.getFeatures.select = select;

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }

   if ((idCtrlr.volWrCache & 0x1) == 0) {
      Error("Failed to get this feature: controller has no write cache!");
      return;
   }

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.param.cmdSpecific;
   Debug("value = %x\n", value);

   esxcli_xml_begin_output();
   xml_struct_begin("VolatileWriteCache");
   PBOOL("Volatile Write Cache Enabled", value & 0x1);
   xml_struct_end();
   esxcli_xml_end_output();
}

void setFeature_06h(struct nvme_handle *handle, int save, int nsId, int argc, const char **argv)
{
   int   ch, rc, enable = 0;
   char *enableStr = NULL;
   struct iden_controller idCtrlr;
   vmk_uint32 dw11;

   optind = 1;
   while ((ch = getopt(argc, (char *const*)argv, ":v:")) != -1) {
      switch (ch) {
         case 'v':
            enableStr = optarg;
            break;
      }
   }

   if (enableStr == NULL) {
      Error("Missing parameter.");
      return;
   }
   errno = 0;
   enable = strtol(enableStr, NULL, 0);
   if (errno) {
      Error("Invalid enable value format.");
      return;
   }
   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }

   if ((idCtrlr.volWrCache & 0x1) == 0) {
      Error("Failed to set this feature: controller has no write cache!");
      return;
   }

   if ((enable >> 1) != 0) {
      Error("Invalid parameter.");
      return;
   }
   dw11 = enable;
   issueSetFeature(handle, 0, FTR_ID_WRITE_CACHE, save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_07h(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   struct usr_io uio;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getFeatures.featureID = FTR_ID_NUM_QUEUE;
   uio.cmd.cmd.getFeatures.select = select;

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.param.cmdSpecific;
   Debug("value = %x\n", value);

   esxcli_xml_begin_output();
   xml_struct_begin("NumberOfQueue");
   PINT("Number of Submission Queues Allocated", value & 0xffff);
   PINT("Number of Completion Queues Allocated", (value & 0xffff0000) >> 16);
   xml_struct_end();
   esxcli_xml_end_output();
}

void getFeature_08h(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   struct usr_io uio;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getFeatures.featureID = FTR_ID_INT_COALESCING;
   uio.cmd.cmd.getFeatures.select = select;

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, 0x%x.", rc);
      return;
   }

   value = uio.comp.param.cmdSpecific;
   Debug("value = %x\n", value);

   esxcli_xml_begin_output();
   xml_struct_begin("InterruptCoalescing");
   PINT("Aggregation Time", (value & 0xff00) >> 8);
   PINT("Aggregation Threshold", value & 0xff);
   xml_struct_end();
   esxcli_xml_end_output();
}

void setFeature_08h(struct nvme_handle *handle, int save, int nsId, int argc, const char **argv)
{
   int   ch, time = 0, threshold = 0;
   char *timeStr = NULL;
   char *thresholdStr = NULL;
   vmk_uint32 dw11;

   optind = 1;
   while ((ch = getopt(argc, (char *const*)argv, ":v:x:")) != -1) {
      switch (ch) {
         case 'v':
            thresholdStr = optarg;
            break;

         case 'x':
            timeStr = optarg;
            break;
      }
   }

   if (thresholdStr == NULL || timeStr == NULL) {
      Error("Missing parameter.");
      return;
   }
   errno = 0;
   threshold = strtol(thresholdStr, NULL, 0);
   if (errno) {
      Error("Invalid aggregation threshold value format.");
      return;
   }
   errno = 0;
   time = strtol(timeStr, NULL, 0);
   if (errno) {
      Error("Invalid aggregation time value format.");
      return;
   }

   if ((threshold >> 8 | time >> 8) != 0) {
      Error("Invalid parameter.");
      return;
   }
   dw11 = threshold | (time << 8);
   issueSetFeature(handle, 0, FTR_ID_INT_COALESCING, save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_09h(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc, vectNum, i;
   struct usr_io uio;
   struct usr_io uioVect;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getFeatures.featureID = FTR_ID_INT_VECTOR_CONFIG;
   uio.cmd.cmd.getFeatures.select = select;

   memset(&uioVect, 0, sizeof(uioVect));
   rc = Nvme_Ioctl(handle, NVME_IOCTL_GET_INT_VECT_NUM, &uioVect);
   if (rc) {
      Error("Failed to get controller interrupt vector number.");
      return;
   }

   vectNum = uioVect.length;
   Debug("vectNum: %d\n", vectNum);
   esxcli_xml_begin_output();
   xml_list_begin("structure");
   for (i = 0; i < vectNum; i++) {
      uio.cmd.cmd.getFeatures.numSubQReq = i;
      rc = Nvme_AdminPassthru(handle, &uio);
      if (rc) {
         continue;
      }
      value = uio.comp.param.cmdSpecific;
      xml_struct_begin("InterruptVectorConfiguration");
      PINT("Interrupt Vector", value & 0xffff);
      PBOOL("Coalescing Disable", value & 0x10000);
      xml_struct_end();
   }
   xml_list_end();
   esxcli_xml_end_output();
}

void setFeature_09h(struct nvme_handle *handle, int save, int nsId, int argc, const char **argv)
{
   int   ch, rc, vectNum, vector = 0, disable = 0;
   char *vectorStr = NULL;
   char *disableStr = NULL;
   struct usr_io uioVect;
   vmk_uint32 dw11;

   optind = 1;
   while ((ch = getopt(argc, (char *const*)argv, ":v:x:")) != -1) {
      switch (ch) {
         case 'v':
            vectorStr = optarg;
            break;

         case 'x':
            disableStr = optarg;
            break;
      }
   }

   if (vectorStr == NULL || disableStr == NULL) {
      Error("Missing parameter.");
      return;
   }
   errno = 0;
   vector = strtol(vectorStr, NULL, 0);
   if (errno) {
      Error("Invalid interrupt vector value format.");
      return;
   }
   errno = 0;
   disable = strtol(disableStr, NULL, 0);
   if (errno) {
      Error("Invalid coalescing disable value format.");
      return;
   }

   if ((vector >> 16 | disable >> 1) != 0) {
      Error("Invalid parameter.");
      return;
   }

   memset(&uioVect, 0, sizeof(uioVect));
   rc = Nvme_Ioctl(handle, NVME_IOCTL_GET_INT_VECT_NUM, &uioVect);
   if (rc) {
      Error("Failed to get controller interrupt vector number.");
      return;
   }

   vectNum = uioVect.length;
   if (vector < 0 || vector > vectNum) {
      Error("Invalid parameter: interrupt vector number is beyond supported: %d!",
            vectNum);
      return;
   }

   if (vector == 0) {
      Error("Invalid parameter: interrupt coalescing is not supported for admin queue!");
      return;
   }


   dw11 = vector | (disable << 16);
   issueSetFeature(handle, 0, FTR_ID_INT_VECTOR_CONFIG, save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_0ah(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   struct usr_io uio;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getFeatures.featureID = FTR_ID_WRITE_ATOMICITY;
   uio.cmd.cmd.getFeatures.select = select;

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.param.cmdSpecific;
   Debug("value = %x\n", value);

   esxcli_xml_begin_output();
   xml_struct_begin("WriteAtomicity");
   PBOOL("Disable Normal", value & 0x1);
   xml_struct_end();
   esxcli_xml_end_output();
}

void setFeature_0ah(struct nvme_handle *handle, int save, int nsId, int argc, const char **argv)
{
   int   ch, disable = 0;
   char *disableStr = NULL;
   vmk_uint32 dw11;

   optind = 1;
   while ((ch = getopt(argc, (char *const*)argv, ":v:")) != -1) {
      switch (ch) {
         case 'v':
            disableStr = optarg;
            break;
      }
   }

   if (disableStr == NULL) {
      Error("Missing parameter.");
      return;
   }
   errno = 0;
   disable = strtol(disableStr, NULL, 0);
   if (errno) {
      Error("Invalid disable normal value format.");
      return;
   }

   if ((disable >> 1) != 0) {
      Error("Invalid parameter.");
      return;
   }

   dw11 = disable;
   issueSetFeature(handle, 0, FTR_ID_WRITE_ATOMICITY, save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_0bh(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   struct usr_io uio;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getFeatures.featureID = FTR_ID_ASYN_EVENT_CONFIG;
   uio.cmd.cmd.getFeatures.select = select;

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.param.cmdSpecific;
   Debug("value = %x\n", value);

   esxcli_xml_begin_output();
   xml_struct_begin("AsyncEventConfiguration");
   xml_field_begin("SMART / Health Critical Warnings");
   xml_struct_begin("SMART");
   PBOOL("Available Spare Space", value & 0x1);
   PBOOL("Temperature", value & 0x2);
   PBOOL("Media Error", value & 0x4);
   PBOOL("Read Only Mode", value & 0x8);
   PBOOL("Backup Device Fail", value & 0x10);
   xml_struct_end();
   xml_field_end();
   PBOOL("Namespace Attribute Notices", value & 0x100);
   PBOOL("Firmware Activation Notices", value & 0x200);
   xml_struct_end();
   esxcli_xml_end_output();
}

void setFeature_0bh(struct nvme_handle *handle, int save, int nsId, int argc, const char **argv)
{
   int   ch, rc, smart = 0, namespace = 0, firmware = 0;
   char *smartStr = NULL;
   char *namespaceStr = NULL;
   char *firmwareStr = NULL;
   vmk_uint32 dw11;
   struct iden_controller idCtrlr;

   optind = 1;
   while ((ch = getopt(argc, (char *const*)argv, ":v:m:w:")) != -1) {
      switch (ch) {
         case 'v':
            smartStr = optarg;
            break;

         case 'm':
            namespaceStr = optarg;
            break;

         case 'w':
            firmwareStr = optarg;
            break;
      }
   }

   if (smartStr == NULL) {
      Error("Missing parameter.");
      return;
   }
   errno = 0;
   smart = strtol(smartStr, NULL, 0);
   if (errno) {
      Error("Invalid smart health critical warnings value format.");
      return;
   }

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }
   if (namespaceStr != NULL) {
      if (!idCtrlr.oaes.nsChgEvent) {
         Error("Invalid parameter: The device don't support to set 'Namespace Activation Notices'");
         return;
      }
      errno = 0;
      namespace = strtol(namespaceStr, NULL, 0);
      if (errno) {
         Error("Invalid namespace attribute notices value format.");
         return;
      }
   }

   if (firmwareStr != NULL) {
      if (!idCtrlr.oaes.fwActEvent) {
         Error("Invalid parameter: The device don't support to set 'Firmware Activation Notices'");
         return;
      }
      errno = 0;
      firmware = strtol(firmwareStr, NULL, 0);
      if (errno) {
         Error("Invalid firmware activation notices value format.");
         return;
      }
   }

   if ((smart >> 8 || namespace >> 1 || firmware >> 1) != 0) {
      Error("Invalid parameter.");
      return;
   }

   dw11 = smart | (namespace << 8) | (firmware << 9);
   issueSetFeature(handle, 0, FTR_ID_ASYN_EVENT_CONFIG, save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_0ch(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc, i;
   struct usr_io uio;
   struct iden_controller idCtrlr;
   vmk_uint64 buf[32];

   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getFeatures.featureID = FTR_ID_AUTO_PWR_TRANSITION;
   uio.cmd.cmd.getFeatures.select = select;
   uio.addr = (vmk_uintptr_t)buf;
   uio.length = 256;

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }

   if (idCtrlr.apsta.autoPowerStX == 0) {
      Error("Invalid operation: The controller doesn't support autonomous power state transitions!");
      return;
   }

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.param.cmdSpecific;
   Debug("value = %x\n", value);
   esxcli_xml_begin_output();
   xml_struct_begin("AutonomousPowerStateTransition");
   PBOOL("Autonomous Power State Transition Enable", value & 0x1);
   xml_field_begin("Autonomous Power State Transition Data");
   xml_list_begin("structure");
   for (i = 0; i < 32; i++) {
      xml_struct_begin("DataEntry");
      PINT("Power State", i);
      PINT("Idle Transition Power State", (buf[i] & 0xf8) >> 3);
      PINT("Idle Time Prior to Transition(milliseconds)", (buf[i] & 0xffffff00) >> 8);
      xml_struct_end();
   }
   xml_list_end();
   xml_field_end();
   xml_struct_end();
   esxcli_xml_end_output();
}

void getFeature_0dh(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   struct usr_io uio;
   vmk_uint32 buf[1024];
   struct iden_controller idCtrlr;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getFeatures.featureID = FTR_ID_HOST_MEM_BUFFER;
   uio.cmd.cmd.getFeatures.select = select;
   uio.addr = (vmk_uintptr_t)buf;
   uio.length = 4096;

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }

   if (idCtrlr.hmPre == 0) {
      Error("Invalid operation: The controller doesn't support the Host Memory Buffer feature!");
      return;
   }

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.param.cmdSpecific;
   Debug("value = %x\n", value);
   esxcli_xml_begin_output();
   xml_struct_begin("HostMemoryBuffer");
   xml_field_begin("Host Memory Buffer Status");
   xml_struct_begin("Status");
   PBOOL("Enable Host Memory", value & 0x1);
   PBOOL("Memory Return", value & 0x2);
   xml_struct_end();
   xml_field_end();
   xml_field_begin("Host Memory Buffer Attributes");
   xml_struct_begin("Data");
   PINTS("Host Memory Buffer Size", buf[0]);
   PULL("Host Memory Descriptor List Address", (vmk_uint64)buf[2] << 32  | buf[1]);
   PINTS("Host Memory Descriptor List Entry Count", buf[3]);
   xml_struct_end();
   xml_field_end();
   xml_struct_end();
   esxcli_xml_end_output();
}

void getFeature_0fh(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   struct usr_io uio;
   struct iden_controller idCtrlr;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getFeatures.featureID = FTR_ID_KEEP_ALIVE_TIMER;
   uio.cmd.cmd.getFeatures.select = select;

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }

   if (idCtrlr.kas == 0) {
      Error("Invalid operation: Keep Alive is not supported.");
      return;
   }

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.param.cmdSpecific;
   Debug("value = %x\n", value);

   esxcli_xml_begin_output();
   xml_struct_begin("KeepAliveTimer");
   PINTS("Keep Alive Timeout", value);
   xml_struct_end();
   esxcli_xml_end_output();
}

void setFeature_0fh(struct nvme_handle *handle, int save, int nsId, int argc, const char **argv)
{
   int   ch, rc, timeout = 0;
   char *timeoutStr = NULL;
   struct iden_controller idCtrlr;
   vmk_uint32 dw11;

   optind = 1;
   while ((ch = getopt(argc, (char *const*)argv, ":t:")) != -1) {
      switch (ch) {
         case 't':
            timeoutStr = optarg;
            break;
      }
   }

   if (timeoutStr == NULL) {
      Error("Missing parameter.");
      return;
   }
   errno = 0;
   timeout = strtol(timeoutStr, NULL, 0);
   if (errno) {
      Error("Invalid keep alive timeout value format.");
      return;
   }

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }

   if (idCtrlr.kas == 0) {
      Error("Invalid operation: Keep Alive is not supported.");
      return;
   }

   dw11 = timeout;
   issueSetFeature(handle, 0, FTR_ID_KEEP_ALIVE_TIMER, save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_80h(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   struct usr_io uio;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getFeatures.featureID = FTR_ID_SW_PROGRESS_MARKER;
   uio.cmd.cmd.getFeatures.select = select;

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.param.cmdSpecific;
   Debug("value = %x\n", value);

   esxcli_xml_begin_output();
   xml_struct_begin("SoftwareProgressMarker");
   PINTS("Pre-boot Software Load Count", value & 0xff);
   xml_struct_end();
   esxcli_xml_end_output();
}

void setFeature_80h(struct nvme_handle *handle, int save, int nsId, int argc, const char **argv)
{
   vmk_uint32 dw11;

   dw11 = 0;
   issueSetFeature(handle, 0, FTR_ID_SW_PROGRESS_MARKER, save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_81h(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   struct usr_io uio;
   vmk_uint8 buf[16];

   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getFeatures.featureID = FTR_ID_HOST_IDENTIFIER;
   uio.cmd.cmd.getFeatures.select = select;
   uio.addr = (vmk_uintptr_t)buf;
   uio.length = 16;

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.param.cmdSpecific;
   Debug("value = %x\n", value);

   esxcli_xml_begin_output();
   xml_struct_begin("HostIdentifier");
   PBOOL("Enable Extended Host Identifier", value & 0x1);
   printf("<field name=\"Host Identifier\"><string>%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x</string></field>\n",
          buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7],
          buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
   xml_struct_end();
   esxcli_xml_end_output();
}

struct Feature features[] = {
   {
      FTR_ID_ARBITRATION,
      "Arbitration",
      0,
      getFeature_01h,
      setFeature_01h,
   },
   {
      FTR_ID_PWR_MANAGEMENT,
      "Power Management",
      0,
      getFeature_02h,
      setFeature_02h,
   },
   {
      FTR_ID_LBA_RANGE_TYPE,
      "LBA Range Type",
      4096,
      getFeature_03h,
      NULL,
   },
   {
      FTR_ID_TEMP_THRESHOLD,
      "Temperature Threshold",
      0,
      getFeature_04h,
      setFeature_04h,
   },
   {
      FTR_ID_ERR_RECOVERY,
      "Error Recovery",
      0,
      getFeature_05h,
      setFeature_05h,
   },
   {
      FTR_ID_WRITE_CACHE,
      "Volatile Write Cache",
      0,
      getFeature_06h,
      setFeature_06h,
   },
   {
      FTR_ID_NUM_QUEUE,
      "Number of Queues",
      0,
      getFeature_07h,
      NULL,
   },
   {
      FTR_ID_INT_COALESCING,
      "Interrupt Coalescing",
      0,
      getFeature_08h,
      setFeature_08h,
   },
   {
      FTR_ID_INT_VECTOR_CONFIG,
      "Interrupt Vector Configuration",
      0,
      getFeature_09h,
      setFeature_09h,
   },
   {
      FTR_ID_WRITE_ATOMICITY,
      "Write Atomicity Normal",
      0,
      getFeature_0ah,
      setFeature_0ah,
   },
   {
      FTR_ID_ASYN_EVENT_CONFIG,
      "Asynchronous Event Configuration",
      0,
      getFeature_0bh,
      setFeature_0bh,
   },
   {
      FTR_ID_AUTO_PWR_TRANSITION,
      "Autonomous Power State Transition",
      256,
      getFeature_0ch,
      NULL,
   },
   {
      FTR_ID_HOST_MEM_BUFFER,
      "Host Memory Buffer",
      4096,
      getFeature_0dh,
      NULL,
   },
   {
      FTR_ID_KEEP_ALIVE_TIMER,
      "Keep Alive Timer",
      0,
      getFeature_0fh,
      setFeature_0fh,
   },
   {
      FTR_ID_SW_PROGRESS_MARKER,
      "Software Progress Marker",
      0,
      getFeature_80h,
      setFeature_80h,
   },
   {
      FTR_ID_HOST_IDENTIFIER,
      "Host Identifier",
      16,
      getFeature_81h,
      NULL,
   },
   {
      FTR_ID_RESERV_NOTIF_MASK,
      "Reservation Notification Mask",
      0,
      NULL,
      NULL,
   },
   {
      FTR_ID_RESERV_PERSIST,
      "Reservation Persistance",
      0,
      NULL,
      NULL,
   },
};
#define NUM_FEATURES       (sizeof(features)/sizeof(features[0]))

static struct Feature*
LookupFeature(int fid)
{
   int i;
   for (i = 0; i < NUM_FEATURES; i++) {
      if (features[i].fid == fid) {
         return &features[i];
      }
   }
   return NULL;
}

void
NvmePlugin_DeviceFeatureCap(int argc, const char *argv[])
{
   int                      ch, rc, i, value;
   const char              *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct usr_io            uio;
   vmk_uint8                buf[4096];

   while ((ch = getopt(argc, (char*const*)argv, ":A:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;
      }
   }

   if (vmhba == NULL) {
      Error("vmhba null");
      return;
   }

   // do stuff for nvme device namespace list -A vmhbax.
   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Failed to get adapter list.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   esxcli_xml_begin_output();
   xml_list_begin("structure");
   for (i = 0; i < NUM_FEATURES; i++) {
      memset(&uio, 0, sizeof(uio));
      uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
      uio.direction = XFER_FROM_DEV;
      uio.timeoutUs = ADMIN_TIMEOUT;
      uio.cmd.cmd.getFeatures.featureID = features[i].fid;
      uio.cmd.cmd.getFeatures.select = 0x3;
      if (features[i].useBufferLen > 0) {
         uio.addr = (vmk_uintptr_t)buf;
         uio.length = features[i].useBufferLen;
      }

      rc = Nvme_AdminPassthru(handle, &uio);

      if (rc) {
         Debug("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
         continue;
      }

      value = uio.comp.param.cmdSpecific;
      Debug("value = %x\n", value);
      xml_struct_begin("Feature");
      PSTR("Feature Identifier", features[i].desc);
      PBOOL("saveable", value & 0x1);
      PBOOL("namespace specific", value & 0x2);
      PBOOL("changeable", value & 0x4);
      xml_struct_end();
   }
   xml_list_end();
   esxcli_xml_end_output();

   Nvme_Close(handle);
}
void
NvmePlugin_DeviceFeatureGet(int argc, const char *argv[])
{
   int                      ch, fid, rc, nsId = 0;
   const char              *vmhba = NULL;
   const char              *ftr = NULL;
   const char              *sel = NULL;
   const char              *ns  = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct Feature          *feature;
   int                      select;

   while ((ch = getopt(argc, (char*const*)argv, ":A:f:n:S:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;

         case 'f':
            ftr = optarg;
            break;

         case 'n':
            ns = optarg;
            break;

         case 'S':
            sel = optarg;
            break;
      }
   }

   if (vmhba == NULL || ftr == NULL) {
      Error("vmhba or ftr null");
      return;
   }

   errno = 0;
   fid = strtol(ftr, NULL, 0);
   if (errno) {
      Error("Invalid feature id.");
      return;
   }

   if (ns != NULL) {
      errno = 0;
      nsId = strtol(ns, NULL, 0);
      if (errno || nsId <= 0) {
         Error("Invalid namespace id.");
         return;
      }
   }

   feature = LookupFeature(fid);

   if (!feature) {
      Error("Invalid feature name!");
      return;
   }

   select = LookupSelect(sel);
   if (select == -1) {
      Error("Invalid parameter: Not supported select.");
      return;
   }

   // do stuff for nvme device namespace list -A vmhbax.
   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Failed to get adapter list.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   if (nsId > 0) {
      rc = Nvme_ValidNsId(handle, nsId);
      if (rc == -1) {
         Error("Failed to validate nsId %d.", nsId);
         goto out;
      }
      if (rc == 0) {
         Error("Invalid namespace Id %d.", nsId);
         goto out;
      }
      rc = Nvme_AllocatedNsId(handle, nsId);
      if (rc == -1) {
         Error("Failed to check Namespace Id %d is created.", nsId);
         goto out;
      }
      if (rc == 0) {
         Error("Invalid parameter: Namespace %d is not created.", nsId);
         goto out;
      }
   }

   if (feature->getFeature) {
      feature->getFeature(handle, select, nsId);
   } else {
      Error("Invalid operation: Not allow to get feature %s.", feature->desc);
   }

out:
   Nvme_Close(handle);
}

void
NvmePlugin_DeviceFeatureSet(int argc, const char *argv[])
{
   int                      ch, fid, rc, nsId = 0;
   const char              *vmhba = NULL;
   const char              *ftr = NULL;
   const char              *ns  = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct iden_controller   idCtrlr;
   int                      save = 0;
   struct Feature          *feature;

   while ((ch = getopt(argc, (char*const*)argv, "-:A:f:n:S")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;

         case 'f':
            ftr = optarg;
            break;

         case 'n':
            ns = optarg;
            break;

         case 'S':
            save = 1;
            break;
      }
   }

   if (vmhba == NULL || ftr == NULL) {
      Error("Invalid argument.");
      return;
   }

   errno = 0;
   fid = strtol(ftr, NULL, 0);
   if (errno) {
      Error("Invalid feature id.");
      return;
   }

   if (ns != NULL) {
      errno = 0;
      nsId = strtol(ns, NULL, 0);
      if (errno || nsId <= 0) {
         Error("Invalid namespace id.");
         return;
      }
   }

   feature = LookupFeature(fid);

   if (!feature) {
      Error("Invalid feature name!");
      return;
   }

   // do stuff for nvme device namespace list -A vmhbax.
   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Failed to get adapter list.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      goto out;
   }

   if ((idCtrlr.cmdSupt & (1 << 4)) == 0 && save == 1) {
      Error("Invalid parameter: The controller doesn't support saving feature.");
      goto out;
   }

   if (nsId > 0) {
      rc = Nvme_ValidNsId(handle, nsId);
      if (rc == -1) {
         Error("Failed to validate nsId %d.", nsId);
         goto out;
      }
      if (rc == 0) {
         Error("Invalid namespace Id %d.", nsId);
         goto out;
      }
      rc = Nvme_AllocatedNsId(handle, nsId);
      if (rc == -1) {
         Error("Failed to check Namespace Id %d is created.", nsId);
         goto out;
      }
      if (rc == 0) {
         Error("Invalid parameter: Namespace %d is not created.", nsId);
         goto out;
      }
   }

   if (feature->setFeature) {
      feature->setFeature(handle, save, nsId, argc, argv);
   } else {
      Error("Invalid operation: Not allow to set feature %s.", feature->desc);
   }

out:
   Nvme_Close(handle);
}

void
NvmePlugin_DeviceFeatureList(int argc, const char *argv[])
{
   int                      ch, i;
   int                      rc;
   const char              *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;

   while ((ch = getopt(argc, (char *const*)argv, "A:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;

         default:
            Error("Invalid parameter.");
            return;
      }
   }

   if (vmhba == NULL) {
      Error("Invalid parameter.");
      return;
   }

   // do stuff for nvme device namespace list -A vmhbax.
   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Failed to get adapter list.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   for (i = 0; i < NUM_FEATURES; i++) {
      if (features[i].getFeature == NULL) {
         continue;
      }
      features[i].getFeature(handle, 0, 0);
   }

   Nvme_Close(handle);
}

void
NvmePlugin_DeviceFirmwareDownload(int argc, const char *argv[])
{
   int  ch;
   char *vmhba = NULL;
   char *fwPath = NULL;
   void *fwBuf = NULL;
   int  fwSize = 0;
   int  rc;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct iden_controller  *idCtrlr;

   while ((ch = getopt(argc, (char *const*)argv, "A:f:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;
	 case 'f':
	    fwPath = optarg;
	    break;
         default:
            Error("Invalid parameter.");
            return;
      }
   }

   if (vmhba == NULL || fwPath == NULL) {
      Error("Invalid parameter.");
      return;
   }

   rc = Nvme_GetAdapterList(&list);
   if (rc) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   idCtrlr = malloc(sizeof(*idCtrlr));
   if (idCtrlr == NULL) {
      Error("Out of memory.");
      goto out;
   }

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, idCtrlr);
   if (rc) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      goto out_free;
   }

   if ((idCtrlr->adminCmdSup & 0x4) == 0) {
      Error("Firmware download command is not supported.");
      goto out_free;
   }

   rc = Nvme_FWLoadImage(fwPath, &fwBuf, &fwSize);
   if (rc) {
      Error("Failed to read firmware image file.");
      goto out_free;
   }

   rc = Nvme_FWDownload(handle, fwBuf, fwSize);
   if (rc) {
      Error("Failed to download firmware, 0x%x", rc);
      goto out_free;
   } else {
      esxcli_xml_begin_output();
      xml_list_begin("string");
      printf("<string>Download firmware successfully.</string>");
      xml_list_end();
      esxcli_xml_end_output();
   }

out_free:
   if (fwBuf) {
      free(fwBuf);
   }
   free(idCtrlr);

out:
   Nvme_Close(handle);
}

void
NvmePlugin_DeviceFirmwareActivate(int argc, const char *argv[])
{
   int  ch;
   char *vmhba = NULL;
   int  slot = -1;
   int  maxSlot = 0;
   int  rc;
   int  action = -1;
   int  status = 0;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct iden_controller  *idCtrlr;

   while ((ch = getopt(argc, (char *const*)argv, "A:s:a:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;
         case 's':
            slot = atoi(optarg);
            break;
         case 'a':
            action = atoi(optarg);
            break;
         default:
            Error("Invalid parameter.");
            return;
      }
   }

   if (vmhba == NULL ||
       action < NVME_FIRMWARE_ACTIVATE_ACTION_NOACT ||
       action > NVME_FIRMWARE_ACTIVATE_ACTION_ACT_NORESET) {
      Error("Invalid parameter.");
      return;
   }

   rc = Nvme_GetAdapterList(&list);
   if (rc) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   idCtrlr = malloc(sizeof(*idCtrlr));
   if (idCtrlr == NULL) {
      Error("Out of memory.");
      goto out;
   }

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, idCtrlr);
   if (rc) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      goto out_free;
   }

   if ((idCtrlr->adminCmdSup & 0x4) == 0) {
      Error("Firmware activate command is not supported.");
      goto out_free;
   }

   maxSlot = (idCtrlr->firmUpdt & 0xf) >> 1;
   if (slot < 0 || slot > maxSlot) {
      Error("Invalid slot number.");
      goto out_free;
   }

   if (slot == 1 && (idCtrlr->firmUpdt & 0x1) &&
       (action == NVME_FIRMWARE_ACTIVATE_ACTION_NOACT ||
       action == NVME_FIRMWARE_ACTIVATE_ACTION_DLACT)) {
      Error("Invalid action: Slot 1 is read only.");
      goto out_free;
   }

   rc = Nvme_FWActivate(handle, slot, action, &status);

   if (rc == 0 && status == 0) {
      esxcli_xml_begin_output();
      xml_list_begin("string");
      if (action == NVME_FIRMWARE_ACTIVATE_ACTION_DLACT ||
          action == NVME_FIRMWARE_ACTIVATE_ACTION_ACTIVATE) {
         printf("<string>Commit firmware successfully,"
                " but activation requires reboot.</string>");
      } else {
         printf("<string>Commit firmware successfully.</string>");
      }
      xml_list_end();
      esxcli_xml_end_output();
   } else if (status == 0x10b || status == 0x110 || status == 0x111) {
      esxcli_xml_begin_output();
      xml_list_begin("string");
      printf("<string>Commit firmware successfully,"
             " but activation requires reboot.</string>");
      xml_list_end();
      esxcli_xml_end_output();
   } else {
      switch (status) {
         case 0x0:
            Error("Failed to execute the requested action, 0x%x.", rc);
            break;
         case 0x106:
            Error("Invalid firmware slot.");
            break;
         case 0x107:
            Error("Invalid firmware image.");
            break;
         case 0x112:
            Error("The frimware activation would exceed the MFTA value reported in identify controller."
                  " Please re-issue activate command with other actions using a reset.");
            break;
         case 0x113:
            Error("The image specified is being prohibited from activation by the controller for vendor specific reasons.");
            break;
         case 0x114:
            Error("The firmware image has overlapping ranges.");
            break;
         default:
            Error("Failed to commit firmware, 0x%x.", status);
            break;
      }
   }

out_free:
   free(idCtrlr);

out:
   Nvme_Close(handle);
}

void
NvmePlugin_DriverLoglevelSet(int argc, const char *argv[])
{

   int  ch;
   int  logLevel = 0;
   int  debugLevel = 0;
   BOOL setDebug = 0;
   char *debugString = NULL;
   int rc;

   while ((ch = getopt(argc, (char *const*)argv, "l:d:")) != -1) {
      switch (ch) {
         case 'l':
            logLevel = atoi(optarg);
            break;
         case 'd':
            setDebug = 1;
            debugString = optarg;
            break;
         default:
            Error("Invalid parameter.");
            return;
      }
   }

   if (logLevel < 1 || logLevel > 5) {
      Error("Invalid log level.");
      return;
   }
   if (setDebug) {
      if (debugString == NULL) {
         Error("Invalid debug level.");
         return;
      }
      if (logLevel != 5) {
         Error("Debug level is invalid when setting log level to %d.\n", logLevel);
      }
      else {
         rc = htoi(debugString, &debugLevel);
         if (rc) {
            Error("Invalid debug level.");
            return;
         }
      }
   }

   rc = Nvme_SetLogLevel(logLevel, debugLevel);
   if (rc) {
      Error("Failed to set log level, 0x%x.", rc);
   }
   else {
      esxcli_xml_begin_output();
      xml_list_begin("string");
      printf("<string>Successfully set log level to %d"
             " and debug level to 0x%x.</string>", logLevel, debugLevel);
      xml_list_end();
      esxcli_xml_end_output();
   }
}

static void
PrintCtrlrRegs(void *regs)
{
   vmk_uint64 reg64;
   vmk_uint32 reg32;

   esxcli_xml_begin_output();
   xml_struct_begin("DeviceRegs");

   reg64 = *(vmk_uint64 *)(regs + NVME_CAP);
   PULL("CAP", reg64);
   PULL("CAP.MPSMAX", (reg64 & NVME_CAP_MPSMAX_MSK64) >> NVME_CAP_MPSMAX_LSB);
   PULL("CAP.MPSMIN", (reg64 & NVME_CAP_MPSMIN_MSK64) >> NVME_CAP_MPSMIN_LSB);
   PULL("CAP.CSS", (reg64 & NVME_CAP_CSS_MSK64) >> NVME_CAP_CSS_LSB);
   PULL("CAP.NSSRS", (reg64 & NVME_CAP_NSSRS_MSK64) >> NVME_CAP_NSSRS_LSB);
   PULL("CAP.DSTRD", (reg64 & NVME_CAP_DSTRD_MSK64) >> NVME_CAP_DSTRD_LSB);
   PULL("CAP.TO", (reg64 & NVME_CAP_TO_MSK64) >> NVME_CAP_TO_LSB);
   PULL("CAP.AMS", (reg64 & NVME_CAP_AMS_MSK64) >> NVME_CAP_AMS_LSB);
   PULL("CAP.CQR", (reg64 & NVME_CAP_CQR_MSK64) >> NVME_CAP_CQR_LSB);
   PULL("CAP.MQES", reg64 & NVME_CAP_MQES_MSK64);

   reg32 = *(vmk_uint32 *)(regs + NVME_VS);
   PINTS("VS", reg32);
   PINTS("VS.MJR", (reg32 & NVME_VS_MJR_MSK) >> NVME_VS_MJR_LSB);
   PINTS("VS.MNR", (reg32 & NVME_VS_MNR_MSK) >> NVME_VS_MNR_LSB);

   PINTS("INTMS", *(vmk_uint32 *)(regs + NVME_INTMS));

   PINTS("INTMC", *(vmk_uint32 *)(regs + NVME_INTMC));

   reg32 = *(vmk_uint32 *)(regs + NVME_CC);
   PINTS("CC", reg32);
   PINTS("CC.IOCQES", (reg32 & NVME_CC_IOCQES_MSK) >> NVME_CC_IOCQES_LSB);
   PINTS("CC.IOSQES", (reg32 & NVME_CC_IOSQES_MSK) >> NVME_CC_IOSQES_LSB);
   PINTS("CC.SHN", (reg32 & NVME_CC_SHN_MSK) >> NVME_CC_SHN_LSB);
   PINTS("CC.AMS", (reg32 & NVME_CC_AMS_MSK) >> NVME_CC_AMS_LSB);
   PINTS("CC.MPS", (reg32 & NVME_CC_MPS_MSK) >> NVME_CC_MPS_LSB);
   PINTS("CC.CSS", (reg32 & NVME_CC_CSS_MSK) >> NVME_CC_CSS_LSB);
   PINTS("CC.EN", reg32 & NVME_CC_EN_MSK);

   reg32 = *(vmk_uint32 *)(regs + NVME_CSTS);
   PINTS("CSTS", reg32);
   PINTS("CSTS.PP", (reg32 & NVME_CSTS_PP_MSK) >> NVME_CSTS_PP_LSB);
   PINTS("CSTS.NSSRO", (reg32 & NVME_CSTS_NSSRO_MSK) >> NVME_CSTS_NSSRO_LSB);
   PINTS("CSTS.SHST", (reg32 & NVME_CSTS_SHST_MSK) >> NVME_CSTS_SHST_LSB);
   PINTS("CSTS.CFS", (reg32 & NVME_CSTS_CFS_MSK) >> NVME_CSTS_CFS_LSB);
   PINTS("CSTS.RDY", reg32 & NVME_CSTS_RDY_MSK);

   PINTS("NSSR", *(vmk_uint32 *)(regs + NVME_NSSR));

   reg32 = *(vmk_uint32 *)(regs + NVME_AQA);
   PINTS("AQA", reg32);
   PINTS("AQA.ACQS", (reg32 & NVME_AQA_CQS_MSK) >> NVME_AQA_CQS_LSB);
   PINTS("AQA.ASQS", reg32 & NVME_AQA_SQS_MSK);

   PULL("ASQ", *(vmk_uint64 *)(regs + NVME_ASQ));
   PULL("ACQ", *(vmk_uint64 *)(regs + NVME_ACQ));
   PINTS("CMBLOC", *(vmk_uint32 *)(regs + NVME_CMBLOC));
   PINTS("CMBSZ", *(vmk_uint32 *)(regs + NVME_CMBSZ));
   xml_struct_end();
   esxcli_xml_end_output();
}

void
NvmePlugin_DeviceRegisterGet(int argc, const char *argv[])
{
   int                      ch;
   int                      rc;
   const char              *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct usr_io            uio;
   vmk_uint8                regs[8192];

   while ((ch = getopt(argc, (char *const*)argv, "A:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;
         default:
            Error("Invalid parameter.");
            return;
      }
   }

   if (vmhba == NULL) {
      Error("Invalid parameter.");
      return;
   }

   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   memset(&uio, 0, sizeof(uio));
   uio.addr = (vmk_uintptr_t)&regs;
   uio.length = sizeof(regs);

   rc = Nvme_Ioctl(handle, NVME_IOCTL_DUMP_REGS, &uio);
   if (!rc) {
      rc = uio.status;
   }

   if (rc) {
      Error("Failed to get controller registers, 0x%x.", rc);
   } else {
      PrintCtrlrRegs(regs);
   }

   Nvme_Close(handle);
}

void
NvmePlugin_DeviceTimeoutSet(int argc, const char *argv[])
{
   int ch;
   int timeout = -1;
   int rc;
   const char *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle *handle;

   while ((ch = getopt(argc, (char *const*)argv, "A:t:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;
         case 't':
            timeout = atoi(optarg);
            break;
         default:
            Error("Invalid parameter.");
            return;
      }
   }

   if (vmhba == NULL || timeout < 0 || timeout > 40) {
      Error("Invalid parameter.");
      return;
   }

   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   rc = Nvme_SetTimeout(handle, timeout);
   if (rc) {
      Error("Failed to set timeout, 0x%x.", rc);
   } else {
      esxcli_xml_begin_output();
      xml_list_begin("string");
      printf("<string>Timeout is set to %d.</string>", timeout);
      xml_list_end();
      esxcli_xml_end_output();
   }

   Nvme_Close(handle);
}

void
NvmePlugin_DeviceTimeoutGet(int argc, const char *argv[])
{
   int timeout = 0;
   int rc;
   int ch;
   const char *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle *handle;

   while ((ch = getopt(argc, (char *const*)argv, "A:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;
         default:
            Error("Invalid parameter.");
            return;
      }
   }

   if (vmhba == NULL) {
      Error("Invalid parameter.");
      return;
   }

   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Failed to open device.");
      return;
   }

   rc = Nvme_GetTimeout(handle, &timeout);
   if (rc) {
      Error("Failed to get timeout, 0x%x.", rc);
   } else {
      esxcli_xml_begin_output();
      xml_list_begin("string");
      if (timeout == 0) {
         printf("<string>Current timeout is 0. Timeout checker is disabled.</string>");
      } else {
         printf("<string>Current timeout is %d s.</string>", timeout);
      }
      xml_list_end();
      esxcli_xml_end_output();
   }

   Nvme_Close(handle);
}

typedef void (*CommandHandlerFunc)(int argc, const char *argv[]);
typedef enum CommandType {NVME_NORMAL = 0, NVME_NS_MGMT,} CommandType;

struct Command {
   const char           *op;
   CommandHandlerFunc    fn;
   CommandType           type;
};

static struct Command commands[] = {
   {
      "nvme.device.list",
      NvmePlugin_DeviceList,
      NVME_NORMAL,
   },
   {
      "nvme.device.get",
      NvmePlugin_DeviceGet,
      NVME_NORMAL,
   },
   {
      "nvme.device.namespace.list",
      NvmePlugin_DeviceNsList,
      NVME_NORMAL,
   },
   {
      "nvme.device.namespace.get",
      NvmePlugin_DeviceNsGet,
      NVME_NORMAL,
   },
   {
      "nvme.device.namespace.format",
      NvmePlugin_DeviceNsFormat,
      NVME_NORMAL,
   },
   {
      "nvme.device.log.get",
      NvmePlugin_DeviceLogGet,
      NVME_NORMAL,
   },
   {
      "nvme.device.feature.list",
      NvmePlugin_DeviceFeatureList,
      NVME_NORMAL,
   },
   {
      "nvme.device.feature.cap",
      NvmePlugin_DeviceFeatureCap,
      NVME_NORMAL,
   },
   {
      "nvme.device.feature.get",
      NvmePlugin_DeviceFeatureGet,
      NVME_NORMAL,
   },
   {
      "nvme.device.feature.set",
      NvmePlugin_DeviceFeatureSet,
      NVME_NORMAL,
   },
   {
      "nvme.device.firmware.download",
      NvmePlugin_DeviceFirmwareDownload,
      NVME_NORMAL,
   },
   {
      "nvme.device.firmware.activate",
      NvmePlugin_DeviceFirmwareActivate,
      NVME_NORMAL,
   },
   {
      "nvme.driver.loglevel.set",
      NvmePlugin_DriverLoglevelSet,
      NVME_NORMAL,
   },
   {
      "nvme.device.register.get",
      NvmePlugin_DeviceRegisterGet,
      NVME_NORMAL,
   },
   {
      "nvme.device.timeout.set",
      NvmePlugin_DeviceTimeoutSet,
      NVME_NORMAL,
   },
   {
      "nvme.device.timeout.get",
      NvmePlugin_DeviceTimeoutGet,
      NVME_NORMAL,
   },
   {
      "nvme.device.namespace.create",
      NvmePlugin_DeviceNsCreate,
      NVME_NS_MGMT,
   },
   {
      "nvme.device.namespace.delete",
      NvmePlugin_DeviceNsDelete,
      NVME_NS_MGMT,
   },
   {
      "nvme.device.namespace.attach",
      NvmePlugin_DeviceNsAttach,
      NVME_NS_MGMT,
   },
   {
      "nvme.device.namespace.detach",
      NvmePlugin_DeviceNsDetach,
      NVME_NS_MGMT,
   },
   {
      "nvme.device.controller.list",
      NvmePlugin_DeviceListController,
      NVME_NS_MGMT,
   },
   {
      "nvme.device.namespace.online",
      NvmePlugin_DeviceNsOnline,
      NVME_NS_MGMT,
   },
   {
      "nvme.device.namespace.offline",
      NvmePlugin_DeviceNsOffline,
      NVME_NS_MGMT,
   },


};

#define NUM_COMMANDS       (sizeof(commands)/sizeof(commands[0]))
#define MAX_COMMAND_LEN    (32)

static inline int
NvmeLookupFunction(const char *op)
{
   int i;
   for (i = 0; i < NUM_COMMANDS; i++) {
      if (strncmp(op, commands[i].op, MAX_COMMAND_LEN) == 0) {
         return i;
      }
   }
   return -1;
}

static inline BOOL
NvmeFunctionEnabled(int fnIdx)
{
   /* All functions are enabled by default. Return false to disable the specific one. */
   return true;
}

int
main(int argc, const char * argv[]) {

   const char        *op;
   int                rc;
   int                fnIdx;

   if (argc < 3) {
      Error("Invalid parameter.\n");
      rc = -EINVAL;
      goto out;
   }

   if (strncmp(argv[1], "--op", MAX_COMMAND_LEN) != 0) {
      Error("Invalid parameter.\n");
      rc = -EINVAL;
      goto out;
   }

   op = argv[2];

   argc -= 2;
   argv += 2;

   fnIdx = NvmeLookupFunction(op);
   if (fnIdx == -1) {
      Error("Invalid parameter.\n");
      rc = -EINVAL;
      goto out;
   }

   if (NvmeFunctionEnabled(fnIdx) == false) {
      Error("This operation is disabled.\n");
      rc = -EINVAL;
      goto out;
   }

   commands[fnIdx].fn(argc, argv);
   rc = 0;

out:
   return rc;
}

/* Required by uw lib linking */
void
Panic(const char *fmt,...)
{
   va_list args;

   va_start(args, fmt);
   vfprintf(stderr, fmt, args);
   va_end(args);

   exit(-1);
}
