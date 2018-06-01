/*********************************************************************************
 * Copyright (c) 2013 VMware, Inc. All rights reserved.
 * ******************************************************************************/

/*
 * @file: nvme_cli.c --
 *
 *    Command line management interface for NVM Express driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/queue.h>
#include <inttypes.h>
#include <unistd.h>

#include <vmkapi.h>

#include "nvme_cli.h"
#include "nvme_lib.h"


/*****************************************************************************
 * DriverCli Ops
 ****************************************************************************/

static int
DriverListCli_Dispatch(struct cli_context *cli, int argc, char *argv[])
{
   Output("VMware NVM Express Driver (nvme)");
   return 0;
}


/*****************************************************************************
 * DriverCli Ops
 ****************************************************************************/

static int
DeviceListCli_Dispatch(struct cli_context *cli, int argc, char *argv[])
{
   int i;
   Output("%s\t%s\t%s\t%s", "ID", "Name", "Status", "Signature");
   Output("-----------------------------------------");
   for (i = 0; i < adapterList.count; i++) {
      Output("%d\t%s\t%s\t%s", i, adapterList.adapters[i].name,
         adapterList.adapters[i].status == ONLINE ? "Online" : "Offline",
         adapterList.adapters[i].signature);
   }
   return 0;
}


/**
 * Validate arguments
 *
 * Acceptable args:
 *
 *    -A <vmhba>
 */
static int
DeviceInfoCli_ValidateArgs(struct cli_context *cli, int argc, char *argv[])
{
   int rc = 0;
   DEBUG("cli %s level %d argc %d", cli->name, cli->level, argc);

   if (argc - cli->level != 2) {
      return -EINVAL;
   }

   rc = strncmp(argv[CLI_ARG_1], "-A", VMK_MISC_NAME_MAX);

   return rc;
}

/**
 * Validate arguments
 *
 * Acceptable args:
 *
 *    get/set -A vmhbaX <feature> <value>
 */
static int
DeviceFeatureCli_ValidateArgs(struct cli_context *cli, int argc, char *argv[])
{
   int rc = 0;
   DEBUG("cli %s level %d argc %d", cli->name, cli->level, argc);

   if (argc == cli->level) {
      /* nvmecli device feature */
      return -EINVAL;
   }

   if (argc - cli->level != 4 && !strncmp(argv[CLI_ARG_2 + 1], "get", VMK_MISC_NAME_MAX)) {
      return -EINVAL;
   }

   if (!strncmp(argv[CLI_ARG_2 + 1], "set", VMK_MISC_NAME_MAX)) {
      if (!strncmp(argv[CLI_ARG_2 + 2], "arbitration", VMK_MISC_NAME_MAX)) {
         if (argc - cli->level != 8)
            return -EINVAL;
      } else if (!strncmp(argv[CLI_ARG_2 + 2], "num_queue", VMK_MISC_NAME_MAX)) {
         if (argc - cli->level != 6)
            return -EINVAL;
      } else if (!strncmp(argv[CLI_ARG_2 + 2], "int_coalescing", VMK_MISC_NAME_MAX)) {
         if (argc - cli->level != 6)
            return -EINVAL;
      } else if (!strncmp(argv[CLI_ARG_2 + 2], "int_vector_config", VMK_MISC_NAME_MAX)) {
         if (argc - cli->level != 6)
            return -EINVAL;
      } else if (argc - cli->level != 5)
         return -EINVAL;
   }

   rc = strncmp(argv[CLI_ARG_1], "-A", VMK_MISC_NAME_MAX)
           | (strncmp(argv[CLI_ARG_2 + 1], "set", VMK_MISC_NAME_MAX)
                 & strncmp(argv[CLI_ARG_2 + 1], "get", VMK_MISC_NAME_MAX));
   return rc;
}

/**
 * Validate arguments
 *
 * Acceptable args:
 *
 *    get -A vmhbaX <log> [ns id]
 */
static int
DeviceLogCli_ValidateArgs(struct cli_context *cli, int argc, char *argv[])
{
   int rc = 0;
   DEBUG("cli %s level %d argc %d", cli->name, cli->level, argc);

   if (!strncmp(argv[CLI_ARG_2 + 1], "smart_health", VMK_MISC_NAME_MAX)) {
      if (argc != 8 || strncmp(argv[CLI_ARG_2 + 2], "ns", VMK_MISC_NAME_MAX)) {
         return -EINVAL;
      } else {
         return 0;
      }
   }

   if (argc - cli->level != 3) {
      return -EINVAL;
   }

   rc = strncmp(argv[CLI_ARG_1], "-A", VMK_MISC_NAME_MAX);

   return rc;
}

/**
 * Validate arguments
 *
 * Acceptable args:
 *
 *  -A <vmhba> -s <slot> -f <firmware file>
 */
   static int
FWDownloadCli_ValidateArgs(struct cli_context *cli, int argc, char *argv[])
{
   char vmhba[MAX_ADAPTER_NAME_LEN];
   char fw_path[MAX_FW_PATH_LEN];
   int slot = -1;
   int option = -1;

   DEBUG("cli %s level %d argc %d", cli->name, cli->level, argc);

   if (argc != 10)
      return -EINVAL;

   while ((option = getopt(argc, argv, "A:s:f:")) != -1) {
      DEBUG("option = %c, optind = %d, optarg = %s.\n", option, optind, optarg);
      switch (option) {
         case 'A' : strncpy(vmhba, optarg, 1+strlen(optarg));
                    break;
         case 's' : slot = atoi(optarg);
                    break;
         case 'f' : strncpy(fw_path, optarg, 1+strlen(optarg));
                    break;
         default:
                    return -EINVAL;
      }
   }

   /* verify arguments */
   if ( slot < 1 || slot > MAX_FW_SLOT) {
      Output("slot number out of range.\n");
      return -EINVAL;
   }

   if (!strstr(vmhba, "vmhba")) {
      Output("invalid vmhba name.\n");
      return -EINVAL;
   }

   return 0;
}

/**
 * Validate arguments
 *
 * Acceptable args:
 *
 *  -A <vmhba> -s <slot>
 */
   static int
FWActivateCli_ValidateArgs(struct cli_context *cli, int argc, char *argv[])
{
   char vmhba[MAX_ADAPTER_NAME_LEN];
   int slot = -1;
   int option = -1;

   DEBUG("cli %s level %d argc %d", cli->name, cli->level, argc);

   if (argc != 8)
      return -EINVAL;

   while ((option = getopt(argc, argv, "A:s:f:")) != -1) {
      DEBUG("option = %c, optind = %d, optarg = %s.\n", option, optind, optarg);
      switch (option) {
         case 'A' : strncpy(vmhba, optarg, 1+strlen(optarg));
                    break;
         case 's' : slot = atoi(optarg);
                    break;
         default:
                    return -EINVAL;
      }
   }

   /* verify arguments */
   if ( slot < 1 || slot > MAX_FW_SLOT) {
      Output("slot number out of range.\n");
      return -EINVAL;
   }

   if (!strstr(vmhba, "vmhba")) {
      Output("invalid vmhba name.\n");
      return -EINVAL;
   }

   return 0;
}

/**
 * Validate arguments
 *
 * Acceptable args:
 *
 *    -A <vmhba>  err1[|err2]
 */
static int
ErrorNvmCli_ValidateArgs(struct cli_context *cli, int argc, char *argv[])
{
   int rc = 0;
   DEBUG("cli %s level %d argc %d", cli->name, cli->level, argc);

   if (argc - cli->level != 3) {
      return -EINVAL;
   }

   rc = strncmp(argv[CLI_ARG_1], "-A", VMK_MISC_NAME_MAX);

   return rc;
}

static void
PrintIdentifyCtrlr(struct iden_controller *id)
{
   Output("VID: 0x%04x\n"
      "SVID: 0x%04x\n"
      "Serial Number: %.20s\n"
      "Model: %.40s\n"
      "Firmware Revision: %.8s\n"
      "Recommended Arbitration Burst: 0x%02x\n"
      "IEEE OUT: 0x%02x 0x%02x 0x%02x\n"
      "Optional Admin Command Support: 0x%04x\n"
      "Abort Command Limit: 0x%02x\n"
      "Asynchronous Event Request Limit: 0x%02x\n"
      "Firmware Updates: 0x%02x\n"
      "Log Page Attributes: 0x%02x\n"
      "Error Log Page Entries: 0x%02x\n"
      "Number of Power States Support: 0x%02x\n"
      "Admin Vendor Specific Command Configuration: 0x%02x\n"
      "Submission Queue Entry Size: 0x%02x\n"
      "Completion Queue Entry Size: 0x%02x\n"
      "Number of Namespaces: %d\n"
      "Optional NVM Command Support: 0x%04x\n"
      "Fused Operation Support: 0x%04x\n"
      "Format NVM Attributes: 0x%02x\n"
      "Volatile Write Cache: 0x%02x\n"
      "Atomic Write Unit Normal: 0x%04x\n"
      "Atomic Write Unit Power Fail: 0x%04x\n"
      "NVM Vendor Specific Command Configuration: 0x%02x",
      id->pcieVID, id->pcieSSVID, id->serialNum, id->modelNum,
      id->firmwareRev, id->arbBurstSize, id->ieeeOui[0], id->ieeeOui[1],
      id->ieeeOui[2], id->adminCmdSup, id->abortCmdLmt, id->asyncReqLmt,
      id->firmUpdt, id->logPgAttrib, id->errLogPgEntr, id->numPowerSt,
      id->admVendCmdCfg, id->subQSize, id->compQSize, id->numNmspc,
      id->cmdSupt, id->fuseSupt, id->cmdAttrib, id->volWrCache,
      id->atomWrNorm, id->atomWrFail, id->nvmVendCmdCfg);

}


static const char *nvmNsRelPerf[] = {
   "Best performance",
   "Better performance",
   "Good performance",
   "Degraded performance",
};


static void
PrintIdentifyNs(struct iden_namespace *idNs)
{
   int lbaIndex;

   Output("Namespace Size: %" VMK_FMT64 "u\n"
      "Namespace Capacity: %" VMK_FMT64 "u\n"
      "Namespace Utilization: %" VMK_FMT64 "u\n"
      "Namespace Features: 0x%02x\n"
      "Number of LBA Formats: 0x%02x\n"
      "Formatted LBA Size: 0x%02x\n"
      "Metadata Capabilities: 0x%02x\n"
      "End-to-end Data Protection Capabilities: 0x%02x\n"
      "End-to-end Data Protection Type Settings: 0x%02x\n"
      "LBA Format Support: ",
      idNs->size, idNs->capacity, idNs->utilization,
      idNs->feat, idNs->numLbaFmt, idNs->fmtLbaSize,
      idNs->metaDataCap, idNs->dataProtCap, idNs->dataProtSet);

   for (lbaIndex = 0; lbaIndex <= idNs->numLbaFmt; lbaIndex ++) {
      Output("   %02d | Metadata Size: %5u, LBA Data Size: %5d, Relative Performance: %s",
         lbaIndex, idNs->lbaFmtSup[lbaIndex].metaSize,
         1 << idNs->lbaFmtSup[lbaIndex].dataSize,
         nvmNsRelPerf[idNs->lbaFmtSup[lbaIndex].relPerf]);
   }
}


static int
DeviceInfoCli_Dispatch(struct cli_context *cli, int argc, char *argv[])
{
   struct nvme_handle *handle;
   const char *vmhba;
   struct iden_controller idCtrlr;
   struct iden_namespace idNs;
   int i, numNs;
   int rc;

   vmhba = argv[CLI_ARG_2];

   handle = Nvme_Open(&adapterList, vmhba);
   if (!handle) {
      Output("Invalid argument: vmhba not found or vmhba not an NVM Express controller.");
      return -EINVAL;
   }

   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc) {
      Output("Failed to get controller info, %s.", strerror(rc));
      return rc;
   }

   Output("Identify Controller: %s", vmhba);
   Output("--------------------------");
   PrintIdentifyCtrlr(&idCtrlr);

   numNs = idCtrlr.numNmspc;
   for (i = 1; i <= numNs; i++) {
      rc = Nvme_Identify(handle, IDENTIFY_NAMESPACE, 0, i, &idNs);
      if (rc) {
         Output("Failed to get identify data for namespace %d, %s.", i, strerror(rc));
      } else {
         Output("");
         Output("Identify Namespace: %d", i);
         Output("--------------------------");
         PrintIdentifyNs(&idNs);
      }
   }

   Nvme_Close(handle);


   return 0;
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
LookupFtrId(char *ftr)
{
   int i;
   for(i = 0; i < MAX_NUM_FTR; i++) {
      if (strncmp(ftr, ftrList[i], VMK_MISC_NAME_MAX) == 0) {
         return i == 11 ? FTR_ID_SW_PROGRESS_MARKER : i + 1;
      }
   }
   return 0;
}
#if NVME_DEBUG_INJECT_ERRORS
static int
errInjectCli_ValidateArgs(struct cli_context *cli, int argc, char *argv[])
{
   vmk_uint8   iterator = CLI_ARG_1;
   vmk_uint32  errType;

   if (argc <= iterator) {
      Output("Invalid number of arguments");
      return -EINVAL;
   }

   if (strncmp(argv[iterator], "-A", VMK_MISC_NAME_MAX) == 0) {
      if (argc != 7) {
         Output("Invalid number of arguments");
         return -EINVAL;
      }

      iterator += 2;
   } else if (strncmp(argv[iterator], "-G", VMK_MISC_NAME_MAX) == 0) {
      if (argc != 6) {
         Output("Invalid number of arguments");
         return -EINVAL;
      }

      iterator += 1;
   } else {
      Output("Invalid argument.");
      return -EINVAL;
   }

   errType = atoi(argv[iterator++]);

   if (errType <= NVME_DEBUG_ERROR_NONE || errType >= NVME_DEBUG_ERROR_LAST) {
      Output("Invalid errType argument");
      return -EINVAL;
   }

   return 0;
}

static int
errInjectCli_Dispatch(struct cli_context *cli, int argc, char *argv[])
{
   vmk_uint8            iterator = CLI_ARG_1;
   vmk_uint32           errType;
   struct nvme_handle  *handle;
   const char          *vmhba;
   vmk_uint32           globalFlag;
   vmk_uint32           likelyhood;
   vmk_uint32           enableFlag;
   int                  rc;

   if (strncmp(argv[iterator], "-A", VMK_MISC_NAME_MAX) == 0) {
      iterator++;
      vmhba = argv[iterator];
      globalFlag = 0;
   } else if (strncmp(argv[iterator], "-G", VMK_MISC_NAME_MAX) == 0) {
      vmhba = adapterList.adapters[0].name;
      globalFlag = 1;
   } else {
      Output("This wont happen!!!!!!!");
      return VMK_FALSE;
   }

   iterator++;

   errType = atoi(argv[iterator++]);
   likelyhood = atoi(argv[iterator++]);
   enableFlag = atoi(argv[iterator++]);


   handle = Nvme_Open(&adapterList, vmhba);
   if (!handle) {
      Output("Invalid argument: vmhba not found or vmhba not an NVM Express controller.");
      return -EINVAL;
   }

   rc = vmk_MgmtUserCallbackInvoke(handle->handle,
                              0LL,
                              NVME_MGMT_CB_ERR_INJECT,
                              &globalFlag,
                              &errType,
                              &likelyhood,
                              &enableFlag);
   return rc;
}
#endif
static int
DeviceFeatureCli_Dispatch(struct cli_context *cli, int argc, char *argv[])
{
   struct nvme_handle *handle;
   const char *vmhba;
   int fid;
   int rc;
   struct usr_io uio;
   int isGet;
   int value;
   int tmp;

   isGet = strncmp(argv[CLI_ARG_2 + 1], "set", VMK_MISC_NAME_MAX);
   vmhba = argv[CLI_ARG_2];
   fid = LookupFtrId(argv[CLI_ARG_2 + 2]);

   if(!fid) {
      Output("Invalid feature name!\n");
      return -EINVAL;
   }

   handle = Nvme_Open(&adapterList, vmhba);
   if (!handle) {
      Output("Invalid argument: vmhba not found or vmhba not an NVM Express controller.");
      return -EINVAL;
   }

   memset(&uio, 0, sizeof(uio));

   if (isGet) {
      uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
      uio.direction = XFER_FROM_DEV;
      uio.timeoutUs = ADMIN_TIMEOUT;
      uio.cmd.cmd.getFeatures.featureID = fid;
      rc = Nvme_AdminPassthru(handle, &uio);

      if (rc) {
         Output("Failed to get feature info, %s.", strerror(rc));
         Nvme_Close(handle);
         return rc;
      }

      value = uio.comp.param.cmdSpecific;
      DEBUG("value = %x\n", value);
      switch (fid)
      {
      case FTR_ID_ARBITRATION:
         Output("ARBITRATION:\nHPW: %d\tMPW: %d\tLPW: %d\tAB: %d\n", (value & 0xff000000)>> 24, (value & 0xff0000) >> 16, (value & 0xff00) >> 8, value & 0x7);
         break;

      case FTR_ID_PWR_MANAGEMENT:
         Output("POWER MANAGEMENT:\nPower State: %d\n", value & 0x1f);
         break;

      case FTR_ID_LBA_RANGE_TYPE:
         break;
      case FTR_ID_TEMP_THRESHOLD:
         Output("TEMPERATURE THRESHOLD:\nTemperature Threshold: %d\n", value & 0xffff);
         break;

      case FTR_ID_ERR_RECOVERY:
         Output("ERROR RECOVERY:\nTime Limited Error Recovery: %d\n", value & 0xffff);
         break;

      case FTR_ID_WRITE_CACHE:
         Output("VOLATILE WRITE CACHE:\nVolatile Write Cache: ");
         value & 0x1 ? Output("Enable\n") : Output("Disable\n");
         break;

      case FTR_ID_NUM_QUEUE:
         Output("NUMBER OF QUEUES:\nNumber of Submission Queues Requested: %d\nNumber of Completion Queues Requested: %d\n", value & 0xffff, (value & 0xffff0000) >> 16);
         break;

      case FTR_ID_INT_COALESCING:
         Output("INTERRUPT COALESCING:\nAggregation Time: %d\nAggregation Threshold: %d\n", (value & 0xff00) >> 8, value & 0xff);
         break;

      case FTR_ID_INT_VECTOR_CONFIG:
         Output("INTERRUPT VECTOR CONFIGURATION:\nCoalescing Disable: %d\nInterrupt Vector:%d\n", (value & 0x10000) >> 16, value & 0xffff);
         break;

      case FTR_ID_WRITE_ATOMICITY:
         Output("WRITE ATOMICITY:\nDisable Normal: %d\n", value & 0x1);
         break;

      case FTR_ID_ASYN_EVENT_CONFIG:
         Output("ASYN EVENT CONFIGURATION:\nSMART/Health Critical Warnings: %d\n", value & 0xff);
         break;

      case FTR_ID_SW_PROGRESS_MARKER:
         Output("SOFTWARE PROGRESS MARKER:\nPre-boot Software Load Count: %d\n", value & 0xff);
         break;
      default:
         Output("Failed to get feature info, %s.", strerror(rc));
         break;
      }
   } else {
      uio.cmd.header.opCode = NVM_ADMIN_CMD_SET_FEATURES;
      uio.direction = XFER_TO_DEV;
      uio.timeoutUs = ADMIN_TIMEOUT;
      uio.cmd.cmd.setFeatures.featureID = fid;

      switch (fid)
      {
      case FTR_ID_ARBITRATION:
         tmp = atoi(argv[CLI_ARG_2 + 3]);
         tmp |= (atoi(argv[CLI_ARG_2 + 4]) << 8);
         uio.cmd.cmd.setFeatures.numSubQReq = tmp;

         tmp = atoi(argv[CLI_ARG_2 + 5]);
         tmp |= (atoi(argv[CLI_ARG_2 + 6]) << 8);
         uio.cmd.cmd.setFeatures.numCplQReq = tmp;
         break;

      case FTR_ID_PWR_MANAGEMENT:
         uio.cmd.cmd.setFeatures.numSubQReq = atoi(argv[CLI_ARG_2 + 3]);
         break;

      case FTR_ID_LBA_RANGE_TYPE:
         Output("Unimplement this feature.");
         Nvme_Close(handle);
         return -EINVAL;
      case FTR_ID_TEMP_THRESHOLD:
         uio.cmd.cmd.setFeatures.numSubQReq = atoi(argv[CLI_ARG_2 + 3]);
         break;

      case FTR_ID_ERR_RECOVERY:
         uio.cmd.cmd.setFeatures.numSubQReq = atoi(argv[CLI_ARG_2 + 3]);
         break;

      case FTR_ID_WRITE_CACHE:
         uio.cmd.cmd.setFeatures.numSubQReq = atoi(argv[CLI_ARG_2 + 3]);
         break;

      case FTR_ID_NUM_QUEUE:
         uio.cmd.cmd.setFeatures.numSubQReq = atoi(argv[CLI_ARG_2 + 3]);
         uio.cmd.cmd.setFeatures.numCplQReq = atoi(argv[CLI_ARG_2 + 4]);
         break;

      case FTR_ID_INT_COALESCING:
         tmp = atoi(argv[CLI_ARG_2 + 3]);
         tmp |= (atoi(argv[CLI_ARG_2 + 4]) << 8);
         uio.cmd.cmd.setFeatures.numSubQReq = tmp;
         break;

      case FTR_ID_INT_VECTOR_CONFIG:
         uio.cmd.cmd.setFeatures.numSubQReq = atoi(argv[CLI_ARG_2 + 3]);
         uio.cmd.cmd.setFeatures.numCplQReq = atoi(argv[CLI_ARG_2 + 4]) & 0x1;
         break;

      case FTR_ID_WRITE_ATOMICITY:
         uio.cmd.cmd.setFeatures.numSubQReq = atoi(argv[CLI_ARG_2 + 3]);
         break;

      case FTR_ID_ASYN_EVENT_CONFIG:
         uio.cmd.cmd.setFeatures.numSubQReq = atoi(argv[CLI_ARG_2 + 3]);
         break;

      case FTR_ID_SW_PROGRESS_MARKER:
         uio.cmd.cmd.setFeatures.numSubQReq = atoi(argv[CLI_ARG_2 + 3]);
         break;
      default:
         Output("Failed to set feature info.");
         Nvme_Close(handle);
         return -EINVAL;
      }
      rc = Nvme_AdminPassthru(handle, &uio);
      if (rc) {
         Output("Failed to set feature info, %s.", strerror(rc));
         Nvme_Close(handle);
         return rc;
      }
   }
   Nvme_Close(handle);
   return 0;
}

#define MAX_NUM_Log 3
const char logList[MAX_NUM_FTR][VMK_MISC_NAME_MAX] = {
   "err_info",
   "smart_health",
   "firmware_slot_info",
};

static int
LookupLogId(char *log)
{
   int i;
   for(i = 0; i < MAX_NUM_FTR; i++) {
      if (strncmp(log, logList[i], VMK_MISC_NAME_MAX) == 0) {
         return i + 1;
      }
   }
   return 0;
}

static void
PrintErrLog(struct error_log * errLog)
{
   Output("Error Count: 0x%" VMK_FMT64 "x\n"
      "Submission Queue ID: 0x%x\n"
      "Command ID: 0x%x\n"
      "Status Field: 0x%x\n"
      "Parameter Error Location: 0x%x\n"
      "LBA: 0x%" VMK_FMT64 "x\n"
      "Namespace: 0x%x\n"
      "Vendor Specific info Available: 0x%x\n",
      errLog->errorCount, errLog->sqID,
      errLog->cmdID, errLog->status,
      errLog->errorByte, errLog->lba,
      errLog->nameSpace, errLog->vendorInfo);
}

static void
PrintSmartLog(struct smart_log * smartLog)
{
   Output("Critical Warning: 0x%x\n"
      "Temperature: 0x%04x\n"
      "Available Spare: 0x%x\n"
      "Available Spare Threshold: 0x%x\n"
      "Percentage Used: 0x%x\n"
      "Data Units Read: 0x%" VMK_FMT64 "x%" VMK_FMT64 "x\n"
      "Data Units Written: 0x%" VMK_FMT64 "x%" VMK_FMT64 "x\n"
      "Host Read Commands: 0x%" VMK_FMT64 "x%" VMK_FMT64 "x\n"
      "Host Write Commands: 0x%" VMK_FMT64 "x%" VMK_FMT64 "x\n"
      "Controller Busy Time: 0x%" VMK_FMT64 "x%" VMK_FMT64 "x\n"
      "Power Cycles: 0x%" VMK_FMT64 "x%" VMK_FMT64 "x\n"
      "Power On Hours: 0x%" VMK_FMT64 "x%" VMK_FMT64 "x\n"
      "Unsafe Shutdowns: 0x%" VMK_FMT64 "x%" VMK_FMT64 "x\n"
      "Media Errors: 0x%" VMK_FMT64 "x%" VMK_FMT64 "x\n"
      "Number of Error Info Log Entries: 0x%" VMK_FMT64 "x%" VMK_FMT64 "x\n",
      smartLog->criticalError,
      *(vmk_uint16 *)smartLog->temperature, smartLog->availableSpace,
      smartLog->availableSpaceThreshold, smartLog->percentageUsed,
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
   Output("Active Firmware Info: %d\n"
      "Firmware Revision for Slot 1: %.8s\n"
      "Firmware Revision for Slot 2: %.8s\n"
      "Firmware Revision for Slot 3: %.8s\n"
      "Firmware Revision for Slot 4: %.8s\n"
      "Firmware Revision for Slot 5: %.8s\n"
      "Firmware Revision for Slot 6: %.8s\n"
      "Firmware Revision for Slot 7: %.8s\n",
      fwSlotLog->activeFirmwareInfo,
      fwSlotLog->FirmwareRevisionSlot1, fwSlotLog->FirmwareRevisionSlot2,
      fwSlotLog->FirmwareRevisionSlot3, fwSlotLog->FirmwareRevisionSlot4,
      fwSlotLog->FirmwareRevisionSlot5, fwSlotLog->FirmwareRevisionSlot6,
      fwSlotLog->FirmwareRevisionSlot7);
}

static int
DeviceLogCli_Dispatch(struct cli_context *cli, int argc, char *argv[])
{
   struct nvme_handle *handle;
   const char *vmhba;
   int lid;
   int rc;
   struct usr_io uio;
   union {
      struct error_log errLog;
      struct smart_log smartLog;
      struct firmware_slot_log fwSlotLog;
   } log;

   vmhba = argv[CLI_ARG_2];
   lid = LookupLogId(argv[CLI_ARG_2 + 1]);

   if(!lid) {
      Output("Invalid feature name!\n");
      return -EINVAL;
   }

   handle = Nvme_Open(&adapterList, vmhba);
   if (!handle) {
      Output("Invalid argument: vmhba not found or vmhba not an NVM Express controller.");
      return -EINVAL;
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
      uio.cmd.cmd.getLogPage.numDW = GLP_LEN_ERR_INFO / 4 - 1;
      uio.length = GLP_LEN_ERR_INFO;
      uio.addr = (vmk_uintptr_t)&log.errLog;
      break;

   case GLP_ID_SMART_HEALTH:
      uio.cmd.header.namespaceID = atoi(argv[CLI_ARG_2 + 3]);
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
      Output("Invalid feature name!\n");
      Nvme_Close(handle);
      return -EINVAL;
   }

   rc = Nvme_AdminPassthru(handle, &uio);

   if (rc) {
      Output("Failed to get log info, %s.", strerror(rc));
      Nvme_Close(handle);
      return rc;
   }

   switch (lid)
   {
   case GLP_ID_ERR_INFO:
      PrintErrLog(&log.errLog);
      break;

   case GLP_ID_SMART_HEALTH:
      PrintSmartLog(&log.smartLog);
      break;

    case GLP_ID_FIRMWARE_SLOT_INFO:
      PrintFwSlotLog(&log.fwSlotLog);
      break;

   default:
      Output("Invalid feature name!\n");
      Nvme_Close(handle);
      return -EINVAL;
   }

   Nvme_Close(handle);
   return 0;
}

static int
FWDownloadCli_Dispatch(struct cli_context *cli, int argc, char *argv[])
{
   struct nvme_handle *handle;
   void *fw_buf = NULL;
   int fw_size = 0;
   int rc = 0;
   char vmhba[MAX_ADAPTER_NAME_LEN];
   char fw_path[MAX_FW_PATH_LEN];
   int slot = -1;
   int i = 0;
   struct iden_controller idCtrlr;
   int slot_max = 0;
   int slot_ro = 0;
   int status = 0;

   for (i=0;i<argc;i++) {
      if (!strcmp(argv[i], "-A"))
      { strncpy(vmhba, argv[i+1], 1+strlen(argv[i+1]));   continue; }
      if (!strcmp(argv[i], "-f"))
      { strncpy(fw_path, argv[i+1], 1+strlen(argv[i+1]));   continue; }
      if (!strcmp(argv[i], "-s"))
      { slot = atoi(argv[i+1]);   continue; }
   }
   DEBUG("vmhba=%s, slot=%d, file=%s.\n", vmhba, slot, fw_path);

   handle = Nvme_Open(&adapterList, vmhba);
   if (!handle) {
      Output("Invalid argument: vmhba not found or vmhba not an NVM Express controller.");
      return -ENXIO;
   }

   /* check if slot is avaible */
   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc) {
      Output("Failed to get controller info, %s.", strerror(rc));
      goto exit;
   }
   slot_max = (idCtrlr.firmUpdt & 0xF) >> 1;
   slot_ro = idCtrlr.firmUpdt & 1;
   if (slot > slot_max) {
      Output("Download Firmware failed: slot %d is not available.\n", slot);
      goto exit;
   }
   if (slot==1 && slot_ro) {
      Output("Download Firmware failed: slot 1 is readonly.\n");
      goto exit;
   }

   /* load firmware image */
   rc = Nvme_FWLoadImage(fw_path, &fw_buf, &fw_size);
   if (rc) {
      Output("Fail to read NVMe firmware image file.\n");
      goto exit;
   }

   printf("Start download firmware to slot %d.\n", slot);
   /* download firmware image*/
   rc = Nvme_FWDownload(handle, fw_buf, fw_size);
   if (rc) {
      Output("Fail to update NVMe firmware.\n");
      goto exit;
   }

   /* replace fw in dedicated slot without activate */
   rc = Nvme_FWActivate(handle, slot, NVME_FIRMWARE_ACTIVATE_ACTION_NOACT, &status);
   if (rc == 0) {
      printf("Great! Download firmware successful.\n");
   } else if (status == 0x10b || status == 0x110 || status == 0x111) {
      printf("Download NVMe firmware successful but need reboot.\n");
      rc = 0;
   } else {
      Output("Fail to activate NVMe firmware.\n");
   }

exit:
   if(fw_buf)
      free(fw_buf);
   Nvme_Close(handle);
   return rc;
}

static int
FWActivateCli_Dispatch(struct cli_context *cli, int argc, char *argv[])
{
   struct nvme_handle *handle;
   char vmhba[MAX_ADAPTER_NAME_LEN];
   int slot = -1;
   int rc = -1;
   int i = 0;
   struct iden_controller idCtrlr;
   int slot_max = 0;
   int status = 0;

   for (i=0;i<argc;i++) {
      if (!strcmp(argv[i], "-A"))
      { strncpy(vmhba, argv[i+1], 1+strlen(argv[i+1]));   continue; }
      if (!strcmp(argv[i], "-s"))
      { slot = atoi(argv[i+1]);   continue; }
   }
   DEBUG("vmhba=%s, slot=%d.\n", vmhba, slot);

   handle = Nvme_Open(&adapterList, vmhba);
   if (!handle) {
      Output("Invalid argument: vmhba not found or vmhba not an NVM Express controller.");
      return -ENXIO;
   }

   /* check if slot is avaible */
   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc) {
      Output("Failed to get controller info, %s.", strerror(rc));
      goto exit;
   }
   slot_max = (idCtrlr.firmUpdt & 0xF) >> 1;
   if (slot > slot_max) {
      Output("Activate Firmware failed: slot %d is not available.\n", slot);
      rc = -ENXIO;
      goto exit;
   }

   /* activate fw */
   rc = Nvme_FWActivate(handle, slot, NVME_FIRMWARE_ACTIVATE_ACTION_ACTIVATE, &status);
   if (rc == 0) {
      printf("Activate NVMe firmware successful.\n");
   } else if (status == 0x10b || status == 0x110 || status == 0x111) {
      printf("Activate NVMe firmware successful but need reboot.\n");
      rc = 0;
   } else {
      Output("Fail to activate NVMe firmware.\n");
   }

exit:
   Nvme_Close(handle);
   return rc;
}

/**
 * Validate arguments for namespace command
 *
 * Acceptable args:
 *
 *    create <ns, snu, nnu, vmhba>
 *    delete <ns>
 */
   static int
NamespaceCli_ValidateArgs(struct cli_context *cli, int argc, char *argv[])
{
   DEBUG("cli %s level %d argc %d", cli->name, cli->level, argc);
   if ((cli->level  == 3) &&
         (strncmp(argv[CLI_ARG_1 - 1], "ns", VMK_MISC_NAME_MAX) == 0)) {
      if ((argc == 9) &&
            (strncmp(argv[CLI_ARG_2 + 1], "create", VMK_MISC_NAME_MAX) == 0)) {
         return 0;
      }
      else if ((argc == 7) &&
            (strncmp(argv[CLI_ARG_2 + 1], "delete", VMK_MISC_NAME_MAX)== 0)) {
         return 0;
      }
   }
   Output("cli level mismatches or invalid command!");
   return -EINVAL;
}


   static int
NamespaceCli_Delete(vmk_uint32 ns, const char *vmhba)
{
   struct nvme_handle *handle;
   struct iden_controller idCtrlr;
   int rc;

   handle = Nvme_Open(&adapterList, vmhba);
   if (!handle) {
      Output("Invalid argument: vmhba not found or vmhba not an NVM Express controller.");
      return -EINVAL;
   }
   /* Identify the current namespace related information.*/
   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc) {
      Output("Failed to get controller info, %s.", strerror(rc));
      goto out;
   }
   if((ns < 1) || (ns > idCtrlr.numNmspc)) {
      Output("ns = %d is an Invalid namespace identifier!", ns);
      goto out;
   }
   switch(idCtrlr.pcieVID) {
      case IDT_DEVICE:
         rc = Nvme_DeleteNamespace_IDT(handle, ns);
         break;
      default:
         Output("delete namespace not supported on this controller!");
         goto out;
   }
   if(rc) {
      Output("Failed to delete namespace %d of controller %s", ns, vmhba);
   }
   else {
      Output("Successfully deleted namespace %d of controller %s", ns, vmhba);
   }
out:
   Nvme_Close(handle);
   return rc;

}

static int
NamespaceCli_Create(vmk_uint32 ns, vmk_uint32 snu, vmk_uint32 nnu, const char *vmhba)
{
   struct nvme_handle *handle;
   struct iden_controller idCtrlr;
   int rc;

   handle = Nvme_Open(&adapterList, vmhba);
   if (!handle) {
      Output("Invalid argument: vmhba not found or vmhba not an NVM Express controller.");
      return -EINVAL;
   }

   /* Identify controller before issuing command*/
   rc = Nvme_Identify(handle, IDENTIFY_CONTROLLER, 0, 0, &idCtrlr);
   if (rc) {
      Output("Failed to get controller info, %s.", strerror(rc));
      goto out;
    }
   if (ns <= idCtrlr.numNmspc) {
      Output("ns = %d is Invalid or Already Existing !", ns);
      goto out;
   }
   switch(idCtrlr.pcieVID) {
      case IDT_DEVICE:
         rc = Nvme_CreateNamespace_IDT(handle, ns, snu, nnu);
         break;
      default:
         Output("create namespace not supported on this controller!");
         goto out;
   }

   /*Issue Create Namespace command to device.*/
   if(rc) {
      Output("Failed to create namesapce %d for controller %s", ns, vmhba);
   }
   else {
      Output("Successfully created namespace %d for controller %s", ns, vmhba);
   }
out:
   Nvme_Close(handle);
   return rc;
}


static int
NamespaceCli_Dispatch(struct cli_context *cli, int argc, char *argv[])
{
   const char *vmhba;
   vmk_uint32 ns;

   /*snu: the starting namespace unit associated with the namespace */
   vmk_uint32 snu;

   /*nnu: the total number of namespace units associated with the namespace*/
   vmk_uint32 nnu;

   vmhba =  argv[CLI_ARG_2];
   ns = atoi(argv[CLI_ARG_2 + 2]);
   if(strncmp(argv[CLI_ARG_2 + 1], "create", VMK_MISC_NAME_MAX) == 0) {
      snu = atol(argv[CLI_ARG_2 + 3]);
      nnu = atol(argv[CLI_ARG_2 + 4]);
      Output("issue create namespace command to %s, ns=%d, snu=%d,nnu=%d", vmhba, ns, snu, nnu);
      return NamespaceCli_Create(ns, snu, nnu, vmhba);
   }
   else if(strncmp(argv[CLI_ARG_2 + 1], "delete", VMK_MISC_NAME_MAX) == 0){
      Output("issue delete namespace command to %s, ns = %d", vmhba, ns);
      return NamespaceCli_Delete(ns, vmhba);
   }
   else {
      Output("Invalid parameter, only create and delete are supported");
      return  -EINVAL;
   }
}

struct cap {
   vmk_uint64 MQES   : 16;
   vmk_uint64 CQR    : 1;
   vmk_uint64 AMS    : 2;
   vmk_uint64 rsvd1  : 5;
   vmk_uint64 TO     : 8;
   vmk_uint64 DSTRD  : 4;
   vmk_uint64 rsvd2  : 1;
   vmk_uint64 CSS    : 8;
   vmk_uint64 rsvd3  : 3;
   vmk_uint64 MPSMIN : 4;
   vmk_uint64 MPSMAX : 4;
   vmk_uint64 rsvd4  : 8;
} VMK_ATTRIBUTE_PACKED;

struct vs {
   vmk_uint16 MNR;
   vmk_uint16 MJR;
} VMK_ATTRIBUTE_PACKED;

struct cc {
   vmk_uint32 EN     : 1;
   vmk_uint32 rsvd1  : 3;
   vmk_uint32 CSS    : 3;
   vmk_uint32 MPS    : 4;
   vmk_uint32 AMS    : 3;
   vmk_uint32 SHN    : 2;
   vmk_uint32 IOSQES : 4;
   vmk_uint32 IOCQES : 4;
   vmk_uint32 rsvd2  : 8;
} VMK_ATTRIBUTE_PACKED;

struct csts {
   vmk_uint32 RDY    : 1;
   vmk_uint32 CFS    : 1;
   vmk_uint32 SHST   : 2;
   vmk_uint32 rsvd1  : 28;
} VMK_ATTRIBUTE_PACKED;

struct aqa {
   vmk_uint32 ASQS   : 12;
   vmk_uint32 rsvd1  : 4;
   vmk_uint32 ACQS   : 12;
   vmk_uint32 rsvd2  : 4;
} VMK_ATTRIBUTE_PACKED;


VMK_ASSERT_LIST(nvme_registers,
   VMK_ASSERT_ON_COMPILE(sizeof(struct cap) == sizeof(vmk_uint64));
   VMK_ASSERT_ON_COMPILE(sizeof(struct vs) == sizeof(vmk_uint32));
   VMK_ASSERT_ON_COMPILE(sizeof(struct cc) == sizeof(vmk_uint32));
   VMK_ASSERT_ON_COMPILE(sizeof(struct csts) == sizeof(vmk_uint32));
   VMK_ASSERT_ON_COMPILE(sizeof(struct aqa) == sizeof(vmk_uint32));
)


static void
PrintRegs(void *regs, int length)
{
   struct cap *cap;
   struct vs *vs;
   struct cc *cc;
   struct csts *csts;
   struct aqa *aqa;

   cap = (struct cap *)(regs + NVME_CAP);
   vs = (struct vs *)(regs + NVME_VS);
   cc = (struct cc *)(regs + NVME_CC);
   csts = (struct csts *)(regs + NVME_CSTS);
   aqa = (struct aqa *)(regs + NVME_AQA);

   Output("NVM Register Dumps");
   Output("--------------------------");
   Output("CAP    : 0x%016" VMK_FMT64 "X", *(vmk_uint64 *)(regs + NVME_CAP));
   Output("   CAP.MPSMAX   : 0x%X", cap->MPSMAX);
   Output("   CAP.MPSMIN   : 0x%X", cap->MPSMIN);
   Output("   CAP.CSS      : 0x%X", cap->CSS);
   Output("   CAP.DSTRD    : 0x%X", cap->DSTRD);
   Output("   CAP.TO       : 0x%X", cap->TO);
   Output("   CAP.AMS      : 0x%X", cap->AMS);
   Output("   CAP.CQR      : 0x%X", cap->CQR);
   Output("   CAP.MQES     : 0x%X", cap->MQES);
   Output("");

   Output("VS     : 0x%08X", *(vmk_uint32 *)(regs + NVME_VS));
   Output("   VS.MJR       : 0x%X", vs->MJR);
   Output("   VS.MNR       : 0x%X", vs->MNR);
   Output("");

   Output("INTMS  : 0x%08X", *(vmk_uint32 *)(regs + NVME_INTMS));
   Output("");

   Output("INTMC  : 0x%08X", *(vmk_uint32 *)(regs + NVME_INTMC));
   Output("");

   Output("CC     : 0x%08X", *(vmk_uint32 *)(regs + NVME_CC));
   Output("   CC.IOCQES    : 0x%X", cc->IOCQES);
   Output("   CC.IOSQES    : 0x%X", cc->IOSQES);
   Output("   CC.SHN       : 0x%X", cc->SHN);
   Output("   CC.AMS       : 0x%X", cc->AMS);
   Output("   CC.MPS       : 0x%X", cc->MPS);
   Output("   CC.CSS       : 0x%X", cc->CSS);
   Output("   CC.EN        : 0x%X", cc->EN);
   Output("");

   Output("CSTS   : 0x%08X", *(vmk_uint32 *)(regs + NVME_CSTS));
   Output("   CSTS.SHST    : 0x%X", csts->SHST);
   Output("   CSTS.CFS     : 0x%X", csts->CFS);
   Output("   CSTS.RDY     : 0x%X", csts->RDY);
   Output("");

   Output("AQA    : 0x%08X", *(vmk_uint32 *)(regs + NVME_AQA));
   Output("   AQA.ACQS     : 0x%X", aqa->ACQS);
   Output("   AQA.ASQS     : 0x%X", aqa->ASQS);
   Output("");

   Output("ASQ    : 0x%016" VMK_FMT64 "X", *(vmk_uint64 *)(regs + NVME_ASQ));
   Output("");

   Output("ACQ    : 0x%016" VMK_FMT64 "X", *(vmk_uint64 *)(regs + NVME_ACQ));
}


static int
DeviceRegsCli_Dispatch(struct cli_context *cli, int argc, char *argv[])
{
   struct nvme_handle *handle;
   const char *vmhba;
   struct usr_io uio;
   vmk_uint8 regs[8192];
   int rc;

   vmhba = argv[CLI_ARG_2];

   handle = Nvme_Open(&adapterList, vmhba);
   if (!handle) {
      Output("Invalid argument: vmhba not found or vmhba not an NVM Express controller.");
      return -EINVAL;
   }

   memset(&uio, 0, sizeof(uio));
   uio.addr = (uintptr_t)&regs;
   uio.length = sizeof(regs);

   rc = Nvme_Ioctl(handle, NVME_IOCTL_DUMP_REGS, &uio);
   if (rc) {
      Output("Failed to dump registers, %s.", strerror(rc));
      goto out;
   }

   PrintRegs(regs, uio.meta_length);

out:
   Nvme_Close(handle);

   return rc;
}


static int
DeviceOnlineCli_Dispatch(struct cli_context *cli, int argc, char *argv[])
{
   struct nvme_handle *handle;
   const char *vmhba;
   struct usr_io uio;
   int rc;

   vmhba = argv[CLI_ARG_2];

   handle = Nvme_Open(&adapterList, vmhba);
   if (!handle) {
      Output("Invalid argument:vmhba not found or vmhba not an NVM Express controller. ");
      return -EINVAL;
   }

   memset(&uio, 0, sizeof(uio));

   rc = Nvme_Ioctl(handle, NVME_IOCTL_SET_CTRLR_ONLINE, &uio);
   if (rc) {
      Output("Failed to online controller, %s.", strerror(rc));
   }
   Nvme_Close(handle);
   return rc;
}

static int
DeviceOfflineCli_Dispatch(struct cli_context *cli, int argc, char *argv[])
{
   struct nvme_handle *handle;
   const char *vmhba;
   struct usr_io uio;
   int rc;

   vmhba = argv[CLI_ARG_2];

   handle = Nvme_Open(&adapterList, vmhba);
   if (!handle) {
      Output("Invalid argument:vmhba not found or vmhba not an NVM Express controller. ");
      return -EINVAL;
   }

   memset(&uio, 0, sizeof(uio));

   rc = Nvme_Ioctl(handle, NVME_IOCTL_SET_CTRLR_OFFLINE, &uio);
   if (rc) {
      Output("Failed to online controller, %s.", strerror(rc));
   }

   Nvme_Close(handle);
   return rc;
}



static int
FormatNvmClid_ValidateArgs(struct cli_context *cli, int argc, char *argv[])
{
   const char *vmhba = NULL;
   int opt, s = -1, l = -1, p = -1, m = -1, f = -1;
   int indent = cli->level - 1;

   /**
    * Reset optind, just in case.
    */
   optind = 1;

   while ((opt = getopt(argc - indent, argv + indent, "A:s:l:p:m:f:")) != -1) {
      switch (opt) {
         case 'A':
            vmhba = optarg;
            break;
         case 's':
            s = atoi(optarg);
            break;
         case 'l':
            l = atoi(optarg);
            break;
         case 'p':
            p = atoi(optarg);
            break;
         case 'm':
            m = atoi(optarg);
            break;
         case 'f':
            f = atoi(optarg);
            break;
         default:
            return -EINVAL;
            break;
      }
   }

   if (optind + indent >= argc) {
      return -EINVAL;
   }

   /**
    * Validate some of the args
    */
   if (vmhba == NULL       ||    /* must provide vmhba */
       (s < 0 || s > 2)    ||    /* SES must between 0, 1, 2 */
       (l != 0 && l != 1)  ||    /* PIL must be 0 or 1 */
       (p < 0 || p > 3)    ||    /* PI must be 0, 1, 2, or 3 */
       (m != 0 && m != 1)  ||    /* MS must be 0 or 1 */
       (f < 0 || f > 16))        /* LBAF must be between 0 and 15 */
   {
      return -EINVAL;
   }

   return 0;
}


static int
FormatNvmCli_Dispatch(struct cli_context *cli, int argc, char *argv[])
{
   struct nvme_handle *handle;
   int rc;
   const char *vmhba = NULL;
   int opt, s = -1, l = -1, p = -1, m = -1, f = -1, nsid = -1;
   int indent = cli->level - 1;

   /**
    * Reset optind, just in case.
    */
   optind = 1;

   while ((opt = getopt(argc - indent, argv + indent, "A:s:l:p:m:f:")) != -1) {
      switch (opt) {
         case 'A':
            vmhba = optarg;
            break;
         case 's':
            s = atoi(optarg);
            break;
         case 'l':
            l = atoi(optarg);
            break;
         case 'p':
            p = atoi(optarg);
            break;
         case 'm':
            m = atoi(optarg);
            break;
         case 'f':
            f = atoi(optarg);
            break;
         default:
            cli->usage(cli);
            return -EINVAL;
            break;
      }
   }

   nsid = atoi(argv[optind + indent]);

   handle = Nvme_Open(&adapterList, vmhba);
   if (!handle) {
      Output("Invalid argument:vmhba not found or vmhba not an NVM Express controller. ");
      return -EINVAL;
   }

   rc = Nvme_FormatNvm(handle, s, l, p, m, f, nsid);
   if (rc) {
      Output("Failed to issue Format NVM to namespace %d, 0x%x.", nsid, rc);
   }

   Nvme_Close(handle);

   return rc;
}

/*
 * Construct_NvmeErr1 is to test cmd value on interface Nvme_Ioctl(handle,cmd,uio)
 * Issue various int value(-1-11) to the Nvme_Ioctl(handle,cmd,uio)
  * */
static int
Construct_NvmeErr1(struct nvme_handle *handle)
{
   struct usr_io uio;
   struct firmware_slot_log fwSlotLog;
   int i, rc = 0;

   Output("Using GetFeature as tested nvme admin cmd");
   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_FEATURES;
   uio.direction = XFER_FROM_DEV;
   uio.cmd.cmd.getFeatures.featureID = FTR_ID_ARBITRATION;  //arbitrtion
   /*with i=-1/0/11, hit PSOD1232371*/
   for(i = -1; i <= 11; i++){
       if(i == 9 || i == 10)
	   /* skip 9/10,for PR 1246782  */
           continue;
       Output("consturct new uio and issue cmd value as %d ",i);
       rc = Nvme_AdminPassthru_error(handle,i,&uio);
   }

   Output("Using GetLog as tested nvme admin cmd");
   /*with i=-1/0/11, hit PSOD1232371*/
   for(i = -1; i <= 11; i++){
       if(i == 9 || i == 10)
	   /* skip 9/10,for PR 1246782  */
           continue;
       Output("consturct new uio and issue cmd value as %d ",i);
       memset(&uio, 0, sizeof(uio));
       uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_LOG_PAGE;
       uio.cmd.header.namespaceID = -1;
       uio.direction = XFER_FROM_DEV;
       uio.timeoutUs = ADMIN_TIMEOUT;
       uio.cmd.cmd.getLogPage.LogPageID = 3; //firmware_slot_info
       uio.cmd.cmd.getLogPage.numDW = GLP_LEN_FIRMWARE_SLOT_INFO / 4 - 1;
       uio.length = GLP_LEN_FIRMWARE_SLOT_INFO;
       memset(&fwSlotLog,0,sizeof(fwSlotLog));
       uio.addr = (vmk_uintptr_t)&fwSlotLog;

       rc = Nvme_AdminPassthru_error(handle,i,&uio);
       //PrintFwSlotLog(&fwSlotLog);
       /*no need to check the rc and value, details refer to PR1232422
       Output("rc is %s", strerror(rc));
       if (rc == 0){
           value = uio.comp.param.cmdSpecific;
           Output("after issue Nvme_AdminPassthru_error,value = %x\n", value);
           PrintFwSlotLog(&fwSlotLog);
       }
       */
   }
   return rc;
}


/*
 * Construct_NvmeErr2 is to test uio on interface Nvme_Ioctl(handle,cmd,uio)
 * Consturct error on uio structure
  * */
static int
Construct_NvmeErr2( struct nvme_handle *handle)
{
   struct usr_io uio;
   int rc;
   struct firmware_slot_log fwSlotLog;

   /* contruct error log cmd: 1. set wrong namespaceID */
   Output("--------contructing error log cmd with wrong namespaceID");
   memset(&uio, 0, sizeof(uio));
   memset(&fwSlotLog,0,sizeof(fwSlotLog));

   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_LOG_PAGE;
   uio.cmd.header.namespaceID = 1;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getLogPage.LogPageID = 3; //firmware cmd
   uio.cmd.cmd.getLogPage.numDW = GLP_LEN_FIRMWARE_SLOT_INFO / 4 - 1;
   uio.length = GLP_LEN_FIRMWARE_SLOT_INFO;
   uio.addr = (vmk_uintptr_t)&fwSlotLog;

   rc = Nvme_AdminPassthru(handle,&uio);
   //PrintFwSlotLog(&fwSlotLog);

   /* contruct error log cmd: 2. set wrong direction  */
   Output("--------contructing error log cmd with wrong direction");
   memset(&uio, 0, sizeof(uio));
   memset(&fwSlotLog,0,sizeof(fwSlotLog));

   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_LOG_PAGE;
   uio.cmd.header.namespaceID = -1;
   uio.direction = XFER_TO_DEV;// wrong value here
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getLogPage.LogPageID = 3; //firmware cmd
   uio.cmd.cmd.getLogPage.numDW = GLP_LEN_FIRMWARE_SLOT_INFO / 4 - 1;
   uio.length = GLP_LEN_FIRMWARE_SLOT_INFO;
   uio.addr = (vmk_uintptr_t)&fwSlotLog;

   rc = Nvme_AdminPassthru(handle,&uio);
   //PrintFwSlotLog(&fwSlotLog);

   /* contruct error log cmd: 3. set wrong logPageID */
   Output("--------contructing error log cmd with wrong logPageID");
   memset(&uio, 0, sizeof(uio));
   memset(&fwSlotLog,0,sizeof(fwSlotLog));

   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_LOG_PAGE;
   uio.cmd.header.namespaceID = -1;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getLogPage.LogPageID = 0; //wrong value here
   uio.cmd.cmd.getLogPage.numDW = GLP_LEN_FIRMWARE_SLOT_INFO / 4 - 1;
   uio.length = GLP_LEN_FIRMWARE_SLOT_INFO;
   uio.addr = (vmk_uintptr_t)&fwSlotLog;

   rc = Nvme_AdminPassthru(handle,&uio);
   //PrintFwSlotLog(&fwSlotLog);

   /* contruct error log cmd: 4. set wrong numDW */
   Output("---------contructing error log cmd with wrong numDW");
   memset(&uio, 0, sizeof(uio));
   memset(&fwSlotLog,0,sizeof(fwSlotLog));

   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_LOG_PAGE;
   uio.cmd.header.namespaceID = -1;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getLogPage.LogPageID = 3; //firmware cmd
   uio.cmd.cmd.getLogPage.numDW = GLP_LEN_ERR_INFO / 4 - 1;
   uio.length = GLP_LEN_FIRMWARE_SLOT_INFO;
   uio.addr = (vmk_uintptr_t)&fwSlotLog;

   rc = Nvme_AdminPassthru(handle,&uio);
   //PrintFwSlotLog(&fwSlotLog);

   /* contruct error log cmd: 5. set wrong length */
   Output("--------contructing error log cmd with wrong length");
   memset(&uio, 0, sizeof(uio));
   memset(&fwSlotLog,0,sizeof(fwSlotLog));

   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_LOG_PAGE;
   uio.cmd.header.namespaceID = -1;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getLogPage.LogPageID = 3; //firmware cmd
   uio.cmd.cmd.getLogPage.numDW = GLP_LEN_FIRMWARE_SLOT_INFO / 4 - 1;
   uio.length = GLP_LEN_ERR_INFO;
   uio.addr = (vmk_uintptr_t)&fwSlotLog;

   rc = Nvme_AdminPassthru(handle,&uio);
   //PrintFwSlotLog(&fwSlotLog);

   /* contruct error log cmd: 6. set wrong addr */
   Output("--------contructing error log cmd with wrong addr");
   memset(&uio, 0, sizeof(uio));
   memset(&fwSlotLog,0,sizeof(fwSlotLog));

   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_LOG_PAGE;
   uio.cmd.header.namespaceID = -1;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = ADMIN_TIMEOUT;
   uio.cmd.cmd.getLogPage.LogPageID = 3; //firmware cmd
   uio.cmd.cmd.getLogPage.numDW = GLP_LEN_FIRMWARE_SLOT_INFO / 4 - 1;
   uio.length = GLP_LEN_FIRMWARE_SLOT_INFO;
   uio.addr = 0;

   rc = Nvme_AdminPassthru(handle,&uio);
   //PrintFwSlotLog(&fwSlotLog);

   /* contruct error log cmd: 7. set wrong addr and 1 timeout */
   Output("--------contructing error log cmd with wrong addr, small timeout");
   memset(&uio, 0, sizeof(uio));
   memset(&fwSlotLog,0,sizeof(fwSlotLog));

   uio.cmd.header.opCode = NVM_ADMIN_CMD_GET_LOG_PAGE;
   uio.cmd.header.namespaceID = -1;
   uio.direction = XFER_FROM_DEV;
   uio.timeoutUs = 1;
   uio.cmd.cmd.getLogPage.LogPageID = 3; //firmware cmd
   uio.cmd.cmd.getLogPage.numDW = GLP_LEN_FIRMWARE_SLOT_INFO / 4 - 1;
   uio.length = GLP_LEN_FIRMWARE_SLOT_INFO;
   uio.addr = 0;

   rc = Nvme_AdminPassthru(handle,&uio);
   //PrintFwSlotLog(&fwSlotLog);
   return rc;
}


static int
ErrorNvmCli_Dispatch(struct cli_context *cli, int argc, char *argv[])
{
   struct nvme_handle *handle;
   const char *vmhba;
   char *sort;

   vmhba = argv[CLI_ARG_2];

   handle = Nvme_Open(&adapterList, vmhba);
   if (!handle) {
      Output("Invalid argument: vmhba not found or vmhba not an NVM Express controller.");
      return -EINVAL;
   }

   sort= argv[CLI_ARG_2 + 1];
   if (strncmp(sort, "err1", VMK_MISC_NAME_MAX) == 0){
	Construct_NvmeErr1(handle);
   }
   else if (strncmp(sort, "err2", VMK_MISC_NAME_MAX) == 0){
	Construct_NvmeErr2(handle);
   }
   else{
       Output("NOT SUPPORTED\n");
   }

   Nvme_Close(handle);
   return 0;
}

/*****************************************************************************
 * Cli Public/Shared Ops
 ****************************************************************************/


int
Cli_ValidateArgs(struct cli_context *cli, int argc, char *argv[])
{
   int rc = 0;
   struct cli_context *n;
   vmk_ListLinks *itemPtr;
   const char *key;

   DEBUG("cli %s level %d argc %d", cli->name, cli->level, argc);

   /**
    * Check for insufficient number of arguments
    */
   if (argc <= cli->level) {
      return -EINVAL;
   }

   /**
    * Check for available sub-commands
    */
   rc = -EINVAL;
   key = argv[cli->level];
   VMK_LIST_FORALL(&cli->head, itemPtr) {
      n = VMK_LIST_ENTRY(itemPtr, struct cli_context, list);
      if (strncmp(key, n->name, VMK_MISC_NAME_MAX) == 0) {
         rc = 0;
         break;
      }
   }

   return rc;
}


int
Cli_ValidateArgsLeafNoArg(struct cli_context *cli, int argc, char *argv[])
{
   DEBUG("cli %s level %d argc %d", cli->name, cli->level, argc);

   /**
    * Leaf node, no additional arguments acceptable.
    */
   if (argc > cli->level) {
      return -1;
   }

   return 0;
}


void Cli_Usage(struct cli_context *cli)
{
   printf("%s\n", cli->usageStr);
}


struct cli_context *
Cli_LookupCli(struct cli_context *cli, const char *key)
{
   vmk_ListLinks *itemPtr;
   struct cli_context *n;

   VMK_LIST_FORALL(&cli->head, itemPtr)
   {
      n = VMK_LIST_ENTRY(itemPtr, struct cli_context, list);
      if (strncmp(key, n->name, VMK_MISC_NAME_MAX) == 0) {
         return n;
      }
   }

   return NULL;
}


struct cli_context *
Cli_LookupCliLeaf(struct cli_context *cli, const char *key)
{
   return NULL;
}


int
Cli_Dispatch(struct cli_context *cli, int argc, char *argv[])
{
   const char *key;
   struct cli_context *subCli;
   int rc;

   DEBUG("cli %s", cli->name);

   key = argv[cli->level];
   subCli = cli->lookupCli(cli, key);
   if (subCli) {
      if (subCli->validateArgs(subCli, argc, argv)) {
         subCli->usage(subCli);
         return -EINVAL;
      }

      rc = subCli->dispatch(subCli, argc, argv);
   } else {
      DEBUG("no key: cli %s key %s.", cli->name, key);
      rc = -EINVAL;
   }

   return rc;
}


/*****************************************************************************
 * Cli Definitions
 ****************************************************************************/


#if NVME_DEBUG_INJECT_ERRORS
#define cliUsage   "Usage: nvmecli {namespace} {cmd} [cmd options]\n"   \
   "\n"                                                                 \
   "   Available Namespaces:\n"                                         \
   "      driver      NVM Express driver related operations.\n"         \
   "      device      NVM Express device related operations.\n"         \
   "      errinject   inject error on device"                           \
   "\n"
#else
#define cliUsage   "Usage: nvmecli {namespace} {cmd} [cmd options]\n"   \
   "\n"                                                                 \
   "   Available Namespaces:\n"                                         \
   "      driver      NVM Express driver related operations.\n"         \
   "      device      NVM Express device related operations.\n"         \
   "\n"
#endif

#define driverCliUsage  "Usage: nvmecli driver {cmd} [cmd options]\n"   \
   "\n"                                                                 \
   "   Available Commands:\n"                                           \
   "      list        List NVM Express driver information.\n"           \
   "\n"


#define driverListCliUsage   "Usage: nvmecli driver list\n"             \
   "\n"                                                                 \
   "   List NVM Express driver information.\n"                          \
   "\n"


#define deviceCliUsage  "Usage: nvmecli device {cmd} [cmd options]\n"   \
   "\n"                                                                 \
   "   Available Commands:\n"                                           \
   "      list        List NVM Express devices.\n"                      \
   "      info        Show NVM Express device information.\n"           \
   "      regs        Dump NVM Express controller registers.\n"         \
   "      online      Make all namespaces on a controller online.\n"    \
   "      offline     Make all namespaces on a controller offline.\n"   \
   "      feature     Set/Get controller features. \n"                  \
   "      ns          Create/Delete namespaces on a controller.\n"      \
   "      log         List NVM Express log information.\n"              \
   "      firmware    Download or activate firmware.\n"                 \
   "      format      Format NVM.\n"                                    \
   "      error       Contruct error NVM.\n"                            \
   "\n"


#define deviceListCliUsage   "Usage: nvmecli device list\n"             \
   "\n"                                                                 \
   "   List NVM Express devices.\n"                                     \
   "\n"

#define deviceInfoCliUsage   "Usage: nvmecli device info [cmd options]\n"  \
   "\n"                                                                    \
   "   List NVM Express controller information.\n"                         \
   "\n"                                                                    \
   "   Options:\n"                                                         \
   "      -A <vmhba>      vmhba to inspect\n"                              \
   "\n"

#define deviceRegsCliUsage   "Usage: nvmecli device regs [cmd options]\n"  \
   "\n"                                                                    \
   "   Dump NVM Express controller registers.\n"                           \
   "\n"                                                                    \
   "   Options:\n"                                                         \
   "      -A <vmhba>      vmhba to inspect\n"                              \
   "\n"

#define deviceOnlineUsage    "Usage: nvmecli device online [cmd options]\n" \
   "\n"                                                                     \
   "   Make all namespaces on a controller online.\n"                       \
   "\n"                                                                     \
   "   Options:\n"                                                          \
   "\n"                                                                     \
   "      -A <vmhba>      vmhba to operate on\n"                            \

#define deviceFeatureCliUsage   "Usage: nvme-cli device feature [cmd options]\n" \
   "\n"                                                                    \
   "   Set/Get NVM Express device feature information.\n"                         \
   "\n"                                                                    \
   "   Options:\n"                                                         \
   "      -A <vmhba>  get <feature>   get feature of vmhba\n"                \
   "      -A <vmhba>  set <feature> <value>   set feature of vmhba to value\n" \
   "   Feature Type:\n"                                                         \
   "      arbitration\n"	\
   "      pwr_management\n"	\
   "      lba_range_type\n"	\
   "      temp_threshold\n"	\
   "      err_recovery\n"	\
   "      write_cache\n"	\
   "      num_queue\n"		\
   "      int_coalescing\n"	\
   "      int_vector_config\n"	\
   "      write_atomicity\n"	\
   "      asyn_event_config\n"	\
   "      sw_progress_marker\n"	\
   "\n"

#define deviceOfflineUsage   "Usage: nvmecli device online [cmd options]\n" \
   "\n"                                                                     \
   "   Make all namespaces on a controller offline.\n"                      \
   "\n"                                                                     \
   "   Options:\n"                                                          \
   "\n"                                                                     \
   "      -A <vmhba>      vmhba to operate on\n"                            \
   "\n"
#define namespaceCliUsage  "Usage: nvme-cli device ns [cmd options]\n"       \
   "\n"                                                                      \
   "   create or delete namespace. Currently only supported on IDT device\n" \
   "\n"                                                                      \
   "   Options:\n"                                                           \
   "      create <ns, snu, nnu>      create namespace on vmhba\n"            \
   "      delete <ns>                delete namespace\n"                     \
   "\n"


#define deviceLogCliUsage   "Usage: nvme-cli device log [cmd options]\n"     \
   "\n"                                                                      \
   "   List NVM Express log information.\n"                                  \
   "\n"                                                                      \
   "   Options:\n"                                                           \
   "      -A <vmhba> <err_info|smart_health|firmware_slot_info> [ns id]   get log of vmhba\n"                                                                                  \
   "\n"

#define firmwareCliUsage   "Usage: nvme-cli device firmware <download|activate>\n" \
   "\n"                                                                      \
   "   Download or activate firmware.\n"                                     \
   "\n"                                                                      \
   "   Options:\n"                                                           \
   "       <download|activate>   download or activate firmware\n"            \
   "\n"
#define FWDownloadCliUsage   "Usage: nvme-cli device firmware download -A <vmhba> -s <slot> -f <firmware file>\n" \
   "\n"                                                                      \
   "   Download firmware to a specified slot.\n"                             \
   "\n"                                                                      \
   "   Options:\n"                                                           \
   "      -A <vmhba>      vmhba to operate on\n"                             \
   "      -s <slot>  firmware slot number.\n"                                \
   "      -f <firmware file>  firmware file path.\n"                         \
   "\n"

#define FWActivateCliUsage   "Usage: nvme-cli device firmware activate -A <vmhba> -s <slot>\n" \
   "\n"                                                                      \
   "   select firmware from specific slot as activate one.\n"                \
   "\n"                                                                      \
   "   Options:\n"                                                           \
   "      -A <vmhba>      vmhba to operate on\n"                             \
   "      -s <slot>  firmware slot number.\n"                                \
   "\n"

#define formatNvmCliUsage   "Usage: nvme-cli device format [cmd options]\n"                                 \
   "\n"                                                                                                     \
   "   Format NVM.\n"                                                                                       \
   "\n"                                                                                                     \
   "   Options:\n"                                                                                          \
   "      -A <vmhba> -s <0|1|2> -l <0|1> -p <0|1|2|3> -m <0|1> -f <integer> <ns id>\n"                      \
   "\n"                                                                                                     \
   "      -A <vmhba>       vmhba to operate on.\n"                                                          \
   "      -s <0|1|2>       Secure Erase Settings (SES).\n"                                                  \
   "                       0: No secure erase operation requested.\n"                                       \
   "                       1: User Data Erase.\n"                                                           \
   "                       2: Cryptographic Erase.\n"                                                       \
   "      -l <0|1>         Protection information Location (PIL).\n"                                        \
   "                       0: PI is transferred as the last eight bytes of metadata, if PI is enabled.\n"   \
   "                       1: PI is transferred as the first eight bytes of metadata, if PI is enabled.\n"  \
   "      -p <0|1|2|3>     Protection Information (PI).\n"                                                  \
   "                       0: Protection information is not enabled.\n"                                     \
   "                       1: Protection information is enabled, Type 1.\n"                                 \
   "                       2: Protection information is enabled, Type 2.\n"                                 \
   "                       3: Protection information is enabled, Type 3.\n"                                 \
   "      -m <0|1>         Metadata Settings (MS).\n"                                                       \
   "                       0: Metadata is transferred as part of a separate buffer.\n"                      \
   "                       1: Metadata is transferred as part of an extended data LBA.\n"                   \
   "      -f <integer>     LBA Format (LBAF).\n"                                                            \
   "      <ns id>          Namespace ID.\n"                                                                 \
   "\n"

#define ErrorNvmCliUsage   "Usage: nvmecli device error  -A <vmhba> <err1|err2>"			\
   "\n"													\
   "   Construct error nvme cmd to test.\n"								\
   "\n"													\
   "   Options:\n"											\
   "      -A <vmhba>      vmhba to inspect\n"								\
   "      err1		  which is to test cmd value on interface Nvme_Ioctl(handle,cmd,uio)\n"		\
   "      err2		  which is to test uio on interface Nvme_Ioctl(handler,cmd,uio)\n"		\
   "\n"

#define ErrInjectCliUsage   "Usage: nvmecli errinject  -A <vmhba> <globalEnable> <errType> <likelyhood> <count>" \
   "\n"													\
   "   Enable/Disable error injection on driver \n"								\
   "\n"													\
   "   Options:\n"											\
   "      1.  To enable it per controller \n" \
   "              -A <vmhba>      vmhba to inspect\n"								\
   "              errType         Type of the error injection to enable. \n"     \
   "              likelyhood	  Likelyhood value for the error injection. \n"		\
   "              count           Number of instances of errType to inject\n"		\
   "      2.  To enable it globally for all avaiable controller\n    " \
   "              -G              Global enable\n"								\
   "              errType         Type of the error injection to enable.\n"     \
   "              likelyhood	  Likelyhood value for the error injection.\n"		\
   "              count           Number of instances of errType to inject\n"		\
   "\n"

static struct cli_context globalCli = {
   .name          = "nvme",
   .parent        = NULL,
   .usageStr      = cliUsage,
   .level         = 1,
   .validateArgs  = Cli_ValidateArgs,
   .usage         = Cli_Usage,
   .lookupCli     = Cli_LookupCli,
   .dispatch      = Cli_Dispatch,
};
#if NVME_DEBUG_INJECT_ERRORS
static struct cli_context errInjectCli = {
   .name          = "errInject",
   .parent        = &globalCli,
   .usageStr      = ErrInjectCliUsage,
   .level         = 2,
   .validateArgs  = errInjectCli_ValidateArgs,
   .usage         = Cli_Usage,
   .lookupCli     = Cli_LookupCli,
   .dispatch      = errInjectCli_Dispatch,
};
#endif
static struct cli_context driverCli = {
   .name          = "driver",
   .parent        = &globalCli,
   .usageStr      = driverCliUsage,
   .level         = 2,
   .validateArgs  = Cli_ValidateArgs,
   .usage         = Cli_Usage,
   .lookupCli     = Cli_LookupCli,
   .dispatch      = Cli_Dispatch,
};


static struct cli_context deviceCli = {
   .name          = "device",
   .parent        = &globalCli,
   .usageStr      = deviceCliUsage,
   .level         = 2,
   .validateArgs  = Cli_ValidateArgs,
   .usage         = Cli_Usage,
   .lookupCli     = Cli_LookupCli,
   .dispatch      = Cli_Dispatch,
};


static struct cli_context driverListCli = {
   .name          = "list",
   .parent        = &driverCli,
   .usageStr      = driverListCliUsage,
   .level         = 3,
   .validateArgs  = Cli_ValidateArgsLeafNoArg,
   .usage         = Cli_Usage,
   .lookupCli     = Cli_LookupCliLeaf,
   .dispatch      = DriverListCli_Dispatch,
};


static struct cli_context deviceListCli = {
   .name          = "list",
   .parent        = &deviceCli,
   .usageStr      = deviceListCliUsage,
   .level         = 3,
   .validateArgs  = Cli_ValidateArgsLeafNoArg,
   .usage         = Cli_Usage,
   .lookupCli     = Cli_LookupCliLeaf,
   .dispatch      = DeviceListCli_Dispatch,
};


static struct cli_context deviceInfoCli = {
   .name          = "info",
   .parent        = &deviceCli,
   .usageStr      = deviceInfoCliUsage,
   .level         = 3,
   .validateArgs  = DeviceInfoCli_ValidateArgs,
   .usage         = Cli_Usage,
   .lookupCli     = Cli_LookupCliLeaf,
   .dispatch      = DeviceInfoCli_Dispatch,
};


static struct cli_context deviceRegsCli = {
   .name          = "regs",
   .parent        = &deviceCli,
   .usageStr      = deviceRegsCliUsage,
   .level         = 3,
   .validateArgs  = DeviceInfoCli_ValidateArgs,
   .usage         = Cli_Usage,
   .lookupCli     = Cli_LookupCliLeaf,
   .dispatch      = DeviceRegsCli_Dispatch,
};

static struct cli_context deviceFeatureCli = {
   .name          = "feature",
   .parent        = &deviceCli,
   .usageStr      = deviceFeatureCliUsage,
   .level         = 3,
   .validateArgs  = DeviceFeatureCli_ValidateArgs,
   .usage         = Cli_Usage,
   .lookupCli     = Cli_LookupCliLeaf,
   .dispatch      = DeviceFeatureCli_Dispatch,
};
static struct cli_context namespaceCli = {
   .name          = "ns",
   .parent        = &deviceCli,
   .usageStr      = namespaceCliUsage,
   .level         = 3,
   .validateArgs  = NamespaceCli_ValidateArgs,
   .usage         = Cli_Usage,
   .lookupCli     = Cli_LookupCliLeaf,
   .dispatch      = NamespaceCli_Dispatch,
};

static struct cli_context deviceOnlineCli = {
   .name          = "online",
   .parent        = &deviceCli,
   .usageStr      = deviceOnlineUsage,
   .level         = 3,
   .validateArgs  = DeviceInfoCli_ValidateArgs,
   .usage         = Cli_Usage,
   .lookupCli     = Cli_LookupCliLeaf,
   .dispatch      = DeviceOnlineCli_Dispatch,
};

static struct cli_context deviceOfflineCli = {
   .name          = "offline",
   .parent        = &deviceCli,
   .usageStr      = deviceOfflineUsage,
   .level         = 3,
   .validateArgs  = DeviceInfoCli_ValidateArgs,
   .usage         = Cli_Usage,
   .lookupCli     = Cli_LookupCliLeaf,
   .dispatch      = DeviceOfflineCli_Dispatch,
};


static struct cli_context deviceLogCli = {
   .name          = "log",
   .parent        = &deviceCli,
   .usageStr      = deviceLogCliUsage,
   .level         = 3,
   .validateArgs  = DeviceLogCli_ValidateArgs,
   .usage         = Cli_Usage,
   .lookupCli     = Cli_LookupCliLeaf,
   .dispatch      = DeviceLogCli_Dispatch,
};

static struct cli_context firmwareCli = {
   .name          = "firmware",
   .parent        = &deviceCli,
   .usageStr      = firmwareCliUsage,
   .level         = 3,
   .validateArgs  = Cli_ValidateArgs,
   .usage         = Cli_Usage,
   .lookupCli     = Cli_LookupCli,
   .dispatch      = Cli_Dispatch,
};

static struct cli_context FWDownloadCli= {
   .name          = "download",
   .parent        = &firmwareCli,
   .usageStr      = FWDownloadCliUsage,
   .level         = 4,
   .validateArgs  = FWDownloadCli_ValidateArgs,
   .usage         = Cli_Usage,
   .lookupCli     = Cli_LookupCliLeaf,
   .dispatch      = FWDownloadCli_Dispatch,
};

static struct cli_context FWActivateCli= {
   .name          = "activate",
   .parent        = &firmwareCli,
   .usageStr      = FWActivateCliUsage,
   .level         = 4,
   .validateArgs  = FWActivateCli_ValidateArgs,
   .usage         = Cli_Usage,
   .lookupCli     = Cli_LookupCliLeaf,
   .dispatch      = FWActivateCli_Dispatch,
};

static struct cli_context formatNvmCli = {
   .name          = "format",
   .parent        = &deviceCli,
   .usageStr      = formatNvmCliUsage,
   .level         = 3,
   .validateArgs  = FormatNvmClid_ValidateArgs,
   .usage         = Cli_Usage,
   .lookupCli     = Cli_LookupCliLeaf,
   .dispatch      = FormatNvmCli_Dispatch,
};

static struct cli_context ErrorNvmCli = {
   .name          = "error",
   .parent        = &deviceCli,
   .usageStr      = ErrorNvmCliUsage,
   .level         = 3,
   .validateArgs  = ErrorNvmCli_ValidateArgs,
   .usage         = Cli_Usage,
   .lookupCli     = Cli_LookupCliLeaf,
   .dispatch      = ErrorNvmCli_Dispatch,
};


/*****************************************************************************
 * Utilities and General-Purpose Ops
 ****************************************************************************/


/**
 * Register a Cli handler.
 *
 * The cli handler must have its parent set properly, so that the cli can be
 * properly handled when we traverse the cli tree.
 *
 * @param [in] cli Cli handler to be registered.
 */
static void
Cli_RegisterCli(struct cli_context *cli)
{
   struct cli_context *parent = cli->parent;

   /**
    * If parent if not initialized
    */
   if (cli->parent->head.prevPtr == NULL ||
      cli->parent->head.nextPtr == NULL) {
      vmk_ListInit(&cli->parent->head);
   }

   vmk_ListInsert(&cli->list, vmk_ListAtRear(&parent->head));
}


/**
 * Initialize the Cli environment, called once during global init.
 *
 * @param [in] cli root of the Cli handler tree
 */
static int
Cli_Init(struct cli_context *cli)
{
   assert(cli != NULL);

   /**
    * Register namespaces
    */
   Cli_RegisterCli(&driverCli);
   Cli_RegisterCli(&deviceCli);
#if NVME_DEBUG_INJECT_ERRORS
   Cli_RegisterCli(&errInjectCli);
#endif
   Cli_RegisterCli(&driverListCli);
   Cli_RegisterCli(&deviceListCli);
   Cli_RegisterCli(&deviceInfoCli);
   Cli_RegisterCli(&deviceRegsCli);
   Cli_RegisterCli(&deviceOnlineCli);
   Cli_RegisterCli(&deviceOfflineCli);
   Cli_RegisterCli(&deviceFeatureCli);
   Cli_RegisterCli(&deviceLogCli);
   Cli_RegisterCli(&namespaceCli);
   Cli_RegisterCli(&firmwareCli);
   Cli_RegisterCli(&FWDownloadCli);
   Cli_RegisterCli(&FWActivateCli);
   Cli_RegisterCli(&formatNvmCli);
   Cli_RegisterCli(&ErrorNvmCli);

   return 0;
}


/**
 * Clean up operations.
 *
 * Called once before exit.
 *
 * @param [in] cli root of the Cli handler tree
 */
static void
Cli_Cleanup(struct cli_context *cli)
{

}


int main(int argc, char *argv[])
{
   int rc;

   /**
    * Initialize NVMe management layer first
    */
   rc = Nvme_GetAdapterList(&adapterList);
   if (rc) {
      Output("Failed to initialize NVMe.");
      return rc;
   }

   rc = Cli_Init(&globalCli);
   if (rc) {
      return rc;
   }

   rc = Cli_ValidateArgs(&globalCli, argc, argv);
   if (rc) {
      Cli_Usage(&globalCli);
      Cli_Cleanup(&globalCli);
      return -EINVAL;
   }

   rc = Cli_Dispatch(&globalCli, argc, argv);

   Cli_Cleanup(&globalCli);

   return rc;
}
