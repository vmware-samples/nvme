/*********************************************************************************
 * Copyright 2013 VMware, Inc. All rights reserved.
 * ******************************************************************************/

/**
*******************************************************************************
** Copyright (c) 2012-2013  Integrated Device Technology, Inc.               **
**                                                                           **
** All rights reserved.                                                      **
**                                                                           **
*******************************************************************************
**                                                                           **
** Redistribution and use in source and binary forms, with or without        **
** modification, are permitted provided that the following conditions are    **
** met:                                                                      **
**                                                                           **
**   1. Redistributions of source code must retain the above copyright       **
**      notice, this list of conditions and the following disclaimer.        **
**                                                                           **
**   2. Redistributions in binary form must reproduce the above copyright    **
**      notice, this list of conditions and the following disclaimer in the  **
**      documentation and/or other materials provided with the distribution. **
**                                                                           **
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS   **
** IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, **
** THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR    **
** PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR         **
** CONTRIBUTORS BE LIABLE FOR ANY DIRECT,INDIRECT, INCIDENTAL, SPECIAL,      **
** EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,       **
** PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR        **
** PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF    **
** LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING      **
** NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS        **
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.              **
**                                                                           **
** The views and conclusions contained in the software and documentation     **
** are those of the authors and should not be interpreted as representing    **
** official policies, either expressed or implied,                           **
** Integrated Device Technology Inc.                                         **
**                                                                           **
*******************************************************************************
**/

/*
 * @file: nvme_driver.c --
 *
 *    Driver interface of native nvme driver
 */

#include "nvme_private.h"


/**
 * attachDevice callback of driver ops
 */
static VMK_ReturnStatus
AttachDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeCtrlr *ctrlr;

   Nvme_LogDebug("enter.");

#if NVME_DEBUG_INJECT_STATE_DELAYS
   Nvme_LogInfo("--ATTACH STARTED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   ctrlr = Nvme_Alloc(sizeof *ctrlr,
      VMK_L1_CACHELINE_SIZE,
      NVME_ALLOC_ZEROED);
   if (ctrlr == NULL) {
      return VMK_NO_MEMORY;
   }

   ctrlr->device = device;

   /*
    * Task lists to attach an Nvme device to the driver
    * Some of them are done in nvme_driver and some are done in nvme_ctrlr.
    * nvme_driver should be responsible for any operations that are related to
    *             the OS layer, e.g. PCI bus brought up and bar mapping;
    * nvme_ctrlr  should be reponsible for Nvme controller related stuff, e.g.
    *             reg configurations, admin q, etc.
    *
    * Open questions: where do we put intr stuff?
    *
    * 1. Get PCI device handle
    * 2. PCI bar mapping
    * 3. Interrupt allocation and setup
    * 4. ??
    */

   vmkStatus = NvmeCtrlr_Attach(ctrlr);
   if (vmkStatus != VMK_OK) {
      goto free_ctrlr;
   }

   /* Attach to management instance here */
   vmkStatus = NvmeMgmt_CtrlrInitialize(ctrlr);
   if (vmkStatus != VMK_OK) {
      goto ctrlr_detach;
   }

   /* Attach the controller instance to the device handle */
   vmkStatus = vmk_DeviceSetAttachedDriverData(device, ctrlr);
   if (vmkStatus != VMK_OK) {
      goto mgmt_unreg;
   }

   /** Add this adaper to the global list */
   vmk_SpinlockLock(NVME_DRIVER_RES_LOCK);
   vmk_ListInsert(&ctrlr->list, vmk_ListAtRear(&NVME_DRIVER_RES_ADAPTERLIST));
   vmk_SpinlockUnlock(NVME_DRIVER_RES_LOCK);

   Nvme_LogDebug("attached driver data %p.", ctrlr);

#if NVME_DEBUG_INJECT_STATE_DELAYS
   Nvme_LogInfo("--ATTACH COMPLETED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   return VMK_OK;

mgmt_unreg:
   NvmeMgmt_CtrlrDestroy(ctrlr);

ctrlr_detach:
   NvmeCtrlr_Detach(ctrlr);

free_ctrlr:
   Nvme_Free(ctrlr);
   return vmkStatus;
}


/**
 * removeDevice callback of device ops
 */
static VMK_ReturnStatus
DriverRemoveDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus;
   vmk_ScsiAdapter *adapter;
   struct NvmeCtrlr *ctrlr;

   Nvme_LogDebug("enter");

   vmkStatus = vmk_DeviceGetRegistrationData(device, (vmk_AddrCookie *)&adapter);
   if (vmkStatus != VMK_OK || adapter == NULL) {
      Nvme_LogError("failed to get logical device data, 0x%x.", vmkStatus);
      return VMK_BAD_PARAM;
   }

   ctrlr = (struct NvmeCtrlr *)adapter->clientData;

   vmkStatus = vmk_DeviceUnregister(device);
   Nvme_LogInfo("removed logical device, 0x%x.", vmkStatus);

   vmkStatus = NvmeScsi_Destroy(ctrlr);
   Nvme_LogInfo("cleaned up scsi layer, 0x%x.", vmkStatus);

   ctrlr->logicalDevice = NULL;

   return VMK_OK;
}


/**
 * device ops of the logical device (logical SCSI device)
 */
static vmk_DeviceOps __deviceOps = {
   .removeDevice = DriverRemoveDevice,
};


/**
 * scanDevice callback of driver ops
 */
static VMK_ReturnStatus
ScanDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeCtrlr *ctrlr;
   vmk_DeviceProps deviceProps;
   vmk_DeviceID deviceId;
   vmk_Name busName;
   vmk_BusType busType;

   Nvme_LogDebug("enter.");

#if NVME_DEBUG_INJECT_STATE_DELAYS
   Nvme_LogInfo("--SCAN STARTED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   vmkStatus = vmk_DeviceGetAttachedDriverData(device, (vmk_AddrCookie *)&ctrlr);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("failed to get controller instance, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   vmkStatus = NvmeScsi_Init(ctrlr);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("failed to initialize scsi layer, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   /* Create the logical device */
   vmk_NameInitialize(&busName, VMK_LOGICAL_BUS_NAME);
   vmk_BusTypeFind(&busName, &busType);
   vmkStatus = vmk_LogicalCreateBusAddress(NVME_DRIVER_RES_DRIVER_HANDLE, device, 0,
      &deviceId.busAddress, &deviceId.busAddressLen);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("failed to create logical bus address, 0x%x.", vmkStatus);
      goto out;
   }

   deviceId.busType = busType;
   deviceId.busIdentifier = VMK_SCSI_PSA_DRIVER_BUS_ID;
   deviceId.busIdentifierLen = vmk_Strnlen(deviceId.busIdentifier, VMK_MISC_NAME_MAX);

   deviceProps.registeringDriver = NVME_DRIVER_RES_DRIVER_HANDLE;
   deviceProps.deviceID = &deviceId;
   deviceProps.deviceOps = &__deviceOps;
   deviceProps.registeringDriverData.ptr = ctrlr;
   deviceProps.registrationData.ptr = ctrlr->scsiAdapter;

   vmkStatus = vmk_DeviceRegister(&deviceProps, device, &ctrlr->logicalDevice);
   vmk_LogicalFreeBusAddress(NVME_DRIVER_RES_DRIVER_HANDLE, deviceId.busAddress);
   vmk_BusTypeRelease(deviceId.busType);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("failed to register logical device, 0x%x.", vmkStatus);
      goto out;
   }

#if NVME_DEBUG_INJECT_STATE_DELAYS
   Nvme_LogInfo("--SCAN COMPLETED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   return VMK_OK;

out:
   NvmeScsi_Destroy(ctrlr);
   return vmkStatus;
}


/**
 * detachDevice callback of driver ops
 */
static VMK_ReturnStatus
DetachDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeCtrlr *ctrlr;

   Nvme_LogDebug("enter.");

#if NVME_DEBUG_INJECT_STATE_DELAYS
   Nvme_LogInfo("--DETACH STARTED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   vmkStatus = vmk_DeviceGetAttachedDriverData(device, (vmk_AddrCookie *)&ctrlr);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("failed to get controller instance, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   /** Remote the adapter from the global list */
   vmk_SpinlockLock(NVME_DRIVER_RES_LOCK);
   vmk_ListRemove(&ctrlr->list);
   vmk_SpinlockUnlock(NVME_DRIVER_RES_LOCK);

   /* Destroy the management handle */
   NvmeMgmt_CtrlrDestroy(ctrlr);

   /* Controller should have been quiesced before destruction.
    * Destruction is handled by nvme_ctrlr, which executes opposite operations
    * from NvmeCtrlr_Attach.
    */
   vmkStatus = NvmeCtrlr_Detach(ctrlr);
   Nvme_LogDebug("nvme controller %p destructed, 0x%x.", ctrlr, vmkStatus);

   /* Should never reference ctrlr after detach. */
   Nvme_Free(ctrlr);

#if NVME_DEBUG_INJECT_STATE_DELAYS
   Nvme_LogInfo("--DETACH COMPLETED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   return VMK_OK;
}


/**
 * quiesceDevice callback of driver ops
 */
static VMK_ReturnStatus
QuiesceDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeCtrlr *ctrlr;

   Nvme_LogDebug("enter.");

#if NVME_DEBUG_INJECT_STATE_DELAYS
   Nvme_LogInfo("--QUIESCE STARTED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   vmkStatus = vmk_DeviceGetAttachedDriverData(device, (vmk_AddrCookie *)&ctrlr);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("failed to get controller instance, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   vmkStatus = NvmeCtrlr_Stop(ctrlr);

#if NVME_DEBUG_INJECT_STATE_DELAYS
   Nvme_LogInfo("--QUIESCE COMPLETED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   return vmkStatus;
}


/**
 * startDevice callback of driver ops
 */
static VMK_ReturnStatus
StartDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeCtrlr *ctrlr;

   Nvme_LogDebug("enter.");

#if NVME_DEBUG_INJECT_STATE_DELAYS
   Nvme_LogInfo("--START STARTED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   vmkStatus = vmk_DeviceGetAttachedDriverData(device, (vmk_AddrCookie *)&ctrlr);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("failed to get controller instance, 0x%x.", vmkStatus);
      return vmkStatus;
   }

   vmkStatus = NvmeCtrlr_Start(ctrlr);

#if NVME_DEBUG_INJECT_STATE_DELAYS
   Nvme_LogInfo("--START COMPLETED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   return vmkStatus;
}


/**
 * forgetDevice callback of driver ops
 */
static void
ForgetDevice(vmk_Device device)
{
   VMK_ReturnStatus vmkStatus;
   struct NvmeCtrlr *ctrlr;

   Nvme_LogDebug("enter.");

#if NVME_DEBUG_INJECT_STATE_DELAYS
   Nvme_LogInfo("--FORGET STARTED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

   vmkStatus = vmk_DeviceGetAttachedDriverData(device, (vmk_AddrCookie *)&ctrlr);
   if (vmkStatus != VMK_OK) {
      Nvme_LogError("failed to get controller instance, 0x%x.", vmkStatus);
   }

   NvmeCtrlr_SetMissing(ctrlr);

#if NVME_DEBUG_INJECT_STATE_DELAYS
   Nvme_LogInfo("--FORGET COMPLETED--");
   vmk_WorldSleep(NVME_DEBUG_STATE_DELAY_US);
#endif

}


/**
 * driver ops used to register nvme driver.
 */
static vmk_DriverOps __driverOps = {
   .attachDevice = AttachDevice,
   .scanDevice = ScanDevice,
   .detachDevice = DetachDevice,
   .quiesceDevice = QuiesceDevice,
   .startDevice = StartDevice,
   .forgetDevice = ForgetDevice,
};


/**
 * Register driver
 *
 * This will update the module's global resource data.
 *
 * @return VMK_OK driver registration successful
 * @return VMK_EXISTS driver already registered
 */
VMK_ReturnStatus
NvmeDriver_Register()
{
   VMK_ReturnStatus vmkStatus;
   vmk_DriverProps props;

   Nvme_LogDebug("enter.");

   VMK_ASSERT(NVME_DRIVER_RES_DRIVER_HANDLE == VMK_DRIVER_NONE);
   if (NVME_DRIVER_RES_DRIVER_HANDLE != VMK_DRIVER_NONE) {
      return VMK_EXISTS;
   }

   props.moduleID = vmk_ModuleCurrentID;
   props.ops = &__driverOps; // defined and exported from nvme_driver.c
   props.privateData.ptr = NULL;
   vmk_NameInitialize(&props.name, NVME_DRIVER_PROPS_DRIVER_NAME);

   vmkStatus = vmk_DriverRegister(&props, &(NVME_DRIVER_RES_DRIVER_HANDLE));

   return vmkStatus;
}


/**
 * Unregister driver.
 *
 * This will update the module's global resource data.
 */
void
NvmeDriver_Unregister()
{
   Nvme_LogDebug("enter.");

   VMK_ASSERT(NVME_DRIVER_RES_DRIVER_HANDLE != VMK_DRIVER_NONE);

   vmk_DriverUnregister(NVME_DRIVER_RES_DRIVER_HANDLE);
   NVME_DRIVER_RES_DRIVER_HANDLE = VMK_DRIVER_NONE;
}
