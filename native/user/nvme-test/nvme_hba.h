/****************************************************************************
 * Copyright (c) 2014-2015 VMware, Inc. All rights reserved
 ***************************************************************************/

/**
 * @file nvme_hba.h
 *
 * Implement helper functions on an Nvme HBA.
 */

#ifndef __NVME_HBA_H__
#define __NVME_HBA_H__

#include <stdint.h>     /* for uint_8_t, etc. */

#include <string>
#include <vector>

extern "C" {            /* for struct nvme_handle and struct usr_io */
   #include <vmkapi.h>
   #include <nvme_lib.h>
}


struct PciAddr {
   uint16_t  segment;
   uint8_t   bus;
   uint8_t   device;
   uint8_t   function;
};


struct NvmeNamespace {
   int         namespaceID;
   std::string deviceName;
   bool        isPartitioned;
   bool        isMounted;
   std::string datastoreName;
};


class NvmeHba {
public:

   NvmeHba();
   NvmeHba(const std::string &_vmhba);
   virtual ~NvmeHba();

   /**
    * Open a management handle to the HBA
    */
   int Open();

   /**
    * Issue NVMe admin passthru commands into the HBA
    */
   int AdminPassthru(struct usr_io *uio);

   /**
    * Issue an identify command into the HBA
    */
   int Identify(int namespaceID, uint8_t *resp, size_t respLen);

public:
   std::string                vmhba;
   std::string                displayName;
   PciAddr                    address;
   std::vector<NvmeNamespace> namespaces;

private:
   struct nvme_handle        *nvmeHandle;
};


#endif /** __NVME_HBA_H__ */
