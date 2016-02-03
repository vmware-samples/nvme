/****************************************************************************
 * Copyright (c) 2014-2015 VMware, Inc. All rights reserved
 ***************************************************************************/

/**
 * @file nvme_test_session.h
 */

#ifndef __NVME_TEST_SESSION_H__
#define __NVME_TEST_SESSION_H__

#include <string>
#include <vector>
#include <stdexcept>
#include <gtest/gtest.h>

#include "nvme_hba.h"


class NvmeTestSessionException : public std::runtime_error {
public:
   NvmeTestSessionException();
   NvmeTestSessionException(const std::string &);
   NvmeTestSessionException(const char *);
};


class NvmeTestSession : public testing::Test {
public:
   NvmeTestSession();
   virtual ~NvmeTestSession();

   /**
    * Initialize HBA list
    */
   virtual void SetUp();

   /**
    * Cleanup
    */
   virtual void TearDown();

protected:
   /**
    * List of NVMe hbas found in the system. The list is primarily generated
    * from the output of `lspci -v` by searching for Class code "0108", this
    * should be able to find NVMe hbas even if the nvme driver is not loaded.
    */
   std::vector<NvmeHba> hbas;

   /**
    * Get an NVMe hba for testing. This finds an NVMe hba with a namespace that
    * doesn't have partitions. This is because some of the test cases may alter
    * the data on the NVMe namespace, which could bring some impact to the
    * system if the namespace is already used as a VMFS datastore or else.
    * Getting a namespace with no partitions on should be safe enough to run
    * most of the test cases (even the desruptive ones).
    */
   NvmeHba& GetTestHba() throw(NvmeTestSessionException);

private:
   void InitializeNvmeHbas();
};

#endif /** __NVME_TEST_SESSION_H__ */
