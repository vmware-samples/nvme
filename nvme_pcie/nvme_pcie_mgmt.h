/*****************************************************************************
 * Copyright (c) 2022-2023 VMware, Inc. All rights reserved.
 *****************************************************************************/

/**
 * @file: nvme_pcie_mgmt.h --
 *
 *   vmkapi_mgmt implementation of native nvme_pcie driver
 */

#ifndef _NVME_PCIE_MGMT_H_
#define _NVME_PCIE_MGMT_H_

/*
 * XXX Specify the code modules that are allowed to use this interface.
 * XXX Possible values are
 * XXX    INCLUDE_ALLOW_DISTRIBUTE
 * XXX    INCLUDE_ALLOW_MODULE
 * XXX    INCLUDE_ALLOW_USERLEVEL
 * XXX    INCLUDE_ALLOW_VMCORE
 * XXX    INCLUDE_ALLOW_VMKERNEL
 * XXX    INCLUDE_ALLOW_VMK_MODULE
 * XXX    INCLUDE_ALLOW_VMIROM
 * XXX    INCLUDE_ALLOW_VMMON
 * XXX    INCLUDE_ALLOW_VMX
 */
#define INCLUDE_ALLOW_VMK_MODULE
//#include "includeCheck.h"

#include "nvme_pcie_int.h"

#define NVMEPCIE_KVMGMT_BUF_SIZE   (4096)

typedef struct NVMEPCIEKVMgmtData {
   char *keyName;
   vmk_MgmtKeyType type;
   vmk_MgmtKeyGetFn getFn;
   char *getDesc;
   vmk_MgmtKeySetFn setFn;
   char *setDesc;
} NVMEPCIEKVMgmtData;


VMK_ReturnStatus NVMEPCIEKeyValInit(NVMEPCIEController *ctrlr);
void NVMEPCIEKeyValDestory(NVMEPCIEController *ctrlr);

VMK_ReturnStatus NVMEPCIEGlobalKeyValInit();
void NVMEPCIEGlobalKeyValDestroy();

#endif // ifndef _NVME_PCIE_MGMT_H_
