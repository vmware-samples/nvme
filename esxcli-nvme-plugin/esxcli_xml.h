/******************************************************************************
 * Copyright (c) 2014-2015 VMware, Inc. All rights reserved.
 *****************************************************************************/

/**
 * @file: esxcli_xml.h
 *
 *    Helper functions to generate XML outputs consumed by esxcli framework.
 */

#ifndef __ESXCLI_XML_H__
#define __ESXCLI_XML_H__

void esxcli_xml_begin_output(void);
void esxcli_xml_end_output(void);

void xml_format(const char *tag, const char *output);

void xml_list_begin(const char *type);
void xml_list_end(void);
void xml_struct_begin(const char *name);
void xml_struct_end(void);
void xml_field_begin(const char *name);
void xml_field_end(void);
void xml_format(const char *tag, const char *output);
void xml_format_string_field(const char *name, const char *output);
void xml_format_int_field(const char *name, int output);
void xml_format_bool_field(const char *name, int output);
void xml_format_int2string_field(const char *name, int output);
void xml_format_ull2string_field(const char *name, vmk_uint64 output);
void xml_format_128b2string_field(const char *name, vmk_uint8 *output);
void xml_format_8B2string_field(const char *name, const char *output);
void xml_format_id2string_field(const char *name, vmk_uint8 *output, int length);

#define PBOOL(name,output) xml_format_bool_field(name,output)
#define PSTR(name,output) xml_format_string_field(name,output)
#define PINT(name,output) xml_format_int_field(name,output)
#define PINTS(name,output) xml_format_int2string_field(name,output)
#define PULL(name,output) xml_format_ull2string_field(name,output)
#define P128BIT(name,output) xml_format_128b2string_field(name,output)
#define P8BYTE(name,output) xml_format_8B2string_field(name,output)
#define PID(name,output,length) xml_format_id2string_field(name,output,length)

#endif /** __ESXCLI_XML_H__ */
