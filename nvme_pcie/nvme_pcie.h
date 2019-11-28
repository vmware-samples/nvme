/*******************************************************************************
 * Copyright (c) 2016  VMware, Inc. All rights reserved.
 *******************************************************************************/

/*
 * nvme_pcie.h --
 *
 *	Exported head file of nvme_pcie driver.
 */

#ifndef _NVME_PCIE_H_
#define _NVME_PCIE_H_

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
#define INCLUDE_ALLOW_DISTRIBUTE
#define INCLUDE_ALLOW_VMK_MODULE
#define INCLUDE_ALLOW_VMKERNEL
//#include "includeCheck.h"

#include <vmkapi.h>

#endif // ifndef _NVME_PCIE_H_
