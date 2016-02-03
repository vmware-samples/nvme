/****************************************************************************
 * Copyright (c) 2014-2015 VMware, Inc. All rights reserved
 ***************************************************************************/

/**
 * @file basic_test.cpp
 */

#include <iostream>
#include <string>
#include <boost/regex.hpp>

#include "basic_test.h"
#include "utils.h"

using namespace std;
using namespace boost;


int
BasicTest::GetNumVmhbas()
{
   int rc;
   string cmd;
   string output;

   cmd = "esxcfg-scsidevs -a";
   rc = ExecuteCommand(cmd, output);
   if (rc != 0) {
      ADD_FAILURE() << "failed to execute command `esxcfg-scsidevs -a`.";
      return rc;
   }

   regex r("(vmhba\\d+)\\s+(nvme)");
   smatch sm;

   rc = 0;
   while (regex_search(output, sm, r)) {
      output = sm.suffix();
      rc ++;
   }

   return rc;
}


int
BasicTest::GetNumDevicesByAdminPassthru()
{
   int      rc;
   struct   nvme_adapter_list list;

   rc = Nvme_GetAdapterList(&list);
   if (rc != 0) {
      ADD_FAILURE() << "failed to get adapter list through nvme-lib.";
      return rc;
   }

   return list.count;
}


/**
 * Validate the number of VMHBAs in the system.
 */
TEST_F(BasicTest, VmbaCount)
{
   int numVmhbas                 = GetNumVmhbas();
   int numDevicesByLspci         = this->hbas.size();

   Log(debug) << "Number of vmhbas: " << numVmhbas;

   /**
    * We expect there are at least 1 NVMe hbas available in the system
    */
   EXPECT_GT(numVmhbas, 0);

   /**
    * As we don't have other nvme drivers, we expect all NVMe devices to be
    * claimed by `nvme` driver, and each nvme device has one vmhba.
    */
   EXPECT_EQ(numVmhbas, numDevicesByLspci);
}


/**
 * Validate the number of VMHBAs found in `lspci` vs. the number of NVMe
 * controllers got from the management interface
 */
TEST_F(BasicTest, VmhbaCountByAdminPassthru)
{
   int numVmhbas = this->hbas.size();
   int numDevicesByAdminPassthru = GetNumDevicesByAdminPassthru();

   Log(debug) << "Number of vmhbas: " << numVmhbas;

   /**
    * We expect there are at least 1 NVMe hbas available in the system
    */
   EXPECT_GT(numVmhbas, 0);

   /**
    * We should have the same number of devies got from the admin passthrough
    * interface.
    */
   EXPECT_EQ(numVmhbas, numDevicesByAdminPassthru);
}
