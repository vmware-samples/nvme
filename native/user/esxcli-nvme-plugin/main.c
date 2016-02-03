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
   PrintString(
      "   PCI Vendor ID:                                          0x%04x\n"
      "   PCI Subsystem Vendor ID:                                0x%04x\n"
      "   Serial Number:                                          %.20s\n"
      "   Model Number:                                           %.40s\n"
      "   Firmware Revision:                                      %.8s\n"
      "   Recommended Arbitration Burst:                          %d\n"
      "   IEEE OUI:                                               0x%02x%02x%02x\n"
      "   Optional Admin Command Support:                         0x%04x\n"
      "      Firmware Activate And Download Support:              0x%x\n"
      "      Format NVM Support:                                  0x%x\n"
      "      Security Send And Receive Support:                   0x%x\n"
      "   Abort Command Limit (0's based):                        %d\n"
      "   Asynchronous Event Request Limit (0's based):           %d\n"
      "   Firmware Updates:                                       0x%02x\n"
      "      Firmware Activate Without Reset Support:             0x%x\n"
      "      Number of Firmware Slot:                             %d\n"
      "      The First Slot Is Read-Only:                         0x%x\n"
      "   Log Page Attributes:                                    0x%02x\n"
      "      Command Effects Log Page Support:                    0x%x\n"
      "      SMART/Health Log Page Per Namespace Support:         0x%x\n"
      "   Error Log Page Entries (0's based):                     %d\n"
      "   Number of Power States Support (0's based):             %d\n"
      "   Admin Vendor Specific Command Configuration:            0x%02x\n"
      "      Same Format for Admin Vendor Specific Cmd:           0x%x\n"
      "   Submission Queue Entry Size:                            0x%02x\n"
      "      Max Submission Queue Entry Size:                     %d Bytes\n"
      "      Min Submission Queue Entry Size:                     %d Bytes\n"
      "   Completion Queue Entry Size:                            0x%02x\n"
      "      Max Completion Queue Entry Size:                     %d Bytes\n"
      "      Min Completion Queue Entry Size:                     %d Bytes\n"
      "   Number of Namespaces:                                   %d\n"
      "   Optional NVM Command Support:                           0x%04x\n"
      "      Reservations Support:                                %d\n"
      "      Save/Select Filed in Set/Get Feature Support:        %d\n"
      "      Write Zeroes Command Support:                        %d\n"
      "      Dataset Management Support:                          %d\n"
      "      Write Uncorrectable Command Support:                 %d\n"
      "      Compare Command Support:                             %d\n"
      "   Fused Operation Support:                                0x%04x\n"
      "      Fused Operation Support:                             0x%x\n"
      "   Format NVM Attributes:                                  0x%02x\n"
      "      Cryptographic Erase Support:                         0x%x\n"
      "      Cryptographic And User Data Erase To All Namespaces: 0x%x\n"
      "      Format To All Namespaces:                            0x%x\n"
      "   Volatile Write Cache:                                   0x%02x\n"
      "      Volatile Write Cache Is Present:                     0x%x\n"
      "   Atomic Write Unit Normal:                               0x%04x\n"
      "   Atomic Write Unit Power Fail:                           0x%04x\n"
      "   NVM Vendor Specific Command Configuration:              0x%02x\n"
      "      Same Format for All NVM Vendor Specific Command:     0x%x\n",
      id->pcieVID, id->pcieSSVID, id->serialNum, id->modelNum,
      id->firmwareRev, id->arbBurstSize, id->ieeeOui[2], id->ieeeOui[1],
      id->ieeeOui[0], id->adminCmdSup, (id->adminCmdSup & 0x4) >> 2,
      (id->adminCmdSup & 0x2) >> 1, id->adminCmdSup & 0x1, id->abortCmdLmt,
      id->asyncReqLmt, id->firmUpdt, (id->firmUpdt & 0x10) >> 4,
      (id->firmUpdt & 0xe) >> 1, id->firmUpdt & 0x1, id->logPgAttrib,
      (id->logPgAttrib & 0x2) >> 1, id->logPgAttrib & 0x1, id->errLogPgEntr,
      id->numPowerSt, id->admVendCmdCfg, id->admVendCmdCfg & 0x1, id->subQSize,
      1 << ((id->subQSize & 0xf0) >> 4), 1 << (id->subQSize & 0xf), id->compQSize,
      1 << ((id->compQSize & 0xf0) >> 4), 1 << (id->compQSize & 0xf), id->numNmspc,
      id->cmdSupt, (id->cmdSupt & 0x20) >> 5, (id->cmdSupt & 0x10) >> 4, (id->cmdSupt & 0x8) >> 3,
      (id->cmdSupt & 0x4) >> 2, (id->cmdSupt & 0x2) >> 1, id->cmdSupt & 0x1,
      id->fuseSupt, id->fuseSupt & 0x1, id->cmdAttrib, (id->cmdAttrib & 0x4) >> 2,
      (id->cmdAttrib & 0x2) >> 1, id->cmdAttrib & 0x1, id->volWrCache,
      id->volWrCache & 0x1, id->atomWrNorm, id->atomWrFail, id->nvmVendCmdCfg,
      id->nvmVendCmdCfg & 0x1);

}

static void
PrintIdentifyNs(struct iden_namespace *idNs)
{
   int lbaIndex;

   PrintString(
      "   Namespace Size:                                %llu logical blocks\n"
      "   Namespace Capacity:                            %llu logical blocks\n"
      "   Namespace Utilization:                         %llu logical blocks\n"
      "   Namespace Features:                            0x%02x\n"
      "      Deallocated/Unwritten Logical Block Error:  0x%x\n"
      "      Namespace Atomic Support:                   0x%x\n"
      "      Thin Provisioning Suppprot:                 0x%x\n"
      "   Number of LBA Formats:                         0x%02x\n"
      "   Formatted LBA Size:                            0x%02x\n"
      "      Extended Metadata:                          0x%x\n"
      "      LBA Format:                                 0x%x\n"
      "   Metadata Capabilities:                         0x%02x\n"
      "      Metadata as Seperate Buffer Capability:     0x%x\n"
      "      Metadata as Extended Buffer Capability:     0x%x\n"
      "   End-to-end Data Protection Capabilities:       0x%02x\n"
      "      PI in Last 8 Bytes of Metadata Capability:  0x%x\n"
      "      PI in First 8 Bytes of Metadata Capability: 0x%x\n"
      "      PI Type 3 Capability:                       0x%x\n"
      "      PI Type 2 Capability:                       0x%x\n"
      "      PI Type 1 Capability:                       0x%x\n"
      "   End-to-end Data Protection Type Settings:      0x%02x\n" ,
      idNs->size, idNs->capacity, idNs->utilization,
      idNs->feat, (idNs->feat & 0x4) >> 2, (idNs->feat & 0x2) >> 1, idNs->feat & 0x1,
      idNs->numLbaFmt, idNs->fmtLbaSize, (idNs->fmtLbaSize & 0x10) >> 4, 
      idNs->fmtLbaSize & 0xf, idNs->metaDataCap, (idNs->metaDataCap & 0x2) >> 1, 
      idNs->metaDataCap & 0x1, idNs->dataProtCap, (idNs->dataProtCap & 0x10) >> 4,
      (idNs->dataProtCap & 0x8) >> 3, (idNs->dataProtCap & 0x4) >> 2,
      (idNs->dataProtCap & 0x2) >> 1, idNs->dataProtCap & 0x1, idNs->dataProtSet);

   if (idNs->dataProtSet & 0x3) {
      PrintString("      PI Type %d\n", idNs->dataProtSet & 0x3);
      if (idNs->dataProtSet & 0x8) {
         PrintString("      Setting PI in Last 8 Bytes of Metadata\n");
      } else {
         PrintString("      Setting PI in First 8 Bytes of Metadata\n");
      }
   } else {
      PrintString("      PI Disabled\n");
   }
 
   PrintString("   LBA Format Support: \n");
   for (lbaIndex = 0; lbaIndex <= idNs->numLbaFmt; lbaIndex ++) {
      PrintString("   %02d | Metadata Size: %5u, LBA Data Size: %5d, Relative Performance: %s\n",
         lbaIndex, idNs->lbaFmtSup[lbaIndex].metaSize,
         1 << idNs->lbaFmtSup[lbaIndex].dataSize,
         nvmNsRelPerf[idNs->lbaFmtSup[lbaIndex].relPerf]);
   }
}

static void
PrintErrLog(struct error_log * errLog)
{
   PrintString(
	 "Error Count: %llu\n"
	 "Submission Queue ID: 0x%x\n"
	 "Command ID: 0x%x\n"
	 "Status Field: 0x%x\n"
	 "Parameter Error Location: 0x%x\n"
	 "   Byte in Command That Contained the Error: %d\n"
	 "   Bit in Command That Contained the Error: %d\n"
	 "LBA: 0x%llx\n"
	 "Namespace: 0x%x\n"
	 "Vendor Specific Infomation Available: 0x%x\n",
	 errLog->errorCount, errLog->sqID,
	 errLog->cmdID, errLog->status,
	 ((vmk_uint16*)errLog)[7], errLog->errorByte,
	 errLog->errorBit, errLog->lba,
	 errLog->nameSpace, errLog->vendorInfo);
}

static void
PrintSmartLog(struct smart_log * smartLog)
{
   PrintString(
	 "Critical Warning: 0x%x\n"
	 "   Availabel Spare Space Below Threshold: %d\n"
	 "   Temperature Above an Over Temperature Threshold or Below an Under Temperature Threshold: %d\n"
	 "   NVM Subsystem Reliability Degradation: %d\n"
	 "   Read Only Mode: %d\n"
	 "   Volatile Memory Backup Device Failure: %d\n"
	 "Composite Temperature: %d K\n" 
	 "Available Spare: %d%%\n"
	 "Available Spare Threshold: %d%%\n"
	 "Percentage Used: %d%%\n"
	 "Data Units Read (reported in 1000 units of 512 bytes): 0x%llx%llx\n"
	 "Data Units Written (reported in 1000 units of 512 bytes): 0x%llx%llx\n"
	 "Host Read Commands: 0x%llx%llx\n"
	 "Host Write Commands: 0x%llx%llx\n"
	 "Controller Busy Time: 0x%llx%llx\n"
	 "Power Cycles: 0x%llx%llx\n"
	 "Power On Hours: 0x%llx%llx\n"
	 "Unsafe Shutdowns: 0x%llx%llx\n"
	 "Media Errors: 0x%llx%llx\n"
	 "Number of Error Info Log Entries: 0x%llx%llx\n",
	 smartLog->criticalError, smartLog->criticalError & 0x1,
	 (smartLog->criticalError & 0x2) >> 1, (smartLog->criticalError & 0x4) >> 2,
	 (smartLog->criticalError & 0x8) >> 3, (smartLog->criticalError & 0x10) >> 4,
	 *(vmk_uint16 *)smartLog->temperature, smartLog->availableSpace,
	 smartLog->availableSpaceThreshold, smartLog->precentageUsed,
	 *(vmk_uint64 *)&smartLog->dataUnitsRead[0], *(vmk_uint64 *)&smartLog->dataUnitsRead[4],
	 *(vmk_uint64 *)&smartLog->dataUnitsWritten[0], *(vmk_uint64 *)&smartLog->dataUnitsWritten[4],
	 *(vmk_uint64 *)&smartLog->hostReadCommands[0], *(vmk_uint64 *)&smartLog->hostReadCommands[4],
	 *(vmk_uint64 *)&smartLog->hostWriteCommands[0], *(vmk_uint64 *)&smartLog->hostWriteCommands[4],
	 *(vmk_uint64 *)&smartLog->controllerBusyTime[0], *(vmk_uint64 *)&smartLog->controllerBusyTime[4],
	 *(vmk_uint64 *)&smartLog->powerCycles[0], *(vmk_uint64 *)&smartLog->powerCycles[4],
	 *(vmk_uint64 *)&smartLog->powerOnHours[0], *(vmk_uint64 *)&smartLog->powerOnHours[4],
	 *(vmk_uint64 *)&smartLog->unsafeShutdowns[0], *(vmk_uint64 *)&smartLog->unsafeShutdowns[4],
	 *(vmk_uint64 *)&smartLog->mediaErrors[0], *(vmk_uint64 *)&smartLog->mediaErrors[4],
	 *(vmk_uint64 *)&smartLog->numberOfErrorInfoLogs[0], *(vmk_uint64 *)&smartLog->numberOfErrorInfoLogs[4]);
}

static void
PrintFwSlotLog(struct firmware_slot_log * fwSlotLog)
{
   PrintString(
	 "Active Firmware Info: 0x%x\n"
	 "   Firmware Slot to Be Activated at Next Controller Reset: %d\n"
	 "   Firmware Slot Being Activated: %d\n"
	 "Firmware Revision for Slot 1: %.8s\n"
	 "Firmware Revision for Slot 2: %.8s\n"
	 "Firmware Revision for Slot 3: %.8s\n"
	 "Firmware Revision for Slot 4: %.8s\n"
	 "Firmware Revision for Slot 5: %.8s\n"
	 "Firmware Revision for Slot 6: %.8s\n"
	 "Firmware Revision for Slot 7: %.8s\n",
	 fwSlotLog->activeFirmwareInfo, (fwSlotLog->activeFirmwareInfo & 0x70) >> 4,
	 fwSlotLog->activeFirmwareInfo & 0x7, fwSlotLog->FirmwareRevisionSlot1,
	 fwSlotLog->FirmwareRevisionSlot2, fwSlotLog->FirmwareRevisionSlot3,
	 fwSlotLog->FirmwareRevisionSlot4, fwSlotLog->FirmwareRevisionSlot5,
	 fwSlotLog->FirmwareRevisionSlot6, fwSlotLog->FirmwareRevisionSlot7);
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

void
NvmePlugin_DeviceList(int argc, const char *argv[])
{
   struct nvme_adapter_list list;
   int    rc;
   int    i;

   PrintString("HBA Name  Status      Signature                     \n");
   PrintString("--------  ----------  ------------------------------\n");

   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      Error("Failed to get adapter list: 0x%x.", rc);
      return;
   }

   for (i = 0; i < list.count; i++) {
      PrintString("%-8s  %-10s  %-30s\n",
         list.adapters[i].name,
         list.adapters[i].status == ONLINE ? "Online" : "Offline",
         list.adapters[i].signature);
   }
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
   char deviceName[MAX_DEV_NAME_LEN];
   char runtimeName[MAX_DEV_NAME_LEN];
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

   PrintString("Namespace ID  Status      Device Name                   \n");
   PrintString("------------  ----------  ------------------------------\n");

   for (i = 1; i <= numNs; i++) {
      snprintf(runtimeName, MAX_DEV_NAME_LEN, "%s:C0:T0:L%d", vmhba, i-1);
      status = GetDeviceName(runtimeName, deviceName, MAX_DEV_NAME_LEN);
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
         snprintf(deviceName, MAX_DEV_NAME_LEN, "N/A");
      }
   
      if (uio.status == 0) {
         PrintString("%-12d  %-10s  %-30s\n", i, "Online", deviceName);
      } else {
         PrintString("%-12d  %-10s  %-30s\n", i, "Offline", "N/A");
      }
   }

out_free:
   free(idCtrlr);
out:
   Nvme_Close(handle);
}

void
NvmePlugin_DeviceNsGet(int argc, const char *argv[])
{
   int                      ch, i, numNs, nsId = 0;
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

   /* If nsId is 0, get all namespaces info on the controller. */
   if (nsId > numNs || nsId < 0) {
      Error("Invalid namespace Id.");
      goto out_free_all;
   } else if (nsId == 0) {
      for (i = 1; i <= numNs; i++) {
         rc = Nvme_Identify(handle, i, idNs);
         if (rc) {
            PrintString("Failed to get identify data for namespace %d, %s.", i, strerror(rc));
         } else {
            PrintString("Identify Namespace: %d\n", i);
            PrintString("--------------------------\n");
            PrintIdentifyNs(idNs);
         }
      }
   } else {
      rc = Nvme_Identify(handle, nsId, idNs);
      if (rc) {
         PrintString("Failed to get identify data for namespace %d, %s.", nsId, strerror(rc));
      } else {
         PrintString("Identify Namespace: %d\n", nsId);
         PrintString("--------------------------\n");
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

   PrintString("%s\n", vmhba);
   PrintString("--------\n");
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
      PrintString("Format successfully.\n");
      memset(&uio, 0, sizeof(uio));
      uio.namespaceID = nsid;
      rc = Nvme_Ioctl(handle, NVME_IOCTL_UPDATE_NS, &uio);
      if (rc || uio.status) {
         Error("Failed to update namespace attributes after format. Offline namespace.");
         goto out_free;
      }
   }

   memset(&uio, 0, sizeof(uio));
   uio.namespaceID = nsid;
   rc = Nvme_Ioctl(handle, NVME_IOCTL_SET_NS_ONLINE, &uio);
   if (rc || uio.status) {
      Error("Failed to online namespace.");
   }
   goto out_free;

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
         uio.addr = (vmk_uint32)&log.errLog;
         break;
      case GLP_ID_SMART_HEALTH:
         uio.cmd.header.namespaceID = nsid;
         uio.cmd.cmd.getLogPage.numDW = GLP_LEN_SMART_HEALTH / 4 - 1;
         uio.cmd.cmd.getLogPage.numDW = GLP_LEN_SMART_HEALTH / 4 - 1;
         uio.length = GLP_LEN_SMART_HEALTH;
         uio.addr = (vmk_uint32)&log.smartLog;
         break;
      case GLP_ID_FIRMWARE_SLOT_INFO:
         uio.cmd.cmd.getLogPage.numDW = GLP_LEN_FIRMWARE_SLOT_INFO / 4 - 1;
         uio.length = GLP_LEN_FIRMWARE_SLOT_INFO;
         uio.addr = (vmk_uint32)&log.fwSlotLog;
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
         for (i = 0; i < elpe; i++) {
            PrintString("---------------log page %d---------------\n", i);
            PrintErrLog(&log.errLog[i]);
         }
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

static int 
GetFeature(struct nvme_handle *handle, struct usr_io *uiop, int fid)
{
   int value, rc, vectNum, i;
   struct usr_io uioVect;
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
         return rc;
      }

      vectNum = uioVect.length;
      Debug("vectNum: %d\n", vectNum);
      PrintString("INTERRUPT VECTOR CONFIGURATION:\n");
      for (i = 0; i < vectNum; i++) {
         uiop->cmd.cmd.getFeatures.numSubQReq = i;
         rc = Nvme_AdminPassthru(handle, uiop);
         if (rc) {
            Error("Failed to get config of interrupt vector %d\n.", i);
            continue;
         }
         value = uiop->comp.param.cmdSpecific;
         PrintString("   Interrupt Vector: %d", value & 0xffff);
         PrintString("   Coalescing Disable: %d\n", (value & 0x10000) >> 16);
      }
      return rc;
   }

   rc = Nvme_AdminPassthru(handle, uiop);

   if (rc) {
      return rc;
   }

   value = uiop->comp.param.cmdSpecific;
   Debug("value = %x\n", value);
   switch (fid)
   {
   case FTR_ID_ARBITRATION:
      PrintString("ARBITRATION:\n   HPW: %d\tMPW: %d\tLPW: %d\tAB: %d\n", (value & 0xff000000)>> 24, (value & 0xff0000) >> 16, (value & 0xff00) >> 8, value & 0x7);
      break;

   case FTR_ID_PWR_MANAGEMENT:
      PrintString("POWER MANAGEMENT:\n   Power State: %d\n", value & 0x1f);
      break;

   case FTR_ID_LBA_RANGE_TYPE:
      PrintString("LBA RANGE TYPE:\n   Unsupported Feature\n");
      break;

   case FTR_ID_TEMP_THRESHOLD:
      PrintString("TEMPERATURE THRESHOLD:\n   Temperature Threshold: %d K (%d C)\n", value & 0xffff, (value & 0xffff) - 273);
      break;

   case FTR_ID_ERR_RECOVERY:
      PrintString("ERROR RECOVERY:\n   Time Limited Error Recovery: %d\n", value & 0xffff);
      break;

   case FTR_ID_WRITE_CACHE:
      PrintString("VOLATILE WRITE CACHE:\n   Volatile Write Cache Enable: %d\n", value & 0x1);
      break;

   case FTR_ID_NUM_QUEUE:
      PrintString("NUMBER OF QUEUES:\n   Number of Submission Queues Allocated: %d\n   Number of Completion Queues Allocated: %d\n", value & 0xffff, (value & 0xffff0000) >> 16);
      break;

   case FTR_ID_INT_COALESCING:
      PrintString("INTERRUPT COALESCING:\n   Aggregation Time: %d\n   Aggregation Threshold: %d\n", (value & 0xff00) >> 8, value & 0xff);
      break;

   case FTR_ID_WRITE_ATOMICITY:
      PrintString("WRITE ATOMICITY:\n   Disable Normal: %d\n", value & 0x1);
      break;

   case FTR_ID_ASYN_EVENT_CONFIG:
      PrintString("ASYN EVENT CONFIGURATION:\n");
      PrintString("   Available Spare Space: %d\n", value & 0x1);
      PrintString("   Temperature:           %d\n", (value & 0x2) >> 1);
      PrintString("   Media Error:           %d\n", (value & 0x4) >> 2);
      PrintString("   Read Only Mode:        %d\n", (value & 0x8) >> 3);
      PrintString("   Backup Device Fail:    %d\n", (value & 0x10) >> 4);
      break;

   case FTR_ID_SW_PROGRESS_MARKER:
      PrintString("SOFTWARE PROGRESS MARKER:\n   Unsupported Feature\n");
      break;

   default:
      PrintString("Failed to get feature info, %s.", strerror(rc));
      break;
   }

   return rc;
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

   rc = GetFeature(handle, &uio, fid);
   if (rc) {
      PrintString("Failed to get feature info, %s.", strerror(rc));
      Nvme_Close(handle);
      return;
   }

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
      uio.cmd.cmd.setFeatures.numSubQReq = value | value2 << 8;
      uio.cmd.cmd.setFeatures.numCplQReq = value3 | value4 << 8;
      PrintString("Currently driver only supports Round Robin Arbitration, only Arbitration Burst will be effective");
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
         Error("Unable to set this feature: controller doesn't have a write cache!");
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
 
      if (value == 0 && value2 == 1) {
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
      rc = GetFeature(handle, &uio, i);
      if (rc) {
         PrintString("Failed to get feature info, %s.", strerror(rc));
         Nvme_Close(handle);
         return;
      }
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
   else {
      PrintString("Download firmware to NVMe controller successfully.\n");
   }

   rc = Nvme_FWActivate(handle, slot, NVME_FIRMWARE_ACTIVATE_ACTION_NOACT);
   if (!rc) {
      PrintString("Commit downloaded firmware to slot %d successfully.\n", slot);
   }
   else if (rc == NVME_NEED_COLD_REBOOT) {
      PrintString("Commit downloaded firmware to slot %d successfully but need cold reboot.\n", slot);
   }
   else {
      Error("Failed to commit downloaded firmware to slot %d, 0x%x", slot, rc);
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
   if (!rc) {
      PrintString("Activate firmware in slot %d successfully.\n", slot);
   }
   else if (rc == NVME_NEED_COLD_REBOOT) {
      PrintString("Activate firmware in slot %d successfully but need code reboot.\n", slot);
   }
   else {
      Error("Failed to activate firmware in slot %d, 0x%x.", slot, rc);
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
         PrintString("Debug level is invalid when setting log level to %d. Set debug level to 0.\n", logLevel);
      }
      else {
         rc = sscanf(debugString, "%x", &debugLevel);
         if (rc != 1) {
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
      PrintString("Successfully set log level to %d and debug level to 0x%x.", logLevel, debugLevel);
   }
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

   esxcli_xml_begin_output();
   xml_list_begin("string");
   printf("<string>");

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
   printf("</string>\n");
   xml_list_end();
   esxcli_xml_end_output();

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
