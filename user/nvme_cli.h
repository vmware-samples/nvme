/*********************************************************************************
 * Copyright 2013 VMware, Inc. All rights reserved.
 * ******************************************************************************/
/*-
 * Copyright (C) 2012 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * @file: nvme_cli.h --
 *
 *    Header file for command line management interface of NVM Express driver.
 */

#ifndef _NVME_CLI_H
#define _NVME_CLI_H


#define CLI_DEBUG 0

#if CLI_DEBUG
#define DEBUG(fmt, ...) \
   printf("%s:%d: " fmt "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define DEBUG(fmt, ...)
#endif


/**
 * Print string to the standard output
 */
#define Output(fmt, ...) \
   printf(fmt "\n", ##__VA_ARGS__)

/**
 * Offset of the first argument for a dispatched command
 */
#define CLI_ARG_1 (cli->level)
/**
 * Offset of the second argument for a dispatched command
 */
#define CLI_ARG_2 (cli->level + 1)


struct cli_context;

/**
 * Function to validate arguments for a command
 */
typedef int (*validateArgsFn)(struct cli_context *, int argc, char *argv[]);

/**
 * Function to print online help information for a command
 */
typedef void (*usageFn)();

/**
 * Function to lookup the next level Cli for a command
 */
typedef struct cli_context* (*lookupCliFn)(struct cli_context *cli, const char *key);

/**
 * Function to execute a Cli command
 */
typedef int (*dispatchFn)(struct cli_context *cli, int argc, char *argv[]);

/**
 * Defines a Cli command
 */
struct cli_context {
   /**
    * Name of the command, also forms part of the command line.
    * Shall not contain spaces.
    */
   const char          *name;

   /**
    * Parent Cli
    */
   struct cli_context  *parent;

   /**
    * Online help string, to be printed out by the online help function
    */
   const char          *usageStr;

   /**
    * Level of the command
    */
   int                  level;

   /**
    * Private handler for validating command arguments
    */
   validateArgsFn       validateArgs;

   /**
    * Private handler for printing online help
    */
   usageFn              usage;

   /**
    * Private handler for looking up next level Cli command
    */
   lookupCliFn          lookupCli;
   
   /**
    * Private handler for executing or dispatching the command
    */
   dispatchFn           dispatch;

   /**
    * Used for organizing multiple Cli commands
    */
   vmk_ListLinks        head;

   /**
    * Used for tracking sub Cli commands.
    */
   vmk_ListLinks        list;
};


#endif
