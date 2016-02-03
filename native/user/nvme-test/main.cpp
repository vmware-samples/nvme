/******************************************************************************
 * Copyright (c) 2014-2015 VMware, Inc. All rights reserved.
 *****************************************************************************/

/**
 * @file: main.cpp
 *
 *    main function for nvme-est
 */

#include <iostream>
#include <string>
#include <gtest/gtest.h>      /** for Gtest framework */
#include "utils.h"

int main(int argc, char *argv[])
{
   /**
    * Controls the log level for the test app.
    */
   g_loglevel = info;

   testing::InitGoogleTest(&argc, argv);
   return RUN_ALL_TESTS();
}
