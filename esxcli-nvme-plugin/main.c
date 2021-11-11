/******************************************************************************
 * Copyright (c) 2014-2021 VMware, Inc. All rights reserved.
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
#define MIN(a,b) ((a) < (b) ? (a) : (b))

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
PrintIdentifyCtrlr(vmk_NvmeIdentifyController *id)
{
   char* hexdumpbuff;
   char* readablebuff;
   char err_str[64] = "(has unprintable characters)";
   int readbufflen;
   int hexbufflen;

   esxcli_xml_begin_output();
   xml_struct_begin("DeviceInfo");
   PINTS("PCIVID", id->vid);
   PINTS("PCISSVID", id->ssvid);
   xml_field_begin("Serial Number");
   printf("<string>%.20s</string>", id->sn);
   xml_field_end();
   xml_field_begin("Model Number");
   printf("<string>%.40s</string>", id->mn);
   xml_field_end();
   xml_field_begin("Firmware Revision");
   printf("<string>%.8s</string>", id->fr);
   xml_field_end();
   PINT("Recommended Arbitration Burst", id->rab);
   xml_field_begin("IEEE OUI Identifier");
   printf("<string>%02x%02x%02x</string>", id->ieee[2], id->ieee[1],
          id->ieee[0]);
   xml_field_end();
   PBOOL("Controller Associated with an SR-IOV Virtual Function",
         id->cmic & VMK_NVME_CTLR_IDENT_CMIC_SRIOV);
   PBOOL("Controller Associated with a PCI Function",
         !(id->cmic & VMK_NVME_CTLR_IDENT_CMIC_SRIOV));
   PBOOL("NVM Subsystem May Contain Two or More Controllers",
         id->cmic & VMK_NVME_CTLR_IDENT_CMIC_MH);
   PBOOL("NVM Subsystem Contains Only One Controller",
         !(id->cmic & VMK_NVME_CTLR_IDENT_CMIC_MH));
   PBOOL("NVM Subsystem May Contain Two or More PCIe Ports",
         id->cmic & VMK_NVME_CTLR_IDENT_CMIC_MP);
   PBOOL("NVM Subsystem Contains Only One PCIe Port",
         !(id->cmic & VMK_NVME_CTLR_IDENT_CMIC_MP));
   PINT("Max Data Transfer Size", id->mdts);
   PINT("Controller ID", id->cntlid);
   xml_field_begin("Version");
   printf("<string>%d.%d</string>", id->ver.mjr, id->ver.mnr);
   xml_field_end();
   PINT("RTD3 Resume Latency", id->rtd3r);
   PINT("RTD3 Entry Latency", id->rtd3e);
   PBOOL("Optional Firmware Activation Event Support",
         id->oaes & VMK_NVME_CTLR_IDENT_OAES_FW_ACTIVATE);
   PBOOL("Optional Namespace Attribute Changed Event Support",
         id->oaes & VMK_NVME_CTLR_IDENT_OAES_NS_ATTRIBUTE);
   PBOOL("Host Identifier Support",
         id->ctratt & VMK_NVME_CTLR_IDENT_CTRATT_HOST_ID);
   PBOOL("Namespace Management and Attachment Support",
         id->oacs & VMK_NVME_CTLR_IDENT_OACS_NS_MGMT);
   PBOOL("Firmware Activate and Download Support",
         id->oacs & VMK_NVME_CTLR_IDENT_OACS_FIRMWARE);
   PBOOL("Format NVM Support",
         id->oacs & VMK_NVME_CTLR_IDENT_OACS_FORMAT);
   PBOOL("Security Send and Receive Support",
         id->oacs & VMK_NVME_CTLR_IDENT_OACS_SECURITY);
   PINT("Abort Command Limit", id->acl);
   PINT("Async Event Request Limit", id->aerl);
   PBOOL("Firmware Activate Without Reset Support",
         id->frmw & VMK_NVME_CTLR_IDENT_FRMW_ACTIVATE_NO_RESET);
   PINT("Firmware Slot Number", (id->frmw & 0xe) >> 1);
   PBOOL("The First Slot Is Read-only",
         id->frmw & VMK_NVME_CTLR_IDENT_FRMW_SLOT_1_RO);
   PBOOL("Telemetry Log Page Support",
         id->lpa & VMK_NVME_CTLR_IDENT_LPA_TELEMETRY);
   PBOOL("Command Effects Log Page Support",
         id->lpa & VMK_NVME_CTLR_IDENT_LPA_CMD_EFFECTS);
   PBOOL("SMART/Health Information Log Page per Namespace Support",
         id->lpa & VMK_NVME_CTLR_IDENT_LPA_SMART_PER_NS);
   PINT("Error Log Page Entries", id->elpe);
   PINT("Number of Power States Support", id->npss);
   PBOOL("Format of Admin Vendor Specific Commands Is Same",
         id->avscc & VMK_NVME_CTLR_IDENT_AVSCC_STD_FMT);
   PBOOL("Format of Admin Vendor Specific Commands Is Vendor Specific",
         (id->avscc & VMK_NVME_CTLR_IDENT_AVSCC_STD_FMT) == 0);
   PBOOL("Autonomous Power State Transitions Support", id->apsta & 0x1);
   PINT("Warning Composite Temperature Threshold", id->wctemp);
   PINT("Critical Composite Temperature Threshold", id->cctemp);
   PINT("Max Time for Firmware Activation", id->mtfa);
   PINT("Host Memory Buffer Preferred Size", id->hmpre);
   PINT("Host Memory Buffer Min Size", id->hmmin);
   P128BIT("Total NVM Capacity", id->tnvmcap);
   P128BIT("Unallocated NVM Capacity", id->unvmcap);
   PINT("Access Size", id->rpmbs.as);
   PINT("Total Size", id->rpmbs.ts);
   PINT("Authentication Method", id->rpmbs.am);
   PINT("Number of RPMB Units", id->rpmbs.nru);
   PINT("Keep Alive Support", id->kas);
   PINT("Max Submission Queue Entry Size", 1 << ((id->sqes & 0xf0) >> 4));
   PINT("Required Submission Queue Entry Size", 1 << (id->sqes & 0xf));
   PINT("Max Completion Queue Entry Size", 1 << ((id->cqes & 0xf0) >> 4));
   PINT("Required Completion Queue Entry Size", 1 << (id->cqes & 0xf));
   PINT("Max Outstanding Commands", id->maxcmd);
   PINT("Number of Namespaces", id->nn);
   PBOOL("Reservation Support", id->oncs & VMK_NVME_CTLR_IDENT_ONCS_RSV);
   PBOOL("Save/Select Field in Set/Get Feature Support",
         id->oncs & VMK_NVME_CTLR_IDENT_ONCS_SV);
   PBOOL("Write Zeroes Command Support",
         id->oncs & VMK_NVME_CTLR_IDENT_ONCS_WZ);
   PBOOL("Dataset Management Command Support",
         id->oncs & VMK_NVME_CTLR_IDENT_ONCS_DM);
   PBOOL("Write Uncorrectable Command Support",
         id->oncs & VMK_NVME_CTLR_IDENT_ONCS_WU);
   PBOOL("Compare Command Support", id->oncs & VMK_NVME_CTLR_IDENT_ONCS_CMP);
   PBOOL("Fused Operation Support", id->fuses & VMK_NVME_CTLR_IDENT_FUSES_CW);
   PBOOL("Cryptographic Erase as Part of Secure Erase Support",
         id->fna & VMK_NVME_CTLR_IDENT_FNA_CYPER);
   PBOOL("Cryptographic Erase and User Data Erase to All Namespaces",
         id->fna & VMK_NVME_CTLR_IDENT_FNA_SECER_ALLNS);
   PBOOL("Cryptographic Erase and User Data Erase to One Particular Namespace",
         (id->fna & VMK_NVME_CTLR_IDENT_FNA_SECER_ALLNS) == 0);
   PBOOL("Format Operation to All Namespaces",
         id->fna & VMK_NVME_CTLR_IDENT_FNA_FMT_ALLNS);
   PBOOL("Format Opertaion to One Particular Namespace",
         (id->fna & VMK_NVME_CTLR_IDENT_FNA_FMT_ALLNS) == 0);
   PBOOL("Volatile Write Cache Is Present", id->vwc & 0x1);
   PINT("Atomic Write Unit Normal", id->awun);
   PINT("Atomic Write Unit Power Fail", id->awupf);
   PBOOL("Format of All NVM Vendor Specific Commands Is Same",
         id->nvscc & VMK_NVME_CTLR_IDENT_NVSCC_STD_FMT);
   PBOOL("Format of All NVM Vendor Specific Commands Is Vendor Specific",
         (id->nvscc & VMK_NVME_CTLR_IDENT_NVSCC_STD_FMT) == 0);
   PINT("Atomic Compare and Write Unit", id->acwu);
   PBOOL("SGL Address Specify Offset Support",
         id->sgls & VMK_NVME_CTLR_IDENT_SGLS_OFFSET_IN_ADDR);
   PBOOL("MPTR Contain SGL Descriptor Support",
         id->sgls & VMK_NVME_CTLR_IDENT_SGLS_MPTR_ONE_SGL);
   PBOOL("SGL Length Able to Larger than Data Amount",
         id->sgls & VMK_NVME_CTLR_IDENT_SGLS_LARGER_SGL);
   PBOOL("SGL Length Shall Be Equal to Data Amount",
         (id->sgls & VMK_NVME_CTLR_IDENT_SGLS_LARGER_SGL) == 0);
   PBOOL("Byte Aligned Contiguous Physical Buffer of Metadata Support",
         id->sgls & VMK_NVME_CTLR_IDENT_SGLS_MPTR_BYTE_ALIGN);
   PBOOL("SGL Bit Bucket Descriptor Support",
         id->sgls & VMK_NVME_CTLR_IDENT_SGLS_BIT_BUCKET);
   PBOOL("SGL Keyed SGL Data Block Descriptor Support",
         id->sgls & VMK_NVME_CTLR_IDENT_SGLS_KEYED_SGL);
   PBOOL("SGL for NVM Command Set Support", id->sgls & 0x1);

   readbufflen = sizeof(id->subnqn) + 64;
   readablebuff = malloc(readbufflen);
   if (readablebuff != NULL) {
      memcpy(readablebuff, id->subnqn, sizeof(id->subnqn));
      if (refineASCIIString(readablebuff, sizeof(id->subnqn))) {
         memcpy(readablebuff + MIN(strlen(readablebuff), sizeof(id->subnqn)),
                err_str, strlen(err_str) + 1);
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
PrintIdentifyNs(vmk_NvmeIdentifyNamespace *idNs)
{
   int lbaIndex;
   esxcli_xml_begin_output();
   xml_struct_begin("NamespaceInfo");
   PULL("Namespace Size", idNs->nsze);
   PULL("Namespace Capacity", idNs->ncap);
   PULL("Namespace Utilization", idNs->nuse);
   PBOOL("Thin Provisioning Support",
         idNs->nsfeat & VMK_NVME_NS_FEATURE_THIN_PROVISION);
   PBOOL("Namespace Atomic Support",
         idNs->nsfeat & VMK_NVME_NS_ATOMICITY);
   PBOOL("Deallocated or Unwritten Logical Block Error Support",
         idNs->nsfeat & VMK_NVME_NS_DEALLOCATED_ERROR);
   PINT("Number of LBA Formats", idNs->nlbaf);
   PINT("LBA Format", idNs->flbas & 0xf);
   PBOOL("Extended Metadata", (idNs->flbas & 0x10) >> 4);
   PBOOL("Metadata as Seperate Buffer Support", (idNs->mc & 0x2) >> 1);
   PBOOL("Metadata as Extended Buffer Support", idNs->mc & 0x1);
   PBOOL("PI Type 1 Support", idNs->dpc & VMK_NVME_DPC_PI_TYPE_1);
   PBOOL("PI Type 2 Support", idNs->dpc & VMK_NVME_DPC_PI_TYPE_2);
   PBOOL("PI Type 3 Support", idNs->dpc & VMK_NVME_DPC_PI_TYPE_3);
   PBOOL("PI in First Eight Bytes of Metadata Support",
         idNs->dpc & VMK_NVME_DPC_PI_FIRST_EIGHT);
   PBOOL("PI in Last Eight Bytes of Metadata Support",
         idNs->dpc & VMK_NVME_DPC_PI_LAST_EIGHT);
   PINT("PI Enabled Type", idNs->dps & 0x7);
   if (idNs->dps & 0x7) {
      PSTR("MetaData Location",
           idNs->dps & 0x8 ? "First Eight Bytes" : "Last Eight Bytes");
   } else {
      PSTR("MetaData Location", "PI Disabled");
   }
   PBOOL("Namespace Shared by Multiple Controllers",
         idNs->nmic & VMK_NVME_NS_IDENT_NMIC_MC);
   PBOOL("Persist Through Power Loss Support",
         idNs->rescap & VMK_NVME_RESCAP_PERSIST_POWER_LOSS);
   PBOOL("Write Exclusive Reservation Type Support",
         idNs->rescap & VMK_NVME_RESCAP_EX_WRITE_RESERVE);
   PBOOL("Exclusive Access Reservation Type Support",
         idNs->rescap & VMK_NVME_RESCAP_EX_ACCESS_RESERVE);
   PBOOL("Write Exclusive Registrants Only Reservation Type Support",
         idNs->rescap & VMK_NVME_RESCAP_EX_WRITE_RESERVE_REG);
   PBOOL("Exclusive Access Registrants Only Reservation Type Support",
         idNs->rescap & VMK_NVME_RESCAP_EX_ACCESS_RESERVE_REG);
   PBOOL("Write Exclusive All Registrants Reservation Type Support",
         idNs->rescap & VMK_NVME_RESCAP_EX_WRITE_RESERVE_ALL);
   PBOOL("Exclusive Access All Registrants Reservation Type Support",
         idNs->rescap & VMK_NVME_RESCAP_EX_ACCESS_RESERVE_ALL);
   PBOOL("Format Progress Indicator Support", idNs->fpi & 0x80);
   PINT("Percentage Remains to Be Formatted", idNs->fpi & 0x7f);
   PINT("Namespace Atomic Write Unit Normal", idNs->nawun);
   PINT("Namespace Atomic Write Unit Power Fail", idNs->nawupf);
   PINT("Namespace Atomic Compare and Write Unit", idNs->nacwu);
   PINT("Namespace Atomic Boundary Size Normal", idNs->nabsn);
   PINT("Namespace Atomic Boundary Offset", idNs->nabo);
   PINT("Namespace Atomic Boundary Size Power Fail", idNs->nabspf);
   P128BIT("NVM Capacity", idNs->nvmcap);
   PID("Namespace Globally Unique Identifier", (vmk_uint8 *)&idNs->nguid, 16);
   PID("IEEE Extended Unique Identifier", (vmk_uint8 *)&idNs->eui64, 8);
   xml_field_begin("LBA Format Support");
   xml_list_begin("structure");
      for (lbaIndex = 0; lbaIndex <= idNs->nlbaf; lbaIndex ++) {
         xml_struct_begin("LBAFormatSupport");
         PINT("Format ID", lbaIndex);
         PINT("Metadata Size", idNs->lbaf[lbaIndex].ms);
         PINT("LBA Data Size", 1 << idNs->lbaf[lbaIndex].lbads);
         PSTR("Relative Performance", nvmNsRelPerf[idNs->lbaf[lbaIndex].rp]);
         xml_struct_end();
   }
   xml_list_end();
   xml_field_end();
   xml_struct_end();
   esxcli_xml_end_output();
}

static void
PrintErrLog(vmk_NvmeErrorInfoLogEntry * errLog)
{
   xml_struct_begin("ErrorInfo");
   PULL("Error Count", errLog->ec);
   PINT("Submission Queue ID", errLog->sqid);
   PINT("Command ID", errLog->cid);
   PINT("Status Field", errLog->sf);
   PINT("Byte in Command That Contained the Error", errLog->pel.byte);
   PINT("Bit in Command That Contained the Error", errLog->pel.bit);
   PULL("LBA", errLog->lba);
   PINT("Namespace", errLog->ns);
   PINT("Vendor Specific Information Available", errLog->vsia);
   xml_struct_end();
}

static void
PrintSmartLog(vmk_NvmeSmartInfoEntry *smartLog)
{
   esxcli_xml_begin_output();
   xml_struct_begin("SMARTInfo");
   PBOOL("Available Spare Space Below Threshold", smartLog->cw.ss);
   PBOOL("Temperature Warning", smartLog->cw.tmp);
   PBOOL("NVM Subsystem Reliability Degradation", smartLog->cw.subsys);
   PBOOL("Read Only Mode", smartLog->cw.ro);
   PBOOL("Volatile Memory Backup Device Failure", smartLog->cw.backup);
   PINT("Composite Temperature", smartLog->ct);
   PINT("Available Spare", smartLog->as);
   PINT("Available Spare Threshold", smartLog->ast);
   PINT("Percentage Used", smartLog->pu);
   P128BIT("Data Units Read", smartLog->dur);
   P128BIT("Data Units Written", smartLog->duw);
   P128BIT("Host Read Commands", smartLog->hrc);
   P128BIT("Host Write Commands", smartLog->hwc);
   P128BIT("Controller Busy Time", smartLog->cbt);
   P128BIT("Power Cycles", smartLog->pc);
   P128BIT("Power On Hours", smartLog->poh);
   P128BIT("Unsafe Shutdowns", smartLog->us);
   P128BIT("Media Errors", smartLog->me);
   P128BIT("Number of Error Info Log Entries", smartLog->neile);
   PINT("Warning Composite Temperature Time", smartLog->wctt);
   PINT("Critical Composite Temperature Time", smartLog->cctt);
   PINT("Temperature Sensor 1", smartLog->ts1);
   PINT("Temperature Sensor 2", smartLog->ts2);
   PINT("Temperature Sensor 3", smartLog->ts3);
   PINT("Temperature Sensor 4", smartLog->ts4);
   PINT("Temperature Sensor 5", smartLog->ts5);
   PINT("Temperature Sensor 6", smartLog->ts6);
   PINT("Temperature Sensor 7", smartLog->ts7);
   PINT("Temperature Sensor 8", smartLog->ts8);
   xml_struct_end();
   esxcli_xml_end_output();
}

static void
PrintFwSlotLog(vmk_NvmeFirmwareSlotInfo *fwSlotLog)
{
   esxcli_xml_begin_output();
   xml_struct_begin("FirmwareSlotInfo");
   PINT("Firmware Slot to Be Activated at Next Controller Reset",
        (fwSlotLog->afi & 0x70) >> 4);
   PINT("Firmware Slot Being Activated", fwSlotLog->afi & 0x7);
   P8BYTE("Firmware Revision for Slot 1", (char *)(&fwSlotLog->frs[0]));
   P8BYTE("Firmware Revision for Slot 2", (char *)(&fwSlotLog->frs[1]));
   P8BYTE("Firmware Revision for Slot 3", (char *)(&fwSlotLog->frs[2]));
   P8BYTE("Firmware Revision for Slot 4", (char *)(&fwSlotLog->frs[3]));
   P8BYTE("Firmware Revision for Slot 5", (char *)(&fwSlotLog->frs[4]));
   P8BYTE("Firmware Revision for Slot 6", (char *)(&fwSlotLog->frs[5]));
   P8BYTE("Firmware Revision for Slot 7", (char *)(&fwSlotLog->frs[6]));
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
   vmk_NvmeIdentifyController   *idCtrlr;
   int                          rc = 0;

   idCtrlr = malloc(sizeof(*idCtrlr));
   if (idCtrlr == NULL) {
      return -1;
   }

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, idCtrlr);
   if (rc != 0) {
      free(idCtrlr);
      return -1;
   }

   rc = idCtrlr->cntlid;
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
      PSTR("Status", list.adapters[i].status == ADAPTER_ONLINE ? "Online" : "Offline");
      PSTR("Signature", list.adapters[i].signature);
      xml_struct_end();
   }
   xml_list_end();
   esxcli_xml_end_output();
}

void
NvmePlugin_DeviceNsCreate(int argc, const char *argv[])
{
   int                         ch;
   int                         rc;
   int                         nsId;
   struct nvme_adapter_list    list;
   const char                  *vmhba = NULL;
   struct nvme_handle          *handle;
   vmk_NvmeIdentifyNamespace   *idNs;
   vmk_uint64                  size        = 0;
   vmk_uint64                  capacity    = 0;
   vmk_uint8                   fmtLbaSize  = -1;
   vmk_uint8                   dataProtSet = -1;
   vmk_uint8                   nmic        = -1;
   int                         cmdStatus   = 0;

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

   idNs->nsze = size;
   idNs->ncap = capacity;
   idNs->flbas = fmtLbaSize;
   idNs->dps = dataProtSet;
   idNs->nmic = nmic & VMK_NVME_NS_IDENT_NMIC_MC;

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
   struct nvme_ctrlr_list  *ctrlrList;
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

   rc = Nvme_NsAttach(handle, VMK_NVME_NS_CTLR_ATTACH,
                      nsId, ctrlrList, &cmdStatus);
   if (rc != 0) {
      switch (cmdStatus) {
         case 0x0:
            Error("Failed to execute attach request, 0x%x.", rc);
            break;
         case 0x118:
            Error("Controller %d is already attached to namespace %d.",
                  ctrlrId, nsId);
            break;
         case 0x119:
            Error("Namespace %d is private.", nsId);
            break;
         case 0x11c:
            Error("The controller list provided is invalid.");
            break;
         default:
            Error("Failed to attach namespace %d to controller %d, 0x%x",
                  nsId, ctrlrId, cmdStatus);
            break;
      }
      goto out_free;
   }

   rc = Nvme_NsListUpdate(handle, VMK_NVME_NS_CTLR_ATTACH, nsId);
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
   const char               *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle       *handle;
   int                      status;
   struct nvme_ctrlr_list   *ctrlrList;
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

   rc = Nvme_NsAttach(handle, VMK_NVME_NS_CTLR_DETACH,
                      nsId, ctrlrList, &cmdStatus);
   if (rc != 0) {
      switch (cmdStatus) {
         case 0x0:
            Error("Failed to execute detach request, 0x%x.", rc);
            break;
         case 0x119:
            Error("Namespace %d is private.", nsId);
            break;
         case 0x11a:
            Error("Controller %d is not attached to the namespace %d",
                  ctrlrId, nsId);
            break;
         case 0x11c:
            Error("The controller list provided is invalid.");
            break;
         default:
            Error("Failed to detach namespace %d from controller %d, 0x%x.",
                  nsId, ctrlrId, cmdStatus);
            break;
      }
      goto out_free;
   }

   rc = Nvme_NsListUpdate(handle, VMK_NVME_NS_CTLR_DETACH, nsId);
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
   const char               *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle       *handle;
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
   const char               *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle       *handle;
   struct nvme_ctrlr_list   *ctrlrList;
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
      rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER_IDS_ATTACHED,
                         0, nsId, ctrlrList);
      if (rc != 0) {
         Error("Failed to get attached controller list, 0x%x.", rc);
         goto out_free;
      }
   } else {
      rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER_IDS,
                         0, 0, ctrlrList);
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
   const char               *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_ns_list      *nsAllocatedList = NULL;
   struct nvme_ns_list      *nsActiveList = NULL;
   struct nvme_handle       *handle;
   vmk_NvmeIdentifyController  *idCtrlr = NULL;
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

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, idCtrlr);
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

      rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_NAMESPACE_IDS,
                         0, 0, nsAllocatedList);
      if (rc != 0) {
         Error("Failed to get allocated namespace list, 0x%x.", rc);
         goto out_free_active;
      }

      rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_NAMESPACE_IDS_ACTIVE,
                         0, 0, nsActiveList);
      if (rc != 0) {
         Error("Failed to attached namespace list, 0x%x.", rc);
         goto out_free_active;
      }
   }

   numNs = idCtrlr->nn < NVME_MAX_NAMESPACE_PER_CONTROLLER ?
           idCtrlr->nn : NVME_MAX_NAMESPACE_PER_CONTROLLER;

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

      if (status == VMK_NOT_FOUND && nsStatus == VMK_NOT_FOUND) {
         /* Mark statusFlags as NS_UNALLOCATED if namespace is not created */
         statusFlags[i-1] = NS_UNALLOCATED;
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
   int                          ch, rc, nsId = 0;
   const char                   *vmhba = NULL;
   struct nvme_adapter_list     list;
   struct nvme_handle           *handle;
   vmk_NvmeIdentifyNamespace    *idNs;

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

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_NAMESPACE_ACTIVE,
                      0, nsId, idNs);
   if (rc) {
      Error("Failed to get identify data for namespace %d, %s.",
            nsId, strerror(rc));
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
   const char               *vmhba = NULL;
   struct nvme_adapter_list    list;
   struct nvme_handle          *handle;
   vmk_NvmeIdentifyController  *id;

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

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, id);
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
   struct nvme_adapter_list    list;
   struct nvme_handle          *handle;
   vmk_NvmeIdentifyController  *idCtrlr;
   vmk_NvmeIdentifyNamespace   *idNs;
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

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      goto out_free_idCtrlr;
   }

   if((idCtrlr->oacs & VMK_NVME_CTLR_IDENT_OACS_FORMAT) == 0) {
      Error("NVM Format command is not supported.");
      goto out_free_idCtrlr;
   }

   idNs = malloc(sizeof(*idNs));
   if (idNs == NULL) {
      Error("Out of memory.");
      goto out_free_idCtrlr;
   }

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_NAMESPACE_ACTIVE,
                      0, nsId, idNs);
   if (rc != 0) {
      Error("Failed to get namespace identify information, 0x%x.", rc);
      goto out_free_idNs;
   }

   if (idNs->nlbaf < f) {
      Error("Invalid parameter: format %d exceeds supported format number %d.",
             f, idNs->nlbaf);
      goto out_free_idNs;
   }

   mdSize = idNs->lbaf[f].ms;

   if ((idNs->mc & VMK_NVME_MC_EXTENDED_LBA) == 0 && m == 1 && mdSize > 0) {
      Error("Invalid parameter: ms, namespace doesn't support metadata"
            " being tranferred as part of an extended data buffer.");
      goto out_free_idNs;
   }

   if ((idNs->mc & VMK_NVME_MC_SEPARATE_BUFFER) == 0 && m == 0 && mdSize > 0) {
      Error("Invalid parameter: ms, namespace doesn't support metadata"
            " being tranferred as part of a separate buffer.");
      goto out_free_idNs;
   }

   if (mdSize == 0 && p > 0) {
      Error("Invalid parameter: pi, PI cannot be enabled with"
            " zero metadata size.");
      goto out_free_idNs;
   }

   if ((idNs->dpc & VMK_NVME_DPC_PI_TYPE_1) == 0 && p == 1) {
      Error("Invalid parameter: pi, namespace doesn't support PI Type 1.");
      goto out_free_idNs;
   }

   if ((idNs->dpc & VMK_NVME_DPC_PI_TYPE_2) == 0 && p == 2) {
      Error("Invalid parameter: pi, namespace doesn't support PI Type 2.");
      goto out_free_idNs;
   }

   if ((idNs->dpc & VMK_NVME_DPC_PI_TYPE_3) == 0 && p == 3) {
      Error("Invalid parameter: pi, namespace doesn't support PI Type 3.");
      goto out_free_idNs;
   }

   if ((idNs->dpc & VMK_NVME_DPC_PI_FIRST_EIGHT) == 0 && l == 1 && p > 0) {
      Error("Invalid parameter: pil, namespace doesn't support PI data"
            " being transferred as first eight bytes of metadata.");
      goto out_free_idNs;
   }

   if ((idNs->dpc & VMK_NVME_DPC_PI_LAST_EIGHT) == 0 && l == 0 && p > 0) {
      Error("Invalid parameter: pil, namespace doesn't support PI data"
            " being transferred as last eight bytes of metadata.");
      goto out_free_idNs;
   }

   if ((idCtrlr->fna & VMK_NVME_CTLR_IDENT_FNA_CYPER) == 0 && s == 2) {
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
      /* If GetDeviceName returns VMK_NOT_FOUND, it indicates that the path is
       * dead or cannot be seen by upper layer for some reason.
       * It should be OK to directly do offline operation under this case.
       */
      if (status == VMK_OK) {
         snprintf(cmd, MAX_CMD_LEN,
                  "esxcli storage core claiming unclaim -t path -p %s",
                  runtimeName);
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
         Error("Format successfully, but failed to update namespace attributes"
               " after format. Offline namespace.");
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
      Error("Format, update namesapce attributes and online namespace"
            " successfully, but failed to rescan the filesystem."
            " A stale entry may exist.");
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
   char *telemetryPath = NULL;
   int  lid = -1;
   int  nsId = -1;
   int  elpe = -1;
   int  dataArea = -1;
   BOOL setNsid = 0;
   BOOL setElpe = 0;
   BOOL setDataarea = 0;
   BOOL setTelemetrypath = 0;
   int  i, rc;
   struct nvme_adapter_list    list;
   struct nvme_handle          *handle;
   vmk_NvmeIdentifyController  *idCtrlr;
   NvmeUserIo uio;
   int maxErrorLogEntries;
   union {
      vmk_NvmeErrorInfoLogEntry errLog[MAX_ERROR_LOG_ENTRIES];
      vmk_NvmeSmartInfoEntry smartLog;
      vmk_NvmeFirmwareSlotInfo fwSlotLog;
      vmk_NvmeTelemetryEntry telemetryLog;
   } log;

   while ((ch = getopt(argc, (char *const*)argv, "A:l:n:e:t:d")) != -1) {
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
         case 't':
            telemetryPath = optarg;
            setTelemetrypath = 1;
            break;
         case 'd':
            dataArea = atoi(optarg);
            setDataarea = 1;
            break;
         default:
            Error("Invalid parameter.");
            return;
      }
   }

   if (vmhba == NULL) {
      Error("Adapter is NULL.");
      return;
   }

   switch (lid) {
      case VMK_NVME_LID_ERROR_INFO:
      case VMK_NVME_LID_SMART_HEALTH:
      case VMK_NVME_LID_FW_SLOT:
      case VMK_NVME_LID_TELEMETRY_HOST_INITIATED:
      case VMK_NVME_LID_TELEMETRY_CONTROLLER_INITIATED:
         break;
      default:
         Error("Not supported log type %d.", lid);
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

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      goto out_free;
   }
   maxErrorLogEntries = (int)idCtrlr->elpe + 1;
   if (maxErrorLogEntries > MAX_ERROR_LOG_ENTRIES) {
      maxErrorLogEntries = MAX_ERROR_LOG_ENTRIES;
   }

  /* Check the optional parameters: nsId, eple, telemetryPath and dataArea. */
  if (setNsid) {
      if (lid == VMK_NVME_LID_SMART_HEALTH &&
          (idCtrlr->lpa & VMK_NVME_CTLR_IDENT_LPA_SMART_PER_NS)) {
         if (nsId < 1 || nsId > (int)idCtrlr->nn) {
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
      if (lid == VMK_NVME_LID_ERROR_INFO) {
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
      if (lid == VMK_NVME_LID_ERROR_INFO) {
         Error("Missing required parameter -e when using -l 1");
         goto out_free;
      }
   }

   if (setTelemetrypath || setDataarea) {
      if (lid == VMK_NVME_LID_TELEMETRY_HOST_INITIATED ||
          lid == VMK_NVME_LID_TELEMETRY_CONTROLLER_INITIATED) {
         if (!(idCtrlr->lpa & VMK_NVME_CTLR_IDENT_LPA_TELEMETRY)) {
            Error("Telemetry log page is not supported.");
            goto out_free;
         } else {
            if (!setTelemetrypath) {
               Error("Missing required parameter -t when using -l %d", lid);
               goto out_free;
            } else if (!setDataarea) {
               /* Choose data area 3 if not set */
               dataArea = 3;
            }
         }
      } else {
         Error("Invalid parameter.");
         goto out_free;
      }
   } else {
      if (lid == VMK_NVME_LID_TELEMETRY_HOST_INITIATED ||
          lid == VMK_NVME_LID_TELEMETRY_CONTROLLER_INITIATED) {
         Error("Missing required parameter -t when using -l %d", lid);
         goto out_free;
      }
   }

   if (lid == VMK_NVME_LID_TELEMETRY_HOST_INITIATED ||
       lid == VMK_NVME_LID_TELEMETRY_CONTROLLER_INITIATED) {
      rc = Nvme_GetTelemetryData(handle, telemetryPath, lid, dataArea);
      if (rc) {
         Error("Failed to get telemetry data, %s.", strerror(rc));
      } else {
         esxcli_xml_begin_output();
         xml_list_begin("string");
         printf("<string>Download telemetry data successfully.</string>");
         xml_list_end();
         esxcli_xml_end_output();
      }
      goto out_free;
   }

   memset(&uio, 0, sizeof(uio));
   uio.cmd.getLogPage.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_LOG_PAGE;
   uio.cmd.getLogPage.nsid = VMK_NVME_DEFAULT_NSID;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getLogPage.cdw10.lid = lid;

   switch (lid)
   {
      case VMK_NVME_LID_ERROR_INFO:
         uio.cmd.getLogPage.cdw10.numdl =
             sizeof(vmk_NvmeErrorInfoLogEntry) * elpe / 4 - 1;
         uio.length = sizeof(vmk_NvmeErrorInfoLogEntry) * elpe;
         uio.addr = (vmk_uintptr_t)&log.errLog;
         break;
      case VMK_NVME_LID_SMART_HEALTH:
         uio.cmd.getLogPage.nsid = nsId;
         uio.cmd.getLogPage.cdw10.numdl =
             sizeof(vmk_NvmeSmartInfoEntry) / 4 -1;
         uio.length = sizeof(vmk_NvmeSmartInfoEntry);
         uio.addr = (vmk_uintptr_t)&log.smartLog;
         break;
      case VMK_NVME_LID_FW_SLOT:
         uio.cmd.getLogPage.cdw10.numdl =
             sizeof(vmk_NvmeFirmwareSlotInfo) / 4 - 1;
         uio.length = sizeof(vmk_NvmeFirmwareSlotInfo);
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
      case VMK_NVME_LID_ERROR_INFO:
         esxcli_xml_begin_output();
         xml_list_begin("structure");
         for (i = 0; i < elpe; i++) {
            PrintErrLog(&log.errLog[i]);
         }
         xml_list_end();
         esxcli_xml_end_output();
         break;
      case VMK_NVME_LID_SMART_HEALTH:
         PrintSmartLog(&log.smartLog);
         break;
      case VMK_NVME_LID_FW_SLOT:
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
#define NVME_FEATURE_ERROR_STR strFeatErr((uio.comp.dw3.sct << 8) |  \
                                          uio.comp.dw3.sc)

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
   NvmeUserIo uio;
   memset(&uio, 0, sizeof(uio));

   uio.cmd.setFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_SET_FEATURES;
   uio.cmd.setFeatures.nsid = nsId;
   uio.cmd.setFeatures.cdw10.fid = fid;
   uio.cmd.setFeatures.cdw10.sv = save;
   uio.cmd.setFeatures.cdw11 = (vmk_NvmeSetFeaturesCdw11)dw11;
   uio.cmd.setFeatures.cdw12 = dw12;
   uio.cmd.setFeatures.cdw13 = dw13;
   uio.cmd.setFeatures.cdw14 = dw14;
   uio.cmd.setFeatures.cdw15 = dw15;
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
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.getFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getFeatures.cdw10.fid = VMK_NVME_FEATURE_ID_ARBITRATION;
   uio.cmd.getFeatures.cdw10.sel = select;

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.dw0;
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

void
setFeature_01h(struct nvme_handle *handle,
               int save,
               int nsId,
               int argc,
               const char **argv)
{
   int   ch, rc, burst = 0, low = 0, mid = 0, high = 0;
   char *burstStr = NULL;
   char *lowStr = NULL;
   char *midStr = NULL;
   char *highStr = NULL;
   NvmeUserIo uioReg;
   vmk_NvmeRegCap regs;
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

   if (regs.ams == 0) {
      if (low || mid || high) {
         Error("Invalid operation: Controller only support Round Robin arbitration"
               " mechanism, Low/Medium/High Priority Weight must be set to 0.");
         return;
      }
   }
   dw11 = burst | (low << 8) | (mid << 16) | (high << 24);
   issueSetFeature(handle, 0, VMK_NVME_FEATURE_ID_ARBITRATION,
      save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_02h(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.getFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getFeatures.cdw10.fid = VMK_NVME_FEATURE_ID_POWER_MANAGEMENT;
   uio.cmd.getFeatures.cdw10.sel = select;

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.dw0;
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
   vmk_NvmeIdentifyController idCtrlr;
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
   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }

   if (powerState > idCtrlr.npss || powerState < 0) {
      Error("Invalid parameter: power state setting is beyond supported: %d!",
            idCtrlr.npss);
      return;
   }
   if (!((idCtrlr.ver.mjr == 1 && idCtrlr.ver.mnr >= 2) ||
       (idCtrlr.ver.mjr >= 2))) {
      if (workloadStr != 0) {
         Error("Invalid parameter: 'Workload Hint' is only supported by the"
               " device whose version >= 1.2.");
         return;
      }
   }
   dw11 = powerState | (workload << 5);
   issueSetFeature(handle, 0, VMK_NVME_FEATURE_ID_POWER_MANAGEMENT,
                   save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_03h(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc, numRanges, i;
   NvmeUserIo uio;
   vmk_uint8 buf[4096];

   memset(&uio, 0, sizeof(uio));
   uio.cmd.getFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getFeatures.cdw10.fid = VMK_NVME_FEATURE_ID_LBA_RANGE_TYPE;
   uio.cmd.getFeatures.cdw10.sel = select;
   uio.addr = (vmk_uintptr_t)buf;
   uio.length = 4096;
   uio.cmd.getFeatures.nsid = nsId;

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.dw0;
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
      printf("<field name=\"Unique Identifier\"><string>"
             "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
             "</string></field>\n",
             buf[64 * i + 32], buf[64 * i + 33], buf[64 * i + 34],
             buf[64 * i + 35], buf[64 * i + 36], buf[64 * i + 37],
             buf[64 * i + 38], buf[64 * i + 39], buf[64 * i + 40],
             buf[64 * i + 41], buf[64 * i + 42], buf[64 * i + 43],
             buf[64 * i + 44], buf[64 * i + 45], buf[64 * i + 46],
             buf[64 * i + 47]);
      xml_struct_end();
   }
   xml_list_end();
   esxcli_xml_end_output();
}

int getSmartLog(struct nvme_handle *handle, vmk_NvmeSmartInfoEntry *smartLog)
{
   int rc;
   NvmeUserIo uio;
   memset(&uio, 0, sizeof(uio));
   uio.cmd.getLogPage.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_LOG_PAGE;
   uio.cmd.getLogPage.nsid = VMK_NVME_DEFAULT_NSID;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getLogPage.cdw10.lid = VMK_NVME_LID_SMART_HEALTH;
   uio.cmd.getLogPage.cdw10.numdl = sizeof(vmk_NvmeSmartInfoEntry) / 4 - 1;
   uio.length = sizeof(vmk_NvmeSmartInfoEntry);
   uio.addr = (vmk_uintptr_t)smartLog;
   rc = Nvme_AdminPassthru(handle, &uio);
   return rc;
}

void getFeature_04h(struct nvme_handle *handle, int select, int nsId)
{
   int rc;
   vmk_uint32 sensor = 0, overThreshold = 0, underThreshold = 0;
   vmk_NvmeIdentifyController idCtrlr;
   vmk_NvmeSmartInfoEntry smartLog;

   NvmeUserIo uio;
   memset(&uio, 0, sizeof(uio));
   uio.cmd.getFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getFeatures.cdw10.fid = VMK_NVME_FEATURE_ID_TEMP_THRESHOLD;
   uio.cmd.getFeatures.cdw10.sel = select;

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
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
         vmk_uint16 temp = (&smartLog.ts1)[sensor - 1];
         if (temp == 0) {
            // The temperature sensor is not implemented
            continue;
         }
      }
      if (sensor != 0 || idCtrlr.wctemp != 0) {
         uio.cmd.getFeatures.cdw11 = (sensor | 0x10) << 16;
         rc = Nvme_AdminPassthru(handle, &uio);
         if (rc) {
            continue;
         }
         underThreshold = uio.comp.dw0 & 0xffff;
      }

      uio.cmd.getFeatures.cdw11 = sensor << 16;
      rc = Nvme_AdminPassthru(handle, &uio);
      if (rc) {
         continue;
      }
      overThreshold = uio.comp.dw0 & 0xffff;

      xml_struct_begin("TemperatureThreshold");
      if (sensor == 0) {
         PSTR("Threshold Temperature Select", "Composite Temperature");
      } else {
         printf("<field name=\"Threshold Temperature Select\"><string>Temperature Sensor %d</string></field>\n", sensor);
      }
      if (sensor == 0 && idCtrlr.wctemp == 0) {
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
   vmk_NvmeIdentifyController idCtrlr;
   vmk_NvmeSmartInfoEntry smartLog;
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

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }

   rc = getSmartLog(handle, &smartLog);
   if (rc) {
      Error("Failed to get log info, %s.", strerror(rc));
      return;
   }

   if (sensor == 0 && under == 1 && idCtrlr.wctemp == 0) {
      Error("Invalid operation: The under temperature threshold Feature is not"
            " implemented for Composite Temperature.");
      return;
   }
   if (sensor != 0 && (&smartLog.ts1)[sensor - 1] == 0) {
      Error("Invalid operation: The Temperature sensor %d is not implemented.",
            sensor);
      return;
   }
   dw11 = threshold | (sensor << 16) | under << 20;
   issueSetFeature(handle, 0, VMK_NVME_FEATURE_ID_TEMP_THRESHOLD,
                   save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_05h(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   NvmeUserIo uio;
   vmk_NvmeIdentifyController idCtrlr;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.getFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getFeatures.cdw10.fid = VMK_NVME_FEATURE_ID_ERROR_RECOVERY;
   uio.cmd.getFeatures.cdw10.sel = select;
   uio.cmd.getFeatures.nsid = nsId;

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }
   if ((idCtrlr.ver.mjr == 1 && idCtrlr.ver.mnr >= 2) ||
       (idCtrlr.ver.mjr >= 2)) {
      if (nsId == 0) {
         Error("Invalid parameter: Must specify a valid namespace ID"
               " for the device whose version >= 1.2.");
         return;
      }
   } else {
      if (nsId != 0) {
         Error("Invalid parameter: Shouldn't specify namespace ID"
               " for a device whose version < 1.2.");
         return;
      }
   }

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.dw0;
   Debug("value = %x\n", value);
   esxcli_xml_begin_output();
   xml_struct_begin("ErrorRecovery");
   PINT("Time Limited Error Recovery", value & 0xffff);
   PBOOL("Deallocated or Unwritten Logical Block Error Enable",
         value & 0x10000);
   xml_struct_end();
   esxcli_xml_end_output();
}

void
setFeature_05h(struct nvme_handle *handle,
               int save,
               int nsId,
               int argc,
               const char **argv)
{
   int   ch, rc, dulbe = 0, time = 0;
   char *dulbeStr = NULL;
   char *timeStr = NULL;
   vmk_NvmeIdentifyController idCtrlr;
   vmk_NvmeIdentifyNamespace idNs;
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

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }
   if ((idCtrlr.ver.mjr == 1 && idCtrlr.ver.mnr >= 2) || (idCtrlr.ver.mjr >= 2) ) {
      if (nsId == 0) {
         Error("Invalid parameter: Must specify a valid namespace ID"
               " for the device whose version >= 1.2.");
         return;
      }
   } else {
      if (nsId != 0) {
         Error("Invalid parameter: Shouldn't specify namespace ID"
               " for a device whose version < 1.2.");
         return;
      }
      if (dulbe) {
         Error("Invalid parameter: Can't enable 'Deallocated or"
               " Unwritten Logical Block Error'."
               " It is not supported for a device whose version < 1.2.");
         return;
      }
   }

   if (dulbe) {
      rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_NAMESPACE_ACTIVE,
                         0, nsId, &idNs);
      if (rc) {
         Error("Failed to get identify data for namespace %d, %s.",
               nsId, strerror(rc));
         return;
      }
      if ((idNs.nsfeat & VMK_NVME_NS_DEALLOCATED_ERROR) == 0) {
         Error("Invalid operation: Can't enable Deallocated or Unwritten"
               " Logical Block Error, it's not supported for the namespace.");
         return;
      }
   }
   dw11 = time | (dulbe << 16);
   issueSetFeature(handle, nsId, VMK_NVME_FEATURE_ID_ERROR_RECOVERY,
                   save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_06h(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   NvmeUserIo uio;
   vmk_NvmeIdentifyController idCtrlr;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.getFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getFeatures.cdw10.fid = VMK_NVME_FEATURE_ID_VOLATILE_WRITE_CACHE;
   uio.cmd.getFeatures.cdw10.sel = select;

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }

   if ((idCtrlr.vwc &0x1) == 0) {
      Error("Failed to get this feature: controller has no write cache!");
      return;
   }

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.dw0;
   Debug("value = %x\n", value);

   esxcli_xml_begin_output();
   xml_struct_begin("VolatileWriteCache");
   PBOOL("Volatile Write Cache Enabled", value & 0x1);
   xml_struct_end();
   esxcli_xml_end_output();
}

void
setFeature_06h(struct nvme_handle *handle,
               int save,
               int nsId,
               int argc,
               const char **argv)
{
   int   ch, rc, enable = 0;
   char *enableStr = NULL;
   vmk_NvmeIdentifyController idCtrlr;
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
   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }

   if ((idCtrlr.vwc & 0x1) == 0) {
      Error("Failed to set this feature: controller has no write cache!");
      return;
   }

   if ((enable >> 1) != 0) {
      Error("Invalid parameter.");
      return;
   }
   dw11 = enable;
   issueSetFeature(handle, 0, VMK_NVME_FEATURE_ID_VOLATILE_WRITE_CACHE,
                   save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_07h(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.getFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getFeatures.cdw10.fid = VMK_NVME_FEATURE_ID_NUM_QUEUE;
   uio.cmd.getFeatures.cdw10.sel = select;

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.dw0;
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
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.getFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getFeatures.cdw10.fid = VMK_NVME_FEATURE_ID_INT_COALESCING;
   uio.cmd.getFeatures.cdw10.sel = select;

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, 0x%x.", rc);
      return;
   }

   value = uio.comp.dw0;
   Debug("value = %x\n", value);

   esxcli_xml_begin_output();
   xml_struct_begin("InterruptCoalescing");
   PINT("Aggregation Time", (value & 0xff00) >> 8);
   PINT("Aggregation Threshold", value & 0xff);
   xml_struct_end();
   esxcli_xml_end_output();
}

void
setFeature_08h(struct nvme_handle *handle,
               int save,
               int nsId,
               int argc,
               const char **argv)
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
   issueSetFeature(handle, 0, VMK_NVME_FEATURE_ID_INT_COALESCING,
                   save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_09h(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc, vectNum, i;
   NvmeUserIo uio;
   NvmeUserIo uioVect;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.getFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getFeatures.cdw10.fid = VMK_NVME_FEATURE_ID_INT_VECTOR_CONFIG;
   uio.cmd.getFeatures.cdw10.sel = select;

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
      uio.cmd.getFeatures.cdw11 = i & 0xffff;
      rc = Nvme_AdminPassthru(handle, &uio);
      if (rc) {
         continue;
      }
      value = uio.comp.dw0;
      xml_struct_begin("InterruptVectorConfiguration");
      PINT("Interrupt Vector", value & 0xffff);
      PBOOL("Coalescing Disable", value & 0x10000);
      xml_struct_end();
   }
   xml_list_end();
   esxcli_xml_end_output();
}

void
setFeature_09h(struct nvme_handle *handle,
               int save,
               int nsId,
               int argc,
               const char **argv)
{
   int   ch, rc, vectNum, vector = 0, disable = 0;
   char *vectorStr = NULL;
   char *disableStr = NULL;
   NvmeUserIo uioVect;
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
      Error("Invalid parameter: interrupt vector number"
            " is beyond supported: %d!",
            vectNum);
      return;
   }

   if (vector == 0) {
      Error("Invalid parameter: interrupt coalescing is not"
            " supported for admin queue!");
      return;
   }


   dw11 = vector | (disable << 16);
   issueSetFeature(handle, 0, VMK_NVME_FEATURE_ID_INT_VECTOR_CONFIG,
                   save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_0ah(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.getFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getFeatures.cdw10.fid = VMK_NVME_FEATURE_ID_WRITE_ATOMICITY;
   uio.cmd.getFeatures.cdw10.sel = select;

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.dw0;
   Debug("value = %x\n", value);

   esxcli_xml_begin_output();
   xml_struct_begin("WriteAtomicity");
   PBOOL("Disable Normal", value & 0x1);
   xml_struct_end();
   esxcli_xml_end_output();
}

void
setFeature_0ah(struct nvme_handle *handle,
               int save,
               int nsId,
               int argc,
               const char **argv)
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
   issueSetFeature(handle, 0, VMK_NVME_FEATURE_ID_WRITE_ATOMICITY,
                   save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_0bh(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.getFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getFeatures.cdw10.fid = VMK_NVME_FEATURE_ID_ASYNC_EVENT_CONFIG;
   uio.cmd.getFeatures.cdw10.sel = select;

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.dw0;
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

void
setFeature_0bh(struct nvme_handle *handle,
               int save,
               int nsId,
               int argc,
               const char **argv)
{
   int   ch, rc, smart = 0, namespace = 0, firmware = 0;
   char *smartStr = NULL;
   char *namespaceStr = NULL;
   char *firmwareStr = NULL;
   vmk_uint32 dw11;
   vmk_NvmeIdentifyController idCtrlr;

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

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }
   if (namespaceStr != NULL) {
      if (!(idCtrlr.oaes & VMK_NVME_CTLR_IDENT_OAES_NS_ATTRIBUTE)) {
         Error("Invalid parameter: The device don't support to"
               " set 'Namespace Activation Notices'");
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
      if (!(idCtrlr.oaes & VMK_NVME_CTLR_IDENT_OAES_FW_ACTIVATE)) {
         Error("Invalid parameter: The device don't support to set"
               " 'Firmware Activation Notices'");
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
   issueSetFeature(handle, 0, VMK_NVME_FEATURE_ID_ASYNC_EVENT_CONFIG,
                   save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_0ch(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc, i;
   NvmeUserIo uio;
   vmk_NvmeIdentifyController idCtrlr;
   vmk_uint64 buf[32];

   memset(&uio, 0, sizeof(uio));
   uio.cmd.getFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getFeatures.cdw10.fid =
      VMK_NVME_FEATURE_ID_AUTONOMOUS_POWER_STATE_TRANS;
   uio.cmd.getFeatures.cdw10.sel = select;
   uio.addr = (vmk_uintptr_t)buf;
   uio.length = 256;

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }

   if ((idCtrlr.apsta & 0x1) == 0) {
      Error("Invalid operation: The controller doesn't support"
            " autonomous power state transitions!");
      return;
   }

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.dw0;
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
      PINT("Idle Time Prior to Transition(milliseconds)",
           (buf[i] & 0xffffff00) >> 8);
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
   NvmeUserIo uio;
   vmk_uint32 buf[1024];
   vmk_NvmeIdentifyController idCtrlr;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.getFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getFeatures.cdw10.fid = VMK_NVME_FEATURE_ID_HOST_MEMORY_BUFFER;
   uio.cmd.getFeatures.cdw10.sel = select;
   uio.addr = (vmk_uintptr_t)buf;
   uio.length = 4096;

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }

   if (idCtrlr.hmpre == 0) {
      Error("Invalid operation: The controller doesn't support"
            " the Host Memory Buffer feature!");
      return;
   }

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.dw0;
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
   PULL("Host Memory Descriptor List Address",
        (vmk_uint64)buf[2] << 32  | buf[1]);
   PINTS("Host Memory Descriptor List Entry Count", buf[3]);
   xml_struct_end();
   xml_field_end();
   xml_struct_end();
   esxcli_xml_end_output();
}

void getFeature_0fh(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   NvmeUserIo uio;
   vmk_NvmeIdentifyController idCtrlr;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.getFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getFeatures.cdw10.fid = VMK_NVME_FEATURE_ID_KEEP_ALIVE_TIMER;
   uio.cmd.getFeatures.cdw10.sel = select;

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
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

   value = uio.comp.dw0;
   Debug("value = %x\n", value);

   esxcli_xml_begin_output();
   xml_struct_begin("KeepAliveTimer");
   PINTS("Keep Alive Timeout", value);
   xml_struct_end();
   esxcli_xml_end_output();
}

void
setFeature_0fh(struct nvme_handle *handle,
               int save,
               int nsId,
               int argc,
               const char **argv)
{
   int   ch, rc, timeout = 0;
   char *timeoutStr = NULL;
   vmk_NvmeIdentifyController idCtrlr;
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

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      return;
   }

   if (idCtrlr.kas == 0) {
      Error("Invalid operation: Keep Alive is not supported.");
      return;
   }

   dw11 = timeout;
   issueSetFeature(handle, 0, VMK_NVME_FEATURE_ID_KEEP_ALIVE_TIMER,
                   save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_80h(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   NvmeUserIo uio;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.getFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getFeatures.cdw10.fid =
      VMK_NVME_FEATURE_ID_SOFTWARE_PROGRESS_MARKER;
   uio.cmd.getFeatures.cdw10.sel = select;

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.dw0;
   Debug("value = %x\n", value);

   esxcli_xml_begin_output();
   xml_struct_begin("SoftwareProgressMarker");
   PINTS("Pre-boot Software Load Count", value & 0xff);
   xml_struct_end();
   esxcli_xml_end_output();
}

void
setFeature_80h(struct nvme_handle *handle,
               int save,
               int nsId,
               int argc,
               const char **argv)
{
   vmk_uint32 dw11;

   dw11 = 0;
   issueSetFeature(handle, 0, VMK_NVME_FEATURE_ID_SOFTWARE_PROGRESS_MARKER,
                   save, dw11, 0, 0, 0, 0, NULL, 0);
}

void getFeature_81h(struct nvme_handle *handle, int select, int nsId)
{
   int value, rc;
   NvmeUserIo uio;
   vmk_uint8 buf[16];

   memset(&uio, 0, sizeof(uio));
   uio.cmd.getFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.getFeatures.cdw10.fid = VMK_NVME_FEATURE_ID_HOST_ID;
   uio.cmd.getFeatures.cdw10.sel = select;
   uio.addr = (vmk_uintptr_t)buf;
   uio.length = 16;

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Error("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
      return;
   }

   value = uio.comp.dw0;
   Debug("value = %x\n", value);

   esxcli_xml_begin_output();
   xml_struct_begin("HostIdentifier");
   PBOOL("Enable Extended Host Identifier", value & 0x1);
   printf("<field name=\"Host Identifier\"><string>"
          "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
          "</string></field>\n",
          buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7],
          buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
   xml_struct_end();
   esxcli_xml_end_output();
}

struct Feature features[] = {
   {
      VMK_NVME_FEATURE_ID_ARBITRATION,
      "Arbitration",
      0,
      getFeature_01h,
      setFeature_01h,
   },
   {
      VMK_NVME_FEATURE_ID_POWER_MANAGEMENT,
      "Power Management",
      0,
      getFeature_02h,
      setFeature_02h,
   },
   {
      VMK_NVME_FEATURE_ID_LBA_RANGE_TYPE,
      "LBA Range Type",
      4096,
      getFeature_03h,
      NULL,
   },
   {
      VMK_NVME_FEATURE_ID_TEMP_THRESHOLD,
      "Temperature Threshold",
      0,
      getFeature_04h,
      setFeature_04h,
   },
   {
      VMK_NVME_FEATURE_ID_ERROR_RECOVERY,
      "Error Recovery",
      0,
      getFeature_05h,
      setFeature_05h,
   },
   {
      VMK_NVME_FEATURE_ID_VOLATILE_WRITE_CACHE,
      "Volatile Write Cache",
      0,
      getFeature_06h,
      setFeature_06h,
   },
   {
      VMK_NVME_FEATURE_ID_NUM_QUEUE,
      "Number of Queues",
      0,
      getFeature_07h,
      NULL,
   },
   {
      VMK_NVME_FEATURE_ID_INT_COALESCING,
      "Interrupt Coalescing",
      0,
      getFeature_08h,
      setFeature_08h,
   },
   {
      VMK_NVME_FEATURE_ID_INT_VECTOR_CONFIG,
      "Interrupt Vector Configuration",
      0,
      getFeature_09h,
      setFeature_09h,
   },
   {
      VMK_NVME_FEATURE_ID_WRITE_ATOMICITY,
      "Write Atomicity Normal",
      0,
      getFeature_0ah,
      setFeature_0ah,
   },
   {
      VMK_NVME_FEATURE_ID_ASYNC_EVENT_CONFIG,
      "Asynchronous Event Configuration",
      0,
      getFeature_0bh,
      setFeature_0bh,
   },
   {
      VMK_NVME_FEATURE_ID_AUTONOMOUS_POWER_STATE_TRANS,
      "Autonomous Power State Transition",
      256,
      getFeature_0ch,
      NULL,
   },
   {
      VMK_NVME_FEATURE_ID_HOST_MEMORY_BUFFER,
      "Host Memory Buffer",
      4096,
      getFeature_0dh,
      NULL,
   },
   {
      VMK_NVME_FEATURE_ID_KEEP_ALIVE_TIMER,
      "Keep Alive Timer",
      0,
      getFeature_0fh,
      setFeature_0fh,
   },
   {
      VMK_NVME_FEATURE_ID_SOFTWARE_PROGRESS_MARKER,
      "Software Progress Marker",
      0,
      getFeature_80h,
      setFeature_80h,
   },
   {
      VMK_NVME_FEATURE_ID_HOST_ID,
      "Host Identifier",
      16,
      getFeature_81h,
      NULL,
   },
   {
      VMK_NVME_FEATURE_ID_RESV_NOTIFICATION_MASK,
      "Reservation Notification Mask",
      0,
      NULL,
      NULL,
   },
   {
      VMK_NVME_FEATURE_ID_RESV_PERSISTENCE,
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
   const char               *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle       *handle;
   NvmeUserIo               uio;
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
      uio.cmd.getFeatures.cdw0.opc = VMK_NVME_ADMIN_CMD_GET_FEATURES;
      uio.direction = XFER_FROM_DEV;
      uio.timeoutUs = ADMIN_TIMEOUT;
      uio.cmd.getFeatures.cdw10.fid = features[i].fid;
      uio.cmd.getFeatures.cdw10.sel = 0x3;
      if (features[i].useBufferLen > 0) {
         uio.addr = (vmk_uintptr_t)buf;
         uio.length = features[i].useBufferLen;
      }

      rc = Nvme_AdminPassthru(handle, &uio);

      if (rc) {
         Debug("Failed to get feature, %s.", NVME_FEATURE_ERROR_STR);
         continue;
      }

      value = uio.comp.dw0;
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
   struct nvme_adapter_list     list;
   struct nvme_handle           *handle;
   vmk_NvmeIdentifyController   idCtrlr;
   int                          save = 0;
   struct Feature               *feature;

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

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc != 0) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      goto out;
   }

   if ((idCtrlr.oncs & VMK_NVME_CTLR_IDENT_ONCS_SV) == 0 && save == 1) {
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
   struct nvme_adapter_list    list;
   struct nvme_handle          *handle;
   vmk_NvmeIdentifyController  *idCtrlr;

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

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, idCtrlr);
   if (rc) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      goto out_free;
   }

   if ((idCtrlr->oacs & VMK_NVME_CTLR_IDENT_OACS_FIRMWARE) == 0) {
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
   struct nvme_adapter_list    list;
   struct nvme_handle          *handle;
   vmk_NvmeIdentifyController  *idCtrlr;

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

   rc = Nvme_Identify(handle, VMK_NVME_CNS_IDENTIFY_CONTROLLER, 0, 0, idCtrlr);
   if (rc) {
      Error("Failed to get controller identify information, 0x%x.", rc);
      goto out_free;
   }

   if ((idCtrlr->oacs & VMK_NVME_CTLR_IDENT_OACS_FIRMWARE) == 0) {
      Error("Firmware activate command is not supported.");
      goto out_free;
   }

   maxSlot = (idCtrlr->frmw & 0xf) >> 1;
   if (slot < 0 || slot > maxSlot) {
      Error("Invalid slot number.");
      goto out_free;
   }

   if (slot == 1 && (idCtrlr->frmw & 0x1) &&
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
            Error("The frimware activation would exceed the MFTA value"
                  " reported in identify controller. Please re-issue"
                  " activate command with other actions using a reset.");
            break;
         case 0x113:
            Error("The image specified is being prohibited from activation by"
                  " the controller for vendor specific reasons.");
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
         Error("Debug level is invalid when setting log level to %d.\n",
               logLevel);
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
   vmk_NvmeRegCap    regCap;
   vmk_NvmeRegVs     regVs;
   vmk_NvmeRegCc     regCc;
   vmk_NvmeRegCsts   regCsts;
   vmk_NvmeRegAqa    regAqa;

   esxcli_xml_begin_output();
   xml_struct_begin("DeviceRegs");

   regCap = *(vmk_NvmeRegCap *)(regs + VMK_NVME_REG_CAP);
   PULL("CAP", *(vmk_uint64 *)(regs + VMK_NVME_REG_CAP));
   PINTS("CAP.MPSMAX", regCap.mpsmax);
   PINTS("CAP.MPSMIN", regCap.mpsmin);
   PINTS("CAP.CSS", regCap.css);
   PINTS("CAP.NSSRS", regCap.nssrs);
   PINTS("CAP.DSTRD", regCap.dstrd);
   PINTS("CAP.TO", regCap.to);
   PINTS("CAP.AMS", regCap.ams);
   PINTS("CAP.CQR", regCap.cqr);
   PINTS("CAP.MQES", regCap.mqes);

   regVs = *(vmk_NvmeRegVs *)(regs + VMK_NVME_REG_VS);
   PINTS("VS", *(vmk_uint32 *)(regs + VMK_NVME_REG_VS));
   PINTS("VS.MJR", regVs.mjr);
   PINTS("VS.MNR", regVs.mnr);

   PINTS("INTMS", *(vmk_uint32 *)(regs + VMK_NVME_REG_INTMS));

   PINTS("INTMC", *(vmk_uint32 *)(regs + VMK_NVME_REG_INTMC));

   regCc = *(vmk_NvmeRegCc *)(regs + VMK_NVME_REG_CC);
   PINTS("CC", *(vmk_uint32 *)(regs + VMK_NVME_REG_CC));
   PINTS("CC.IOCQES", regCc.iocqes);
   PINTS("CC.IOSQES", regCc.iosqes);
   PINTS("CC.SHN", regCc.shn);
   PINTS("CC.AMS", regCc.ams);
   PINTS("CC.MPS", regCc.mps);
   PINTS("CC.CSS", regCc.css);
   PINTS("CC.EN", regCc.en);

   regCsts = *(vmk_NvmeRegCsts *)(regs + VMK_NVME_REG_CSTS);
   PINTS("CSTS", *(vmk_uint32 *)(regs + VMK_NVME_REG_CSTS));
   PINTS("CSTS.PP", regCsts.pp);
   PINTS("CSTS.NSSRO", regCsts.nssro);
   PINTS("CSTS.SHST", regCsts.shst);
   PINTS("CSTS.CFS", regCsts.cfs);
   PINTS("CSTS.RDY", regCsts.rdy);

   PINTS("NSSR", *(vmk_uint32 *)(regs + VMK_NVME_REG_NSSR));

   regAqa = *(vmk_NvmeRegAqa *)(regs + VMK_NVME_REG_AQA);
   PINTS("AQA", *(vmk_uint32 *)(regs + VMK_NVME_REG_AQA));
   PINTS("AQA.ACQS", regAqa.acqs);
   PINTS("AQA.ASQS", regAqa.asqs);

   PULL("ASQ", *(vmk_uint64 *)(regs + VMK_NVME_REG_ASQ));
   PULL("ACQ", *(vmk_uint64 *)(regs + VMK_NVME_REG_ACQ));
   PINTS("CMBLOC", *(vmk_uint32 *)(regs + VMK_NVME_REG_CMBLOC));
   PINTS("CMBSZ", *(vmk_uint32 *)(regs + VMK_NVME_REG_CMBSZ));
   xml_struct_end();
   esxcli_xml_end_output();
}

void
NvmePlugin_DeviceRegisterGet(int argc, const char *argv[])
{
   int                      ch;
   int                      rc;
   const char               *vmhba = NULL;
   struct nvme_adapter_list list;
   struct nvme_handle       *handle;
   NvmeUserIo               uio;
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
         printf("<string>Current timeout is 0."
                " Timeout checker is disabled.</string>");
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
   /* All functions are enabled by default.
    * Return false to disable the specific one.
    */
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
