/****************************************************************************
 * Copyright (c) 2014-2015 VMware, Inc. All rights reserved
 ***************************************************************************/

/**
 * @file utils.cpp
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <iostream>

#include "utils.h"

using namespace std;

int
ExecuteCommand(const std::string command, std::string &output)
{
   int rc = 0;
   FILE *p;
   char buf[128];

   p = popen(command.c_str(), "r");
   if (!p) {
      rc = -EINVAL;
      goto out;
   }

   output = "";
   while (!feof(p)) {
      if (fgets(buf, sizeof(buf), p) != NULL) {
         output += buf;
      }
   }

   pclose(p);

out:
   return 0;
}


/**
 * Defines the global log level
 */
LogLevel g_loglevel = info;


const std::string LogLevelNames[] = {
   "[DEBUG]",
   "[INFO ]",
   "[ERROR]",
};


Logger::Logger(LogLevel level)
{
   _buffer << LogLevelNames[level] << " ";
}


Logger::~Logger() {
   _buffer << std::endl;
   std::cerr << _buffer.str();
}
