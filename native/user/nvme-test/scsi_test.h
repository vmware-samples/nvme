/****************************************************************************
 * Copyright (c) 2014-2015 VMware, Inc. All rights reserved
 ***************************************************************************/

/**
 * @file scsi_test.h
 */

#ifndef __SCSI_TEST_H__
#define __SCSI_TEST_H__

#include "nvme_test_session.h"

class ScsiTest : public NvmeTestSession {

public:
   ScsiTest();
   virtual ~ScsiTest();
   virtual void SetUp();
   virtual void TearDown();

public:
   int ScsiReadWrite6(
      bool isWrite,
      uint32_t lba,
      uint8_t lbc,
      uint8_t *dataBuffer,
      uint32_t dataBufferLen);

   int ScsiReadWrite10(
      bool isWrite,
      uint8_t protect,
      uint8_t dpo,
      uint8_t fua,
      uint8_t fua_nv,
      uint32_t lba,
      uint32_t lbc,
      uint8_t  *dataBuffer,
      uint32_t  dataBufferLen);

   int ScsiUnmap(
      uint64_t *lba_list,
      uint32_t *lbc_list,
      int      num);

public:
   NvmeHba       *testHba;
   NvmeNamespace *testNs;
   int            fd;
   uint64_t       nsCap;
   uint32_t       nsBlockSize;
};

#endif /** __SCSI_TEST_H__ */
