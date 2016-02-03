/****************************************************************************
 * Copyright (c) 2014-2015 VMware, Inc. All rights reserved
 ***************************************************************************/

/**
 * @file basic_test.h
 *
 * Implement Basic test cases that are fundamental for the driver, e.g. validate
 * device counts.
 *
 * More to come.
 */

#ifndef __BASIC_TEST_H__
#define __BASIC_TEST_H__

#include "nvme_test_session.h"

class BasicTest : public NvmeTestSession {
public:
   /**
    * Get number of vmhbas from `esxcfg-scsidevs -a` output.
    */
   int GetNumVmhbas();

   /**
    * Get number of NVMe devices from admin passthru interface.
    */
   int GetNumDevicesByAdminPassthru();
};

#endif /** __BASIC_TEST_H__ */
