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

#include <vmkapi.h>
#include "esxcli_xml.h"
#include "nvme_lib.h"

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

typedef enum __bool {false = 0, true,} Bool;
/* We assume the command and device name length is less than 150 and 100 */
#define MAX_CMD_LEN 150
#define MAX_DEV_NAME_LEN 100

#define MAX_ERROR_LOG_ENTRIES 64

static const char *nvmNsRelPerf[] = {
   "Best performance",
   "Better performance",
   "Good performance",
   "Degraded performance",
};


static void
PrintIdentifyCtrlr(struct iden_controller *id)
{
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
   PBOOL("Optional Namespace Attribute Changed Event Support", id->oaes.nsChgEvent);
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
   PINT("Max Submission Queue Entry Size", 1 << ((id->subQSize & 0xf0) >> 4));
   PINT("Required Submission Queue Entry Size", 1 << (id->subQSize & 0xf));
   PINT("Max Completion Queue Entry Size", 1 << ((id->compQSize & 0xf0) >> 4));
   PINT("Required Completion Queue Entry Size", 1 << (id->compQSize & 0xf));
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
   PBOOL("SGL Length Able to Larger than Data Amount", id->sgls.sglsLargerThanData);
   PBOOL("SGL Length Shall Be Equal to Data Amount", (id->sgls.sglsLargerThanData == 0));
   PBOOL("Byte Aligned Contiguous Physical Buffer of Metadata Support",
         id->sgls.byteAlignedContPhyBufSup);
   PBOOL("SGL Bit Bucket Descriptor Support", id->sgls.sglsBitBuckDescSup);
   PBOOL("SGL for NVM Command Set Support", id->sgls.sglsSup);
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
   PINT("PI Enabled Type", idNs->dataProtSet & 0x3);
   if (idNs->dataProtSet & 0x3) {
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
   xml_field_begin("Namespace Globally Unique Identifier");
   printf("<string>0x%.16" VMK_FMT64 "x%.16" VMK_FMT64 "x</string>\n",
          *(vmk_uint64 *)idNs->nguid.extId,
          *(vmk_uint64 *)idNs->nguid.vendorSpecExtId);
   xml_field_end();
   PULL("IEEE Extended Unique Identifier", (unsigned long long int)idNs->eui64);
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

   s1 = strstr(buf, "state:active");
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

void
NvmePlugin_DeviceList(int argc, const char *argv[])
{
   struct nvme_adapter_list list;
   int    rc;
   int    i;

   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Failed to get adapter list: 0x%x.", rc);
      return;
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
NvmePlugin_DeviceNsList(int argc, const char *argv[])
{
   int                      ch, i, numNs;
   int                      rc;
   const char              *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct iden_controller  *idCtrlr;
   char runtimeName[MAX_DEV_NAME_LEN];
   char (*devNames)[MAX_DEV_NAME_LEN] = NULL;
   int  *statusFlags = NULL;
   struct usr_io uio;
   VMK_ReturnStatus status;

   while ((ch = getopt(argc, (char *const*)argv, "A:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;

         default:
            Error("Invalid argument.");
            return;
      }
   }

   if (vmhba == NULL) {
      Error("Invalid argument.");
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
      Error("Adapter not found.");
      return;
   }

   idCtrlr = malloc(sizeof(*idCtrlr));
   if (idCtrlr == NULL) {
      Error("Out of memory.");
      goto out;
   }

   rc = Nvme_Identify(handle, -1, idCtrlr);
   if (rc != 0) {
      Error("Failed to get adapter information, 0x%x.", rc);
      goto out_free;
   }

   numNs = (int)idCtrlr->numNmspc;

   devNames = (char(*)[MAX_DEV_NAME_LEN])malloc(numNs * sizeof(char) * MAX_DEV_NAME_LEN);
   statusFlags = (int *)malloc(numNs * sizeof(int));
   memset(devNames, 0, numNs * sizeof(*devNames));
   memset(statusFlags, 0, numNs * sizeof(*statusFlags));

   for (i = 1; i <= numNs; i++) {
      snprintf(runtimeName, MAX_DEV_NAME_LEN, "%s:C0:T0:L%d", vmhba, i-1);
      status = GetDeviceName(runtimeName, devNames[i-1], MAX_DEV_NAME_LEN);
      if (status == VMK_FAILURE) {
         Error("Failed to get device name of namespace %d.", i);
         goto out_free;
      }

      memset(&uio, 0, sizeof(uio));
      uio.namespaceID = i;
      rc = Nvme_Ioctl(handle, NVME_IOCTL_GET_NS_STATUS, &uio);
      if (rc) {
         Error("Failed to get device status of namespace %d.", i);
         goto out_free;
      }

      if (status == VMK_NOT_FOUND && uio.status == 0) {
         /* Reach here mostly because the path is not claimed by upper layer.*/
         snprintf(devNames[i-1], MAX_DEV_NAME_LEN, "N/A");
      }
      statusFlags[i-1] = uio.status;
   }

   esxcli_xml_begin_output();
   xml_list_begin("structure");
   for (i = 0; i < numNs; i++) {
      xml_struct_begin("NamespaceList");
      PINT("Namespace ID", i+1);
      if (statusFlags[i]) {
         PSTR("Status", "Offline");
         PSTR("Device Name", "N/A");
      } else {
         PSTR("Status", "Online");
         PSTR("Device Name", devNames[i]);
      }
      xml_struct_end();
   }
   xml_list_end();
   esxcli_xml_end_output();

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
   int                      ch, numNs, nsId = 0;
   int                      rc;
   const char              *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct iden_controller  *idCtrlr;
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
            Error("Invalid argument.");
            return;
      }
   }

   if (vmhba == NULL) {
      Error("Invalid argument.");
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
      Error("Adapter not found.");
      return;
   }

   idCtrlr = malloc(sizeof(*idCtrlr));
   if (idCtrlr == NULL) {
      Error("Out of memory.");
      goto out;
   }

   idNs = malloc(sizeof(*idNs));
   if (idNs == NULL) {
      Error("Out of memory.");
      goto out_free_idCtrlr;
   }

   rc = Nvme_Identify(handle, -1, idCtrlr);
   if (rc != 0) {
      Error("Failed to get adapter information, 0x%x.", rc);
      goto out_free_all;
   }

   numNs = (int)idCtrlr->numNmspc;

   if (nsId > numNs || nsId <= 0) {
      Error("Invalid namespace Id.");
      goto out_free_all;
   } else {
      rc = Nvme_Identify(handle, nsId, idNs);
      if (rc) {
         Error("Failed to get identify data for namespace %d, %s.", nsId, strerror(rc));
      } else {
         PrintIdentifyNs(idNs);
      }
   }

out_free_all:
   free(idNs);
out_free_idCtrlr:
   free(idCtrlr);
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
            Error("Invalid argument.");
            return;
      }
   }

   if (vmhba == NULL) {
      Error("Invalid argument.");
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
      Error("Adapter not found.");
      return;
   }

   id = malloc(sizeof(*id));
   if (id == NULL) {
      Error("Out of memory.");
      goto out;
   }

   rc = Nvme_Identify(handle, -1, id);
   if (rc != 0) {
      Error("Failed to get adapter information, 0x%x.", rc);
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
   int  nsid = -1;
   int  f = -1, s = -1, l = -1, p = -1, m = -1;
   int  rc;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct iden_controller  *idCtrlr;
   char cmd[MAX_CMD_LEN];
   char runtimeName[MAX_DEV_NAME_LEN];
   int ch;
   struct usr_io uio;
   VMK_ReturnStatus status;
   char deviceName[MAX_DEV_NAME_LEN];

   while ((ch = getopt(argc, (char *const*)argv, "A:n:f:s:p:l:m:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;
         case 'n':
            nsid = atoi(optarg);
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
            Error("Invalid argument.");
            return;
      }
   }

   if (vmhba == NULL      || 
       nsid < 1           || 
       (f < 0 || f > 16)  ||
       (s < 0 || s > 2)   ||
       (p < 0 || p > 3)   ||
       (l < 0 || l > 1)   ||
       (m < 0 || m > 1))
   {
      Error("Invalid argument.");
      return;
   }

   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Adapter not found.");
      return;
   }

   idCtrlr = malloc(sizeof(*idCtrlr));
   if (idCtrlr == NULL) {
      Error("Out of memory.");
      goto out;
   }

   rc = Nvme_Identify(handle, -1, idCtrlr);
   if (rc != 0) {
      Error("Failed to get adapter information, 0x%x.", rc);
      goto out_free;
   }

   if((idCtrlr->adminCmdSup & 0x2) == 0) {
      Error("NVM Format command is not supported.");
      goto out_free;
   }

   if(nsid > (int)idCtrlr->numNmspc) {
      Error("Invalid Namespace ID.");
      goto out_free;
   }

   /* Check the namespace status.*/
   memset(&uio, 0, sizeof(uio));
   uio.namespaceID = nsid;
   rc = Nvme_Ioctl(handle, NVME_IOCTL_GET_NS_STATUS, &uio);
   if (rc) {
      Error("Failed to get device status of namespace %d.", nsid);
      goto out_free;
   }
   
   /* If namespace is online, Offline namespace if the namespace is not busy.*/
   if (uio.status == 0) { 
      snprintf(runtimeName, MAX_DEV_NAME_LEN, "%s:C0:T0:L%d", vmhba, nsid-1);
      status = GetDeviceName(runtimeName, deviceName, MAX_DEV_NAME_LEN);
      if (status == VMK_FAILURE) {
         Error("Failed to get device name of namespace %d.", nsid);
         goto out_free;
      }
      /* If GetDeviceName returns VMK_NOT_FOUND, it indicates that the path is dead or cannot be seen
       * by upper layer for some reason. It should be OK to directly do offline operation under this case.*/
      if (status == VMK_OK) {
         snprintf(cmd, MAX_CMD_LEN, "esxcli storage core claiming unclaim -t path -p %s", runtimeName);
         rc = ExecuteCommand(cmd);
         if (rc) {
            Error("Failed to format since the namespace is still in use.");
            goto out_free;
         }
      }
      memset(&uio, 0, sizeof(uio));
      uio.namespaceID = nsid;
      rc = Nvme_Ioctl(handle, NVME_IOCTL_SET_NS_OFFLINE, &uio);
      if (rc || uio.status) {
         Error("Failed to offline namespace.");
         goto out_reclaim;
      }
   }

   rc = Nvme_FormatNvm(handle, s, l, p, m, f, nsid);
   if (rc) {
      Error("Format fails or timeout, 0x%x. Offline namespace.", rc);
      goto out_free;
   } else { 
      memset(&uio, 0, sizeof(uio));
      uio.namespaceID = nsid;
      rc = Nvme_Ioctl(handle, NVME_IOCTL_UPDATE_NS, &uio);
      if (rc || uio.status) {
         Error("Format successfully, but failed to update namespace attributes after"
               " format. Offline namespace.");
         goto out_free;
      }
   }

   memset(&uio, 0, sizeof(uio));
   uio.namespaceID = nsid;
   rc = Nvme_Ioctl(handle, NVME_IOCTL_SET_NS_ONLINE, &uio);
   if (rc || uio.status) {
      Error("Format and update namespace attributes successfully,"
            " but failed to online namespace.");
      goto out_free;
   }

   snprintf(cmd, MAX_CMD_LEN, "esxcli storage filesystem rescan");
   rc = ExecuteCommand(cmd);
   if (rc) {
      Error("Format, update namesapce attributes and online namespace successfully,"
            " but failed to rescan the filesystem. A stale entry may exist.");
      goto out_free;
   }

   esxcli_xml_begin_output();
   xml_list_begin("string");
   xml_format("string", "Format successfully!");
   xml_list_end();
   esxcli_xml_end_output();

out_reclaim:
   snprintf(cmd, MAX_CMD_LEN, "esxcfg-rescan -a %s", vmhba);
   rc = ExecuteCommand(cmd);

out_free:
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
   int  nsid = -1;
   int  elpe = -1;
   Bool setNsid = 0;
   Bool setElpe = 0;
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
            nsid = atoi(optarg);
            setNsid = 1;
            break;
         case 'e':
            elpe = atoi(optarg);
            setElpe = 1;
            break;
         default:
            Error("Invalid argument.");
            return;
      }
   }

   if (vmhba == NULL      ||
       (lid < 1 || lid > 3))
   {
      Error("Invalid argument.");
      return;
   }

   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Adapter not found.");
      return;
   }

   idCtrlr = malloc(sizeof(*idCtrlr));
   if (idCtrlr == NULL) {
      Error("Out of memory.");
      goto out;
   }

   rc = Nvme_Identify(handle, -1, idCtrlr);
   if (rc != 0) {
      Error("Failed to get adapter information, 0x%x.", rc);
      goto out_free;
   }
   maxErrorLogEntries = (int)idCtrlr->errLogPgEntr + 1;
   if (maxErrorLogEntries > MAX_ERROR_LOG_ENTRIES) {
      maxErrorLogEntries = MAX_ERROR_LOG_ENTRIES;
   }

   /* Check the optional parameters: nsid and eple.*/
  if (setNsid) {
      if (lid == GLP_ID_SMART_HEALTH && (idCtrlr->logPgAttrib & 0x1)) {
         if (nsid < 1 || nsid > (int)idCtrlr->numNmspc) {
            Error("Invalid namespace ID.");
            goto out_free;
         }
      } else {
         Error("This log page is not supported on a per namespace basis.");
         goto out_free;
      }
   }
   if (setElpe) {
      if (lid == GLP_ID_ERR_INFO) {
         if (elpe < 1 || elpe > maxErrorLogEntries) {
            Error("Invalid error log page entries. The supported range is [1, %d].", maxErrorLogEntries);
            goto out_free;
         }
      } else {
         Error("Invalid argument.");
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
         uio.cmd.header.namespaceID = nsid;
         uio.cmd.cmd.getLogPage.numDW = GLP_LEN_SMART_HEALTH / 4 - 1;
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
         Error("Invalid argument.");
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

#define MAX_NUM_FTR 12
const char ftrList[MAX_NUM_FTR][VMK_MISC_NAME_MAX] = {
   "arbitration",
   "pwr_management",
   "lba_range_type",
   "temp_threshold",
   "err_recovery",
   "write_cache",
   "num_queue",
   "int_coalescing",
   "int_vector_config",
   "write_atomicity",
   "asyn_event_config",
   "sw_progress_marker"
};

static int
LookupFtrId(const char *ftr)
{
   int i;
   for(i = 0; i < MAX_NUM_FTR; i++) {
      if (strncmp(ftr, ftrList[i], VMK_MISC_NAME_MAX) == 0) {
         return i == 11 ? FTR_ID_SW_PROGRESS_MARKER : i + 1;
      }
   }
   return 0;
}

static void
GetFeature(struct nvme_handle *handle, struct usr_io *uiop, int fid)
{
   int value, rc, vectNum, i;
   struct usr_io uioVect;
   struct iden_controller  *idCtrlr;
   memset(uiop, 0, sizeof(*uiop));

   uiop->cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
   uiop->direction = XFER_FROM_DEV;
   uiop->timeoutUs = ADMIN_TIMEOUT;
   uiop->cmd.cmd.getFeatures.featureID = fid;
   if (fid == FTR_ID_INT_VECTOR_CONFIG) {
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
         uiop->cmd.cmd.getFeatures.numSubQReq = i;
         rc = Nvme_AdminPassthru(handle, uiop);
         if (rc) {
            Error("Failed to get config of interrupt vector %d\n.", i);
            continue;
         }
         value = uiop->comp.param.cmdSpecific;
         xml_struct_begin("InterruptVectorConfiguration");
         PINT("Interrupt Vector", value & 0xffff);
         PBOOL("Coalescing Disable", (value & 0x10000) >> 16);
         xml_struct_end();
      }
      xml_list_end();
      esxcli_xml_end_output();
      return;
   }

   if (fid == FTR_ID_WRITE_CACHE) {
      idCtrlr = malloc(sizeof(*idCtrlr));
      if (idCtrlr == NULL) {
         Error("Out of memory.");
         return;
      }

      rc = Nvme_Identify(handle, -1, idCtrlr);
      if (rc != 0) {
         Error("Failed to get controller identify information, 0x%x.", rc);
         free(idCtrlr);
         return;
      }

      if ((idCtrlr->volWrCache & 0x1) == 0) {
         Error("Failed to get this feature: controller has no write cache!");
         free(idCtrlr);
         return;
      }
   }

   rc = Nvme_AdminPassthru(handle, uiop);

   if (rc) {
      Error("Failed to get feature, 0x%x.", rc);
      return;
   }

   value = uiop->comp.param.cmdSpecific;
   Debug("value = %x\n", value);
   esxcli_xml_begin_output();
   switch (fid)
   {
   case FTR_ID_ARBITRATION:
      xml_struct_begin("Arbitration");
      PINT("Arbitration Burst", value & 0x7);
      PINT("Low Priority Weight", (value & 0xff00) >> 8);
      PINT("Medium Priority Weight", (value & 0xff0000) >> 16);
      PINT("High Priority Weight", (value & 0xff000000) >> 24); 
      break;

   case FTR_ID_PWR_MANAGEMENT:
      xml_struct_begin("PowerManagement");
      PINT("Power State", value & 0x1f);
      break;

//   case FTR_ID_LBA_RANGE_TYPE:
//      PrintString("LBA RANGE TYPE:\n   Unsupported Feature\n");
//      break;

   case FTR_ID_TEMP_THRESHOLD:
      xml_struct_begin("TemperatureThreshold");
      PINT("Temperature Threshold", value & 0xffff);
      break;

   case FTR_ID_ERR_RECOVERY:
      xml_struct_begin("ErrorRecovery");
      PINT("Time Limited Error Recovery", value & 0xffff);
      break;

   case FTR_ID_WRITE_CACHE:
      xml_struct_begin("VolatileWriteCache");
      PBOOL("Volatile Write Cache Enabled", value & 0x1);
      break;

   case FTR_ID_NUM_QUEUE:
      xml_struct_begin("NumberOfQueue");
      PINT("Number of Submission Queues Allocated", value & 0xffff);
      PINT("Number of Completion Queues Allocated", (value & 0xffff0000) >> 16);
      break;

   case FTR_ID_INT_COALESCING:
      xml_struct_begin("InterruptCoalescing");
      PINT("Aggregation Time", (value & 0xff00) >> 8);
      PINT("Aggregation Threshold", value & 0xff);
      break;

   case FTR_ID_WRITE_ATOMICITY:
      xml_struct_begin("WriteAtomicity");
      PBOOL("Disable Normal", value & 0x1);
      break;

   case FTR_ID_ASYN_EVENT_CONFIG:
      xml_struct_begin("AsyncEventConfiguration");
      PBOOL("Available Spare Space", value & 0x1);
      PBOOL("Temperature", (value & 0x2) >> 1);
      PBOOL("Media Error", (value & 0x4) >> 2);
      PBOOL("Read Only Mode", (value & 0x8) >> 3);
      PBOOL("Backup Device Fail", (value & 0x10) >> 4);
      break;

//   case FTR_ID_SW_PROGRESS_MARKER:
//      PrintString("SOFTWARE PROGRESS MARKER:\n   Unsupported Feature\n");
//      break;

   default:
      break;
   }

   xml_struct_end();
   esxcli_xml_end_output();
   return;
}

void
NvmePlugin_DeviceFeatureGet(int argc, const char *argv[])
{
   int                      ch, fid, rc;
   const char              *vmhba = NULL;
   const char              *ftr = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct usr_io uio;

   while ((ch = getopt(argc, (char *const*)argv, "A:f:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;

         case 'f':
            ftr = optarg;
            break;

         default:
            Error("Invalid argument.");
            return;
      }
   }

   if (vmhba == NULL || ftr == NULL) {
      Error("Invalid argument.");
      return;
   }

   fid = LookupFtrId(ftr);

   if (!fid) {
      Error("Invalid feature name!");
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
      Error("Adapter not found.");
      return;
   }

   GetFeature(handle, &uio, fid);

   Nvme_Close(handle);
}

void
NvmePlugin_DeviceFeatureSet(int argc, const char *argv[])
{
   int                      ch, fid, rc, value = 0, value2 = 0, value3 = 0, value4 = 0;
   const char              *vmhba = NULL;
   const char              *ftr = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct usr_io            uio;
   struct iden_controller  *idCtrlr;
   struct usr_io            uioVect;
   int                      vectNum;
   vmk_uint64               regs;
   struct usr_io            uioReg;
   Bool                     setX = false;
   Bool                     setY = false;
   Bool                     setZ = false;

   while ((ch = getopt(argc, (char *const*)argv, "A:f:v:x:y:z:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;

         case 'f':
            ftr = optarg;
            break;

         case 'v':
            value = atoi(optarg);
            break;

         case 'x':
            value2 = atoi(optarg);
            setX = true;
            break;

         case 'y':
            value3 = atoi(optarg);
            setY = true;
            break;

         case 'z':
            value4 = atoi(optarg);
            setZ = true;
            break;

         default:
            Error("Invalid argument.");
            return;
      }
   }

   if (vmhba == NULL || ftr == NULL) {
      Error("Invalid argument.\n");
      return;
   }

   fid = LookupFtrId(ftr);

   if (!fid) {
      Error("Invalid feature name!");
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
      Error("Adapter not found.");
      return;
   }

   memset(&uio, 0, sizeof(uio));

   uio.cmd.header.opCode = NVM_ADMIN_CMD_SET_FEATURES;
   uio.direction = XFER_TO_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.setFeatures.featureID = fid;

   switch (fid)
   {
   case FTR_ID_TEMP_THRESHOLD:
   case FTR_ID_ERR_RECOVERY:
      if ((value >> 16) != 0) {
         Error("Invalid parameter.");
         goto out;
      }
      uio.cmd.cmd.setFeatures.numSubQReq = value;
      break;

   case FTR_ID_WRITE_ATOMICITY:
      if ((value >> 1) != 0) {
         Error("Invalid parameter.");
         goto out;
      }
      uio.cmd.cmd.setFeatures.numSubQReq = value;
      break;

   case FTR_ID_ASYN_EVENT_CONFIG:
      if ((value >> 8) != 0) {
         Error("Invalid parameter.");
         goto out;
      }
      uio.cmd.cmd.setFeatures.numSubQReq = value;
      break;


   case FTR_ID_ARBITRATION:
      if (!setX || !setY || !setZ) {
         Error("Missing parameter.");
         goto out;
      }

      if ((value >> 3 | value2 >> 8 | value3 >> 8 | value4 >> 8) != 0) {
         Error("Invalid parameter.");
         goto out;
      }

      memset(&uioReg, 0, sizeof(uioReg));
      uioReg.addr = (vmk_uintptr_t)&regs;
      uioReg.length = sizeof(regs);

      rc = Nvme_Ioctl(handle, NVME_IOCTL_DUMP_REGS, &uioReg);
      if (rc) {
         Error("Failed to get controller registers, 0x%x.", rc);
         goto out;
      }

      if ((regs & NVME_CAP_AMS_MSK64) >> NVME_CAP_AMS_LSB == 0) {
         if (value2 || value3 || value4) {
            Error("Invalid parameter. Controller only support Round Robin arbitration"
                  " mechanism, Low/Medium/High Priority Weight must be set to 0.");
            goto out;
         }
      }

      uio.cmd.cmd.setFeatures.numSubQReq = value | value2 << 8;
      uio.cmd.cmd.setFeatures.numCplQReq = value3 | value4 << 8;
      break;

   case FTR_ID_INT_COALESCING:
      if (!setX) {
         Error("Missing parameter.");
         goto out;
      }
 
      if ((value >> 8 | value2 >> 8) != 0) {
         Error("Invalid parameter.");
         goto out;
      }
      uio.cmd.cmd.setFeatures.numSubQReq = value | value2 << 8;
      break;

   case FTR_ID_PWR_MANAGEMENT:
   case FTR_ID_WRITE_CACHE:
      idCtrlr = malloc(sizeof(*idCtrlr));
      if (idCtrlr == NULL) {
         Error("Out of memory.");
         goto out;
      }
   
      rc = Nvme_Identify(handle, -1, idCtrlr);
      if (rc != 0) {
         Error("Failed to get adapter information, 0x%x.", rc);
         free(idCtrlr);
         goto out;
      }

      if (fid == FTR_ID_PWR_MANAGEMENT && (value > idCtrlr->numPowerSt || value < 0)) {
         Error("Invalid parameter: power state setting is beyond supported: %d!",
               idCtrlr->numPowerSt);
         free(idCtrlr);
         goto out;
      }

      if (fid == FTR_ID_WRITE_CACHE && (idCtrlr->volWrCache & 0x1) == 0) {
         Error("Failed to set this feature: controller has no write cache!");
         free(idCtrlr);
         goto out;
      }

      if (fid == FTR_ID_WRITE_CACHE && (value >> 1) != 0) {
         Error("Invalid parameter.");
         free(idCtrlr);
         goto out;
      }

      uio.cmd.cmd.setFeatures.numSubQReq = value;
      free(idCtrlr);
      break;

   case FTR_ID_INT_VECTOR_CONFIG:
      if (!setX) {
         Error("Missing parameter.");
         goto out;
      }
 
      memset(&uioVect, 0, sizeof(uioVect));
      rc = Nvme_Ioctl(handle, NVME_IOCTL_GET_INT_VECT_NUM, &uioVect);
      if (rc) {
         Error("Failed to get controller interrupt vector number.");
         goto out;
      }

      vectNum = uioVect.length;
      Debug("vectNum: %d\n", vectNum);
      if (value < 0 || value > vectNum) {
         Error("Invalid parameter: interrupt vector number is beyond supported: %d!",
               vectNum);
         goto out;
      }

      if ((value2 >> 1) != 0) {
         Error("Invalid parameter.");
         goto out;
      }

      if (value == 0) {
         Error("Invalid parameter: interrupt coalescing is not supported for admin queue!");
         goto out;
      }
      uio.cmd.cmd.setFeatures.numSubQReq = value;
      uio.cmd.cmd.setFeatures.numCplQReq = value2;
      break;

   case FTR_ID_NUM_QUEUE:
      Error("Unable to set this feature after controller initialization.\n");
      goto out;

   case FTR_ID_LBA_RANGE_TYPE:
   case FTR_ID_SW_PROGRESS_MARKER:
      Error("Unsupported feature.\n");
      goto out;

   default:
      Error("Invalid feature to set.");
      goto out;
   }

   rc = Nvme_AdminPassthru(handle, &uio);
   if (rc) {
      Error("Failed to set feature info, %s.", strerror(rc));
   } else {
      esxcli_xml_begin_output();
      xml_list_begin("string");
      xml_format("string", "Feature set successfully!");
      xml_list_end();
      esxcli_xml_end_output();
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
   struct usr_io            uio;

   while ((ch = getopt(argc, (char *const*)argv, "A:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;

         default:
            Error("Invalid argument.");
            return;
      }
   }

   if (vmhba == NULL) {
      Error("Invalid argument.");
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
      Error("Adapter not found.");
      return;
   }

   for (i = 1; i <= MAX_NUM_FTR; i++) {
      if (i == 12) {
         i = FTR_ID_SW_PROGRESS_MARKER;
      }
      GetFeature(handle, &uio, i);
   }

   Nvme_Close(handle);
}

void
NvmePlugin_DeviceFirmwareDownload(int argc, const char *argv[])
{
   int  ch;
   char *vmhba = NULL;
   char *fwPath = NULL;
   int  slot = -1;
   int  maxSlot = 0;
   void *fwBuf = NULL;
   int  fwSize = 0;
   int  rc;
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct iden_controller  *idCtrlr;

   while ((ch = getopt(argc, (char *const*)argv, "A:f:s:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;
	 case 'f':
	    fwPath = optarg;
	    break;
         case 's':
            slot = atoi(optarg);
            break;
         default:
            Error("Invalid argument.");
            return;
      }
   }

   if (vmhba == NULL || fwPath == NULL) {
      Error("Invalid argument.");
      return;
   }

   rc = Nvme_GetAdapterList(&list);
   if (rc) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Adapter not found.");
      return;
   }

   idCtrlr = malloc(sizeof(*idCtrlr));
   if (idCtrlr == NULL) {
      Error("Out of memory.");
      goto out;
   }

   rc = Nvme_Identify(handle, -1, idCtrlr);
   if (rc) {
      Error("Failed to get adapter information, 0x%x.", rc);
      goto out_free;
   }

   maxSlot = (idCtrlr->firmUpdt & 0xf) >> 1;
   if (slot < 1 || slot > maxSlot) {
      Error("Invalid slot number.");
      goto out_free;
   }

   if (slot == 1 && (idCtrlr->firmUpdt & 0x1)) {
      Error("Failed to download firmware: slot 1 is read only.");
      goto out_free;
   }
   
   rc = Nvme_FWLoadImage(fwPath, &fwBuf, &fwSize);
   if (rc) {
      Error("Failed to read firmware image file.");
      goto out_free;
   }

   rc = Nvme_FWDownload(handle, slot, fwBuf, fwSize);
   if (rc) {
      Error("Failed to download firmware, 0x%x", rc);
      goto out_free;
   }

   rc = Nvme_FWActivate(handle, slot, NVME_FIRMWARE_ACTIVATE_ACTION_NOACT);
   if (rc != NVME_NEED_COLD_REBOOT && rc != 0) {
      Error("Failed to commit downloaded firmware to slot %d, 0x%x", slot, rc);
   } else {
      esxcli_xml_begin_output();
      xml_list_begin("string");
      if (rc == NVME_NEED_COLD_REBOOT) {
         printf("<string>Commit downloaded firmware to slot %d successfully"
                " but need cold reboot.</string>", slot);
      } else {
         printf("<string>Commit downloaded firmware to slot %d successfully.</string>",
                slot);
      }
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
   struct nvme_adapter_list list;
   struct nvme_handle      *handle;
   struct iden_controller  *idCtrlr;

   while ((ch = getopt(argc, (char *const*)argv, "A:s:")) != -1) {
      switch (ch) {
         case 'A':
            vmhba = optarg;
            break;
         case 's':
            slot = atoi(optarg);
            break;
         default:
            Error("Invalid argument.");
            return;
      }
   }

   if (vmhba == NULL) {
      Error("Invalid argument.");
      return;
   }

   rc = Nvme_GetAdapterList(&list);
   if (rc) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Adapter not found.");
      return;
   }

   idCtrlr = malloc(sizeof(*idCtrlr));
   if (idCtrlr == NULL) {
      Error("Out of memory.");
      goto out;
   }

   rc = Nvme_Identify(handle, -1, idCtrlr);
   if (rc) {
      Error("Failed to get adapter information, 0x%x.", rc);
      goto out_free;
   }

   maxSlot = (idCtrlr->firmUpdt & 0xf) >> 1;
   if (slot < 1 || slot > maxSlot) {
      Error("Invalid slot number.");
      goto out_free;
   }

   rc = Nvme_FWActivate(handle, slot, NVME_FIRMWARE_ACTIVATE_ACTION_ACTIVATE);

   if (rc != NVME_NEED_COLD_REBOOT && rc != 0) {
      Error("Failed to activate firmware in slot %d, 0x%x", slot, rc);
   } else {
      esxcli_xml_begin_output();
      xml_list_begin("string");
      if (rc == NVME_NEED_COLD_REBOOT) {
         printf("<string>Activate firmware in slot %d successfully"
                " but need cold reboot.</string>", slot);
      } else {
         printf("<string>Activate firmware in slot %d successfully.</string>", slot);
      }
      xml_list_end();
      esxcli_xml_end_output();
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
   Bool setDebug = 0;
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
            Error("Invalid argument.");
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
            Error("Invalid argument.");
            return;
      }
   }

   if (vmhba == NULL) {
      Error("Invalid argument.");
      return;
   }

   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Adapter not found.");
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
            Error("Invalid argument.");
            return;
      }
   }

   if (vmhba == NULL || timeout < 0 || timeout > 40) {
      Error("Invalid argument.");
      return;
   }

   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Adapter not found.");
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
            Error("Invalid argument.");
            return;
      }
   }

   if (vmhba == NULL) {
      Error("Invalid argument.");
      return;
   }

   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Adapter not found.");
      return;
   }

   handle = Nvme_Open(&list, vmhba);
   if (handle == NULL) {
      Error("Adapter not found.");
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

struct Command {
   const char           *op;
   CommandHandlerFunc    fn;
};

static struct Command commands[] = {
   {
      "nvme.device.list",
      NvmePlugin_DeviceList,
   },
   {
      "nvme.device.get",
      NvmePlugin_DeviceGet,
   },
   {
      "nvme.device.namespace.list",
      NvmePlugin_DeviceNsList,
   },
   {
      "nvme.device.namespace.get",
      NvmePlugin_DeviceNsGet,
   },
   {
      "nvme.device.namespace.format",
      NvmePlugin_DeviceNsFormat,
   },
   {
      "nvme.device.log.get",
      NvmePlugin_DeviceLogGet,
   },
   {
      "nvme.device.feature.list",
      NvmePlugin_DeviceFeatureList,
   },
   {
      "nvme.device.feature.get",
      NvmePlugin_DeviceFeatureGet,
   },
   {
      "nvme.device.feature.set",
      NvmePlugin_DeviceFeatureSet,
   },
   {
      "nvme.device.firmware.download",
      NvmePlugin_DeviceFirmwareDownload,
   },
   {
      "nvme.device.firmware.activate",
      NvmePlugin_DeviceFirmwareActivate,
   },
   {
      "nvme.driver.loglevel.set",
      NvmePlugin_DriverLoglevelSet,
   },
   {
      "nvme.device.register.get",
      NvmePlugin_DeviceRegisterGet,
   },
   {
      "nvme.device.timeout.set",
      NvmePlugin_DeviceTimeoutSet,
   },
   {
      "nvme.device.timeout.get",
      NvmePlugin_DeviceTimeoutGet,
   },
};

#define NUM_COMMANDS       (sizeof(commands)/sizeof(commands[0]))
#define MAX_COMMAND_LEN    (32)

static CommandHandlerFunc
NvmeLookupFunction(const char *op)
{
   int i;
   for (i = 0; i < NUM_COMMANDS; i++) {
      if (strncmp(op, commands[i].op, MAX_COMMAND_LEN) == 0) {
         return commands[i].fn;
      }
   }
   return NULL;
}


int
main(int argc, const char * argv[]) {

   CommandHandlerFunc fn;
   const char        *op;
   int                rc;

   if (argc < 3) {
      Error("Invalid argument.\n");
      rc = -EINVAL;
      goto out;
   }

   if (strncmp(argv[1], "--op", MAX_COMMAND_LEN) != 0) {
      Error("Invalid argument.\n");
      rc = -EINVAL;
      goto out;
   }

   op = argv[2];

   argc -= 2;
   argv += 2;

   fn = NvmeLookupFunction(op);
   if (fn == NULL) {
      Error("Invalid argument.\n");
      rc = -EINVAL;
      goto out;
   }

   fn(argc, argv);
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
