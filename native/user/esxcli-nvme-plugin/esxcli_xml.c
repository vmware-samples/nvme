/******************************************************************************
 * Copyright (c) 2014-2015 VMware, Inc. All rights reserved.
 *****************************************************************************/

/**
 * @file: esxcli_xml.c
 *
 *    Helper functions to generate XML outputs consumed by esxcli framework.
 */

#include <stdio.h>
#include <vmkapi.h>
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

void xml_format_string_field(const char *name, const char *output)
{
   printf("<field name=\"%s\"><string>%s</string></field>\n", name, output);
}

void xml_format_int_field(const char *name, int output)
{
   printf("<field name=\"%s\"><int>%d</int></field>\n", name, output);
}

void xml_format_bool_field(const char *name, int output)
{
   printf("<field name=\"%s\"><bool>%s</bool></field>\n", name,
          output ? "true" : "false");
}

void xml_format_int2string_field(const char *name, int output)
{
   printf("<field name=\"%s\"><string>0x%x</string></field>\n", name, output);
}

void xml_format_ull2string_field(const char *name, vmk_uint64 output)
{
   printf("<field name=\"%s\"><string>0x%" VMK_FMT64 "x</string></field>\n",
          name, output);
}

void xml_format_128b2string_field(const char *name, vmk_uint8 *output)
{
   xml_field_begin(name);
   if (*(vmk_uint64 *)(output + 8) == 0) {
      printf("<string>0x%" VMK_FMT64 "x</string>\n", *(vmk_uint64 *)output);
   } else {
      printf("<string>0x%" VMK_FMT64 "x%" VMK_FMT64 "x</string>\n",
             *(vmk_uint64 *)(output + 8), *(vmk_uint64 *)output);
   }
   xml_field_end();
}

void xml_format_8B2string_field(const char *name, const char *output)
{
   printf("<field name=\"%s\"><string>%.8s</string></field>\n", name, output);
}

void xml_list_begin(const char *type)
{
   printf("<list type=\"%s\">\n", type);
}

void xml_list_end(void)
{
   printf("</list>\n");
}

void xml_struct_begin(const char *name)
{
   printf("<structure typeName=\"%s\">\n", name);
}

void xml_struct_end(void)
{
   printf("</structure>\n");
}

void xml_field_begin(const char *name)
{
   printf("<field name=\"%s\">\n", name);
}

void xml_field_end(void)
{
   printf("</field>\n");
}

