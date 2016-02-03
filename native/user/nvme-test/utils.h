/****************************************************************************
 * Copyright (c) 2014-2015 VMware, Inc. All rights reserved
 ***************************************************************************/

/**
 * @file utils.h
 */

#ifndef __UTILS_H__
#define __UTILS_H__

#include <string>
#include <sstream>


/**
 * Execute a command and get the output.
 *
 * Put this here temporarily; may need to move it out to other source code
 * module later.
 */
int
ExecuteCommand(const std::string command, std::string &output);


/**
 * Utilities for logging
 */
enum LogLevel {
   debug,
   info,
   error,
};


class Logger {
public:
   Logger(LogLevel level);
   virtual ~Logger();

   template <class T>
   Logger &operator<<(const T &val) {
      _buffer << val;
      return *this;
   }

private:
   std::ostringstream _buffer;
};

extern LogLevel g_loglevel;

/**
 * Log a message.
 */
#define Log(level) \
   if (g_loglevel > level) ; \
   else Logger(level)

#endif /** __UTILS_H__ */

