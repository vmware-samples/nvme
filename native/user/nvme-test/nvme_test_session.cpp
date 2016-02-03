/****************************************************************************
 * Copyright (c) 2014-2015 VMware, Inc. All rights reserved
 ***************************************************************************/

/**
 * @file: nvme_test_session.cpp
 *
 *
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <stdexcept>
#include <stdlib.h>
#include <boost/regex.hpp>
#include "nvme_test_session.h"
#include "utils.h"
#include "nvme_hba.h"

using namespace std;
using namespace boost;

/**
 * Static function to get the list of VMHBAs from the output of `lspci -v`.
 */
static void
getHbas(vector<NvmeHba> &hbas) throw(NvmeTestSessionException)
{
   int rc;
   string output;
   string cmd;

   /**
    * Get list of NVMe SSDs through output of `lspci -v`.
    */
   cmd = "lspci -v";
   rc = ExecuteCommand(cmd, output);
   if (rc != 0) {
      throw(NvmeTestSessionException("failed to execute `lspci -v`."));
   }

   regex r("^(\\S{4}):(\\S{2}):(\\S{2})\\.(\\S{1})[^\n]*?: ([^\n]*?)\\[(\\S+)\\]\n\\s+Class 0108:");
   smatch sm;

   enum {
      MATCH_STR      = 0,
      MATCH_SEGMENT  = 1,
      MATCH_BUS      = 2,
      MATCH_DEVICE   = 3,
      MATCH_FUNCTION = 4,
      MATCH_NAME     = 5,
      MATCH_VMHBA    = 6,
      MATCH_SIZE     = 7,
   };

   while (regex_search(output, sm, r)) {
      if (sm.size() != MATCH_SIZE) {
         ADD_FAILURE() << "Invalid HBA found: " << sm.str();
         output = sm.suffix();
         continue;
      }

      NvmeHba hba(sm[MATCH_VMHBA].str());
      hba.displayName      = sm[MATCH_NAME].str();
      hba.address.segment  = strtoul(sm[MATCH_SEGMENT].str().c_str(), NULL, 16);
      hba.address.bus      = strtoul(sm[MATCH_BUS].str().c_str(), NULL, 16);
      hba.address.device   = strtoul(sm[MATCH_DEVICE].str().c_str(), NULL, 16);
      hba.address.function = strtoul(sm[MATCH_FUNCTION].str().c_str(), NULL, 16);

      hbas.push_back(hba);

      output = sm.suffix();
   }
}


/**
 * Static function to determine if a given namespace (device name) has
 * partitions.
 */
static bool
deviceIsPartitioned(string device) throw(NvmeTestSessionException)
{
   int rc;
   string cmd;
   string output;

   cmd = "ls /dev/disks/" + device + ":* 2>/dev/null";
   rc = ExecuteCommand(cmd, output);

   return !(output.size() == 0);
}


/**
 * Static function to get the namespace ID (deducted from the LUN ID) for a
 * given nvme device.
 */
static int
getNamespaceId(string deviceName) throw (NvmeTestSessionException)
{
   int rc;
   string cmd;
   string output;

   cmd = "esxcfg-mpath -L";
   rc = ExecuteCommand(cmd, output);
   if (rc != 0) {
      throw(NvmeTestSessionException("failed to execute command `esxcfg-mpath -L`."));
   }

   regex r("C0:T0:L(\\d+) \\S+ " + deviceName);
   smatch sm;

   if (!regex_search(output, sm, r)) {
      throw(NvmeTestSessionException("failed to find LUN for device " + deviceName + ", pattern not found."));
   }

   if (sm.size() != 2) {
      throw(NvmeTestSessionException("failed to find LUN for device " + deviceName + ", invalid match size."));
   }

   Log(debug) << "Device: " << deviceName << "LUN: " << sm[1].str();

   rc = strtoul(sm[1].str().c_str(), NULL, 10);

   return rc + 1;
}


/**
 * Static function to get list of namespaces for a given hba (identified by
 * vmhba name) through /dev/disks/*.
 */
static void
getNamespaces(string vmhba, vector<NvmeNamespace> &namespaces) throw(NvmeTestSessionException)
{
   int rc;
   string cmd;
   string output;

   cmd = "esxcfg-scsidevs -A";
   rc = ExecuteCommand(cmd, output);
   if (rc != 0) {
      throw(NvmeTestSessionException("failed to execute `esxcfg-scsidevs -A`."));
   }

   regex r(vmhba + "\\s+(\\S+)");
   smatch sm;

   enum {
      MATCH_STR  = 0,
      MATCH_NAME = 1,
      MATCH_SIZE = 2,
   };

   while (regex_search(output, sm, r)) {

      if (sm.size() != MATCH_SIZE) {
         ADD_FAILURE() << "Invalid namespace device name found: " << sm.str();
         output = sm.suffix();
         continue;
      }

      NvmeNamespace ns;
      ns.deviceName     = sm[MATCH_NAME].str();
      ns.namespaceID    = getNamespaceId(ns.deviceName);
      ns.isPartitioned  = deviceIsPartitioned(ns.deviceName);
      ns.isMounted      = false; /** Cannot get datastore mount for now */
      ns.datastoreName  = "unknown"; /** Cannot get datastore fow now */

      namespaces.push_back(ns);

      output = sm.suffix();
   }
}


NvmeTestSessionException::NvmeTestSessionException()
   : runtime_error("unknown error.")
{
}


NvmeTestSessionException::NvmeTestSessionException(const std::string &what_arg)
   : runtime_error(what_arg)
{
}


NvmeTestSessionException::NvmeTestSessionException(const char *what_arg)
   : runtime_error(what_arg)
{
}


NvmeTestSession::NvmeTestSession()
{
   InitializeNvmeHbas();
}


NvmeTestSession::~NvmeTestSession()
{
}


void
NvmeTestSession::SetUp()
{
}


void
NvmeTestSession::TearDown()
{
}


void
NvmeTestSession::InitializeNvmeHbas()
{
   getHbas(this->hbas);
   for (vector<NvmeHba>::iterator itor = this->hbas.begin(); itor != hbas.end(); ++itor) {
      getNamespaces(itor->vmhba, itor->namespaces);
   }
}


NvmeHba&
NvmeTestSession::GetTestHba() throw(NvmeTestSessionException)
{
   for (vector<NvmeHba>::iterator itor = hbas.begin(); itor != hbas.end(); ++itor) {
      Log(debug) << itor->vmhba;
      Log(debug) << itor->displayName;
      Log(debug) << setfill('0') << setw(4) << (int)itor->address.segment
         << ":"
         << setw(2) << (int)itor->address.bus
         << ":"
         << setw(2) << (int)itor->address.device
         << "."
         << (int)itor->address.function;
      for (vector<NvmeNamespace>::iterator nsItor = itor->namespaces.begin();
         nsItor != itor->namespaces.end(); ++nsItor) {
         Log(debug) << "\t"
            << nsItor->namespaceID << ": "
            << nsItor->deviceName
            << " ("
            << string(nsItor->isPartitioned ? "Partitioned" : "Not-Partitioned")
            << ")";
         /**
          * Return the first HBA that has Not-Partitioned namespace.
          */
         if (nsItor->isPartitioned == false) {
            return *itor;
         }
      }
   }

   /**
    * No available HBA found, should raise an error.
    */
   throw(NvmeTestSessionException("test hba not available."));
}
