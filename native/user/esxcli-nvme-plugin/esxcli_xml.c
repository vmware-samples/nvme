/******************************************************************************
 * Copyright (c) 2014-2015 VMware, Inc. All rights reserved.
 *****************************************************************************/

/**
 * @file: esxcli_xml.c
 *
 *    Helper functions to generate XML outputs consumed by esxcli framework.
 */

#include <stdio.h>
#include "esxcli_xml.h"

void esxcli_xml_begin_output(void)
{
   printf("<?xml version=\"1.0\"?><output xmlns:esxcli=\"nvme\">\n");
}

void esxcli_xml_end_output(void)
{
   printf("</output>\n");
}

void xml_format(const char *tag, const char *output)
{
   printf("<%s>%s</%s>", tag, output, tag);
}

void xml_list_begin(const char *type)
{
   printf("<list type=\"%s\">\n", type);
}

void xml_list_end(void)
{
   printf("</list>\n");
}

