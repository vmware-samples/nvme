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

#endif /** __ESXCLI_XML_H__ */
