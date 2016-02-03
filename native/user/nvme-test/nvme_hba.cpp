/****************************************************************************
 * Copyright (c) 2014-2015 VMware, Inc. All rights reserved
 ***************************************************************************/

/**
 * @file nvme_hba.cpp
 */

#include <string.h>

#include "nvme_hba.h"


NvmeHba::NvmeHba()
   : nvmeHandle(NULL)
{
}

NvmeHba::NvmeHba(const std::string &_vmhba)
   : vmhba(_vmhba)
   , nvmeHandle(NULL)
{
}


NvmeHba::~NvmeHba()
{
   if (nvmeHandle != NULL) {
      Nvme_Close(nvmeHandle);
      nvmeHandle = NULL;
   }
}


int
NvmeHba::Open()
{
   int      rc;
   struct   nvme_adapter_list adapters;

   if (vmhba == "") {
      return -1;
   }

   if (nvmeHandle != NULL) {
      return 0;
   }

   rc = Nvme_GetAdapterList(&adapters);
   if (rc != 0) {
      return rc;
   }

   nvmeHandle = Nvme_Open(&adapters, vmhba.c_str());
   if (nvmeHandle == NULL) {
      return -1;
   }

   return 0;
}


int
NvmeHba::AdminPassthru(struct usr_io *uio)
{
   int rc;

   if (nvmeHandle == NULL) {
      rc = Open();
      if (rc != 0) {
         return rc;
      }
   }

   return Nvme_AdminPassthru(nvmeHandle, uio);
}


int
NvmeHba::Identify(int namespaceID, uint8_t *resp, size_t respLen)
{
   int            rc;
   struct usr_io  uio;

   memset(&uio, 0, sizeof(uio));
   uio.cmd.header.opCode                     = NVM_ADMIN_CMD_IDENTIFY;
   if (namespaceID < 0) {
      uio.cmd.cmd.identify.controllerStructure  = IDENTIFY_CONTROLLER;
   } else {
      uio.cmd.cmd.identify.controllerStructure  = IDENTIFY_NAMESPACE;
      uio.cmd.header.namespaceID                = namespaceID;
   }

   uio.namespaceID   = 0xff;
   uio.direction     = XFER_FROM_DEV;
   uio.timeoutUs     = ADMIN_TIMEOUT;
   uio.length        = respLen;
   uio.addr          = (uint32_t) resp;

   return AdminPassthru(&uio);
}
