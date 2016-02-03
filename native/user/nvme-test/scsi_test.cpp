/****************************************************************************
 * Copyright (c) 2014-2015 VMware, Inc. All rights reserved
 ***************************************************************************/

/**
 * @file scsi_test.cpp
 */

#include <string>
#include <string.h>   /* for memset */
#include <stdint.h>
#include <limits>     /* for UINT32_MAX */
#include <stdlib.h>   /* for rand() */
#include <time.h>     /* for time() */

#include <sg_lib.h>   /* for SCSI Generic functions */
#include <sg_cmds.h>
#include <sg_pt.h>

#include "scsi_test.h"
#include "utils.h"


using namespace std;

/**
 * For checking if the Read Capacity (10) can return valid capacity size.
 */
#define UINT32_MAX         (numeric_limits<uint32_t>::max())


/**
 * Default timeout for SG commands, in seconds.
 */
#define DEF_PT_TIMEOUT     (60)


/**
 * Number of IO to issue during IO test
 */
#define RW_IO_COUNT        (10)


/**
 * Function to get particular namespace attributes, including namespace
 * capacity (nsCap) and formatted block size (nsBlockSize).
 */
static int
getNamespaceCaps(NvmeHba *hba, int namespaceID,
   uint64_t &nsCap, uint32_t &nsBlockSize)
{
   int      rc;
   uint8_t *identifyNsResp;

   identifyNsResp = new uint8_t[4096];
   rc = hba->Identify(namespaceID, identifyNsResp, 4096);
   if (rc != 0) {
      delete[] identifyNsResp;
      return rc;
   }

   /**
    * IDENTIFY NAMESPACE response is in little endian format.
    */
   nsCap = *(uint64_t *)(&identifyNsResp[8]);

   int nsFormatted = identifyNsResp[26] & 0xF;
   int lbaf        = *(uint32_t *)&identifyNsResp[128 + 4 * nsFormatted];
   int lbaShift    = (lbaf >> 16) & 0xf;
   nsBlockSize     = 1 << lbaShift;

   Log(debug) << "nsCap: " << nsCap << "; nsBlockSize: " << nsBlockSize;

   delete[] identifyNsResp;

   return 0;
}


/**
 * Open a scsi device and return the fd for SG
 */
static int
scsiOpen(const string &device)
{
   int fd;
   string path;

   path = "/dev/disks/" + device;

   return sg_cmds_open_device(path.c_str(), 1, 0);
}


/**
 * Close the opened SG device fd.
 */
static int
scsiClose(int fd)
{
   return sg_cmds_close_device(fd);
}


ScsiTest::ScsiTest()
{
}


ScsiTest::~ScsiTest()
{
}


/**
 * Initialize hba and device resources, and open fd to the SG device.
 */
void
ScsiTest::SetUp()
{
   this->testHba = &GetTestHba();
   this->testNs  = this->testHba->namespaces.size() > 0 ? &this->testHba->namespaces[0] : NULL;

   if (this->testNs == NULL) {
      throw (NvmeTestSessionException("no available namespace for test."));
   }

   Log(debug) << "test hba: " << testHba->vmhba;
   Log(debug) << "test namespace: " << testNs->deviceName;

   fd = scsiOpen(testNs->deviceName);
   if (fd < 0) {
      throw (NvmeTestSessionException("failed to open scsi device " + testNs->deviceName));
   }

   if (getNamespaceCaps(testHba, testNs->namespaceID, this->nsCap, this->nsBlockSize)) {
      throw (NvmeTestSessionException("failed to get properties for namespace " + testNs->deviceName));
   }
}


/**
 * Close connection to SG
 */
void
ScsiTest::TearDown()
{
   if (fd > 0) {
      scsiClose(fd);
   }
}


/**
 * Issue a SCSI READ (6) or WRITE (6) command
 */
int
ScsiTest::ScsiReadWrite6(bool isWrite,
   uint32_t lba,
   uint8_t  lbc,
   uint8_t  *dataBuffer,
   uint32_t dataBufferLen)
{
   uint8_t            rwCmd[6];
   uint8_t            senseData[64];
   struct sg_pt_base *ptvp;
   int                res, ret, senseCat, resid;

   memset(&rwCmd, 0, sizeof(rwCmd));

   rwCmd[0] = isWrite ? 0xa : 0x8;
   rwCmd[1] = (lba & 0x1f0000) >> 16;
   rwCmd[2] = (lba & 0xff00) >> 8;
   rwCmd[3] = lba & 0xff;
   rwCmd[4] = lbc;
   rwCmd[5] = 0;

   ptvp = construct_scsi_pt_obj();
   if (ptvp == NULL) {
      ADD_FAILURE() << "failed to allocate scsi pt cmd.";
      ret = -1;
      goto err_out;
   }

   set_scsi_pt_cdb(ptvp, rwCmd, sizeof(rwCmd));
   set_scsi_pt_sense(ptvp, senseData, sizeof(senseData));
   if (isWrite) {
      set_scsi_pt_data_out(ptvp, (uint8_t *)dataBuffer, dataBufferLen);
   } else {
      set_scsi_pt_data_in(ptvp, (uint8_t *)dataBuffer, dataBufferLen);
   }

   res = do_scsi_pt(ptvp, fd, DEF_PT_TIMEOUT, 1);
   ret = sg_cmds_process_resp(ptvp, "readwrite", res, dataBufferLen, senseData,
      0, 1, &senseCat);
   resid = get_scsi_pt_resid(ptvp);

   destruct_scsi_pt_obj(ptvp);

   Log(debug) << "res: " << res;
   Log(debug) << "ret: " << ret;
   Log(debug) << "resid: " << resid;
   Log(debug) << "senseCat: " << senseCat;

err_out:
   return ret;
}


/**
 * Issue a SCSI READ (10) or WRITE (10) command
 */
int
ScsiTest::ScsiReadWrite10(bool isWrite,
      uint8_t protect,
      uint8_t dpo,
      uint8_t fua,
      uint8_t fua_nv,
      uint32_t lba,
      uint32_t lbc,
      uint8_t  *dataBuffer,
      uint32_t  dataBufferLen)
{
   uint8_t            rwCmd[10];
   uint8_t            senseData[64];
   struct sg_pt_base *ptvp;
   int                res, ret, senseCat, resid, i;

   memset(&rwCmd, 0, sizeof(rwCmd));

   rwCmd[0] = isWrite ? 0x2a : 0x28;
   rwCmd[1] = 0;
   rwCmd[1] |= (protect & 0x3) << 5;
   rwCmd[1] |= (dpo & 0x1) << 4;
   rwCmd[1] |= (fua & 0x1) << 3;
   rwCmd[1] |= (fua_nv & 0x1) << 1;
   rwCmd[2] = (lba >> 24) & 0xff;
   rwCmd[3] = (lba >> 16) & 0xff;
   rwCmd[4] = (lba >> 8) & 0xff;
   rwCmd[5] = lba & 0xff;
   rwCmd[6] = 0;  /** GROUP NUMBER */
   rwCmd[7] = (lbc >> 8) & 0xff;
   rwCmd[8] = lbc & 0xff;
   rwCmd[9] = 0;  /** CONTROL */

   for (i = 0; i < 10; i++) {
      Log(debug) << "rwCmd[" << i << "]: 0x" << hex << (int)rwCmd[i];
   }
      
   ptvp = construct_scsi_pt_obj();
   if (ptvp == NULL) {
      ADD_FAILURE() << "failed to allocate scsi pt cmd.";
      ret = -1;
      goto err_out;
   }

   set_scsi_pt_cdb(ptvp, rwCmd, sizeof(rwCmd));
   set_scsi_pt_sense(ptvp, senseData, sizeof(senseData));
   if (isWrite) {
      set_scsi_pt_data_out(ptvp, (uint8_t *)dataBuffer, dataBufferLen);
   } else {
      set_scsi_pt_data_in(ptvp, (uint8_t *)dataBuffer, dataBufferLen);
   }

   res = do_scsi_pt(ptvp, fd, DEF_PT_TIMEOUT, 1);
   ret = sg_cmds_process_resp(ptvp, "readwrite", res, dataBufferLen, senseData,
      0, 1, &senseCat);
   resid = get_scsi_pt_resid(ptvp);

   destruct_scsi_pt_obj(ptvp);

   Log(debug) << "res: " << res;
   Log(debug) << "ret: " << ret;
   Log(debug) << "resid: " << resid;
   Log(debug) << "senseCat: " << senseCat;

err_out:
   return ret;
}


/**
 * Issue a scsi unmap commond.
 *
 * @param [in] lba_list   pointer to the buffer which holds the values of 
 *                        UNMAP LOGICAL BLOCK ADDRESS field in unmap block descriptors
 * @param [in] lbc_list   pointer to the buffer which holds the values of 
 *                        NUMBER OF LOGICAL BLOCKS field in unmap block descriptors
 * @param [in] num        the number of unmap block descriptors
 *
 * return  0       if command is completed successfully 
 *         others  if command is completed with failure
 *
 * ref: sbc3r36, 5.28
 *
 */
int 
ScsiTest::ScsiUnmap(uint64_t *lba_list, uint32_t *lbc_list, int num)
{
   uint16_t descriptor_len;
   uint16_t unmap_len;
   uint32_t parameter_len;
   uint8_t  *parameter_list;
   int i, k;
   int rc;

   /* Set the UNMAP BLOCK DESCRIPTOR DATA LENGTH field in unmap parameter list.*/
   descriptor_len = num * 16;

   /* Set the UNMAP DATA LENGTH field in unmap parameter list.*/
   unmap_len = descriptor_len + 6;

   /* Allocate the unmap parameter list.*/
   parameter_len = descriptor_len + 8;
   parameter_list = new(uint8_t[parameter_len]);
   memset(parameter_list, 0, parameter_len);

   /* Fill in the header in unmap parameter list.*/
   parameter_list[0] = (unmap_len >> 8) & 0xff;
   parameter_list[1] = unmap_len & 0xff;
   parameter_list[2] = (descriptor_len >> 8) & 0xff;
   parameter_list[3] = descriptor_len & 0xff;

   /* Fill in the block descriptors in unmap paramter list.*/
   k = 8;
   for (i = 0; i < num; i++) {
      parameter_list[k++] = (lba_list[i] >> 56) & 0xff;
      parameter_list[k++] = (lba_list[i] >> 48) & 0xff;
      parameter_list[k++] = (lba_list[i] >> 40) & 0xff;
      parameter_list[k++] = (lba_list[i] >> 32) & 0xff;
      parameter_list[k++] = (lba_list[i] >> 24) & 0xff;
      parameter_list[k++] = (lba_list[i] >> 16) & 0xff;
      parameter_list[k++] = (lba_list[i] >> 8) & 0xff;
      parameter_list[k++] = lba_list[i] & 0xff;

      parameter_list[k++] = (lbc_list[i] >> 24) & 0xff;
      parameter_list[k++] = (lbc_list[i] >> 16) & 0xff;
      parameter_list[k++] = (lbc_list[i] >> 8) & 0xff;
      parameter_list[k++] = lbc_list[i] & 0xff;

      k += 4;
   }

   rc = sg_ll_unmap_v2(fd, 0, 0, DEF_PT_TIMEOUT, parameter_list, parameter_len, 0, 0);
   
   delete[] parameter_list;

   return rc;
}


/**
 * Issue a READ CAPACITY (10) command in to the SCSI device and check if the
 * return value is correct (matches the IDENTIFY page of the namespace).
 */
TEST_F(ScsiTest, ReadCapacity) {
   int      rc;
   uint8_t  resp[8];
   uint32_t rlba;
   uint32_t lbn;

   rc = sg_ll_readcap_10(fd, 0, 0, resp, sizeof(resp), 0, 0);
   if (rc != 0) {
      ADD_FAILURE() << "failed to issue READ CAPACITY (10) command, " << rc;
      return;
   }

   rlba = resp[0] << 24 |
      resp[1] << 16     |
      resp[2] << 8      |
      resp[3];
   lbn = resp[4] << 24  |
      resp[5] << 16     |
      resp[6] << 8      |
      resp[7];

   Log(debug) << "Returned Logical Block Address: " << rlba;
   Log(debug) << "Logical Block Length In Bytes: " << lbn;

   if (nsCap > UINT32_MAX) {
      EXPECT_EQ(rlba, UINT32_MAX);
   } else {
      EXPECT_EQ(rlba, (uint32_t)nsCap - 1);
   }
   EXPECT_EQ(lbn, nsBlockSize);
}


/**
 * Issue a READ CAPACITY (16) command in to the SCSI device and check if the
 * return value is correct (matches the IDENTIFY page of the namespace).
 *
 * TODO: Currently only check the "Returned Logical Block Address" and "Logical
 *       Block Length In Bytes" field. Should check other fields in the READ
 *       CAPACITY (16) parameter data as well.
 */
TEST_F(ScsiTest, ReadCapacity16) {
   uint8_t        resp[32];
   uint64_t       rlba;
   uint32_t       lbn;
   uint8_t        lbppbe;
   int            rc;

   rc = sg_ll_readcap_16(fd, 0, 0, resp, sizeof(resp), 0, 0);
   if (rc != 0) {
      ADD_FAILURE() << "failed to issue READ CAPACITY (16) command, " << rc;
      return;
   }

   rlba = (uint64_t)resp[0] << 56   |
      (uint64_t)resp[1] << 48       |
      (uint64_t)resp[2] << 40       |
      (uint64_t)resp[3] << 32       |
      (uint64_t)resp[4] << 24       |
      (uint64_t)resp[5] << 16       |
      (uint64_t)resp[6] << 8        |
      (uint64_t)resp[7];

   lbn = resp[8] << 24  |
      resp[9] << 16     |
      resp[10] << 8     |
      resp[11] << 0;

   lbppbe = resp[13] & 0xf;

   Log(debug) << "Returned Logical Block Address: " << rlba;
   Log(debug) << "Logical Block Length In Bytes: " << lbn;
   Log(debug) << "Logical Blocks Per Physical Block Exponent: " << (int)lbppbe;

   EXPECT_EQ(rlba, nsCap - 1);
   EXPECT_EQ(lbn, nsBlockSize);

   /**
    * Check lbppbe based on bloksize.
    * Currently, we only support two kinds of logical blocksize: 512B & 4KB
    */
   if (nsBlockSize == 512) {
      EXPECT_EQ(lbppbe, 3);
   } else if (nsBlockSize == 4096) {
      EXPECT_EQ(lbppbe, 0);
   } else {
      ADD_FAILURE() << "Namespace is neither in 512e nor 4kn mode!";
   }
   
}


/**
 * Issue a set of WRITE 6 and READ 6 commands into the device, and validate if
 * the data written can be read back successfully.
 *
 * The test will cover the first block, the last available block (if
 * applicable), and some random blocks in between.
 */
TEST_F(ScsiTest, ReadWrite6_Basic)
{
   uint8_t  *dataInBuffer;
   uint8_t  *dataOutBuffer;
   uint32_t  dataBufferLen;
   int       rc;
   uint32_t  lba_range[RW_IO_COUNT];
   uint32_t  lba;
   uint32_t  lbc;

   srand(time(NULL));

   lba_range[0]  = 0;
   lba_range[1]  = nsCap - 1;
   for (int i = 2; i < RW_IO_COUNT; i++) {
      lba_range[i] = rand() % nsCap;
   }

   lbc = 1;

   dataBufferLen = lbc * nsBlockSize;
   dataInBuffer  = new(uint8_t[dataBufferLen]);
   dataOutBuffer = new(uint8_t[dataBufferLen]);

   for (int i = 0; i < sizeof(lba_range)/sizeof(lba_range[0]); i++) {
      lba = lba_range[i];
      memset(dataOutBuffer, rand(), dataBufferLen);

      rc = ScsiReadWrite6(true, lba, lbc, dataOutBuffer, dataBufferLen);
      EXPECT_EQ(rc, dataBufferLen);

      rc = ScsiReadWrite6(false, lba, lbc, dataInBuffer, dataBufferLen);
      EXPECT_EQ(rc, dataBufferLen);

      EXPECT_EQ(memcmp(dataInBuffer, dataOutBuffer, dataBufferLen), 0);
   }

   delete[] dataInBuffer;
   delete[] dataOutBuffer;
}


/**
 * Issue a set of WRITE 10 and READ 10 commands into the device, and validate if
 * the data written can be read back successfully.
 *
 * The test will cover the first block, the last available block (if
 * applicable), and some random blocks in between.
 */
TEST_F(ScsiTest, ReadWrite10_Basic)
{
   uint8_t  *dataInBuffer;
   uint8_t  *dataOutBuffer;
   uint32_t  dataBufferLen;
   int       rc;
   uint32_t  lba_range[RW_IO_COUNT];
   uint32_t  lba;
   uint32_t  lbc;

   srand(time(NULL));

   lba_range[0]  = 0;
   lba_range[1]  = nsCap - 1;
   for (int i = 2; i < RW_IO_COUNT; i++) {
      lba_range[i] = rand() % nsCap;
   }

   lbc = 1;

   dataBufferLen = lbc * nsBlockSize;
   dataInBuffer  = new(uint8_t[dataBufferLen]);
   dataOutBuffer = new(uint8_t[dataBufferLen]);

   for (int i = 0; i < sizeof(lba_range)/sizeof(lba_range[0]); i++) {
      lba = lba_range[i];
      memset(dataOutBuffer, rand(), dataBufferLen);

      rc = ScsiReadWrite10(true, 0, 0, 0, 0, lba, lbc, dataOutBuffer, dataBufferLen);
      EXPECT_EQ(rc, dataBufferLen);

      rc = ScsiReadWrite10(false, 0, 0, 0, 0, lba, lbc, dataInBuffer, dataBufferLen);
      EXPECT_EQ(rc, dataBufferLen);

      EXPECT_EQ(memcmp(dataInBuffer, dataOutBuffer, dataBufferLen), 0);
   }

   delete[] dataInBuffer;
   delete[] dataOutBuffer;
}
TEST_F(ScsiTest, SyncCache10)
{
   int rc;
   /* test with IMMED = 0, which is not supported*/
   rc = sg_ll_sync_cache_10(fd, 0, 0, 0, 0, 0, 0, 0);
   EXPECT_NE(rc, 0);

   /* test with IMMED = 1*/
   rc = sg_ll_sync_cache_10(fd, 0, 1, 0, 0, 0, 0, 0);
   EXPECT_EQ(rc, 0);
}

TEST_F(ScsiTest, LogSense)
{
   int rc;
   
   /* TODO: add unit test here*/
#if 0
   /* supported pages */
   pg_code = 0x0;
   rc = sg_ll_log_sense(fd, 0, 0, 1, pg_code, 0, 0, (unsigned char*)&resp, sizeof(resp), 0, 0);
   EXPECT_EQ(rc, 0);

   /* Informational Exceptions */
   pg_code = 0x2f;
   rc = sg_ll_log_sense(fd, 0, 0, 1, pg_code, 0, 0, (unsigned char*)&resp, sizeof(resp), 0, 0);
   EXPECT_EQ(rc, 0);

   /* Temperature log page */
   pg_code = 0x0d;
   rc = sg_ll_log_sense(fd, 0, 0, 1, pg_code, 0, 0, (unsigned char*)&resp, sizeof(resp), 0, 0);
   EXPECT_EQ(rc, 0);
#endif
}

/**
 * Issue a set of WRITE 10 and READ 10 commands with setting FUA into the device,
 * and validate if the data written can be read back successfully.
 *
 * The test will cover the first block, the last available block (if
 * applicable), and some random blocks in between.
 */
TEST_F(ScsiTest, FUA_ReadWrite10)
{
   uint8_t  *dataInBuffer;
   uint8_t  *dataOutBuffer;
   uint32_t  dataBufferLen;
   int       rc;
   uint32_t  lba_range[RW_IO_COUNT];
   uint32_t  lba;
   uint32_t  lbc;

   srand(time(NULL));

   lba_range[0]  = 0;
   lba_range[1]  = nsCap - 1;
   for (int i = 2; i < RW_IO_COUNT; i++) {
      lba_range[i] = rand() % nsCap;
   }

   lbc = 1;

   dataBufferLen = lbc * nsBlockSize;
   dataInBuffer  = new(uint8_t[dataBufferLen]);
   dataOutBuffer = new(uint8_t[dataBufferLen]);

   for (int i = 0; i < sizeof(lba_range)/sizeof(lba_range[0]); i++) {
      lba = lba_range[i];
      memset(dataOutBuffer, rand(), dataBufferLen);

      rc = ScsiReadWrite10(true, 0, 0, 1, 0, lba, lbc, dataOutBuffer, dataBufferLen);
      EXPECT_EQ(rc, dataBufferLen);

      rc = ScsiReadWrite10(false, 0, 0, 1, 0, lba, lbc, dataInBuffer, dataBufferLen);
      EXPECT_EQ(rc, dataBufferLen);

      EXPECT_EQ(memcmp(dataInBuffer, dataOutBuffer, dataBufferLen), 0);
   }

   delete[] dataInBuffer;
   delete[] dataOutBuffer;
}


/**
 * Issue an unmap command to deallocate part of allocated blocks, and
 * validate if the data on the other blocks can be read back.
 */
TEST_F(ScsiTest, Unmap)
{
   uint8_t   *identifyCtrlrResp;
   uint8_t   *dataInBuffer;
   uint8_t   *dataOutBuffer;
   uint32_t  dataBufferLen;
   uint32_t  write_lba;
   uint32_t  write_lbc;
   uint32_t  read_lba;
   uint32_t  read_lbc;
   uint64_t  unmap_lba;
   uint32_t  unmap_lbc;
   int rc;

   /* Validate if device supports DSM command.*/
   identifyCtrlrResp = new uint8_t[4096];
   rc = testHba->Identify(-1, identifyCtrlrResp, 4096);
   if (rc != 0) {
      ADD_FAILURE() << "Failed to identify controller.";
      delete[] identifyCtrlrResp;
      return;
   }
   if (!(identifyCtrlrResp[520] & 0x4)) {
      ADD_FAILURE() << "The device doesn't support trim.";
      delete[] identifyCtrlrResp;
      return;
   }

   srand(time(NULL));

   /* Randomly select a range of consecutive LBAs to write data to.*/
   write_lbc = 20;
   write_lba = rand() % (nsCap - write_lbc);
   
   /* Within the writing range, randomly select a range of consecutive LBAs to unmap.*/
   unmap_lbc = write_lbc / 2;
   unmap_lba = write_lba + rand() % (write_lbc - unmap_lbc);

   /* Write data to LBAs specified by write_lba and write_lbc.*/
   dataBufferLen = write_lbc * nsBlockSize;
   dataOutBuffer = new(uint8_t[dataBufferLen]);
   memset(dataOutBuffer, rand(), dataBufferLen);
   rc = ScsiReadWrite10(true, 0, 0, 0, 0, write_lba, write_lbc, dataOutBuffer, dataBufferLen);
   EXPECT_EQ(rc, dataBufferLen);

   /* Validate if the data can be read back.*/
   dataInBuffer = new(uint8_t[dataBufferLen]);
   rc = ScsiReadWrite10(false, 0, 0, 0, 0, write_lba, write_lbc, dataInBuffer, dataBufferLen);
   EXPECT_EQ(rc, dataBufferLen);
   EXPECT_EQ(memcmp(dataInBuffer, dataOutBuffer, dataBufferLen), 0);

   /* Issue SCSI Unmap command to deallocate LBAs specified by unmap_lba and unmap_lbc.*/
   rc = ScsiUnmap(&unmap_lba, &unmap_lbc, 1);
   EXPECT_EQ(rc, 0);

   /* Validate if the data written to the LBAs that are not unmapped can be read back.
    * Note: according to the selection method of unmap LBAs, the LBAs that are less than unmap_lba
    * and greater than (unmap_lba + unmap_lbc -1) are not unmapped.*/
   read_lba = write_lba;
   read_lbc = unmap_lba - write_lba;
   if (read_lbc > 0) {
      dataBufferLen = nsBlockSize * read_lbc;
      rc = ScsiReadWrite10(false, 0, 0, 0, 0, read_lba, read_lbc, dataInBuffer, dataBufferLen);
      EXPECT_EQ(rc, dataBufferLen);
      EXPECT_EQ(memcmp(dataInBuffer, dataOutBuffer + (read_lba - write_lba) * nsBlockSize, dataBufferLen), 0);
   }

   read_lba = unmap_lba + unmap_lbc;
   read_lbc = write_lbc - unmap_lbc - read_lbc; 
   if (read_lbc > 0) {
      dataBufferLen = nsBlockSize * read_lbc;
      rc = ScsiReadWrite10(false, 0, 0, 0, 0, read_lba, read_lbc, dataInBuffer, dataBufferLen);
      EXPECT_EQ(rc, dataBufferLen);
      EXPECT_EQ(memcmp(dataInBuffer, dataOutBuffer + (read_lba - write_lba) * nsBlockSize, dataBufferLen), 0);
   }

   delete[] identifyCtrlrResp;
   delete[] dataInBuffer;
   delete[] dataOutBuffer;
}
