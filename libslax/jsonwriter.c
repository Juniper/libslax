/*
 * $Id$
 *
 * Copyright (c) 2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * jsonwriter.c -- turn json-oriented XML into json text
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/xmlsave.h>

#include <libslax/slax.h>
#include "slaxinternals.h"
#include "jsonlexer.h"
#include "jsonwriter.h"

/* Forward declarations */
static int
jsonWriteChildren (slax_writer_t *swp, xmlNodePtr parent, unsigned flags);

static const char *
jsonAttribValue (xmlNodePtr nodep, const char *name)
{
    xmlAttrPtr attr = xmlHasProp(nodep, (const xmlChar *) name);

    if (attr && attr->children
	    && attr->children->type == XML_TEXT_NODE)
	return (const char *) attr->children->content;

    return NULL;
}

static int
jsonHasChildNodes (xmlNodePtr nodep)
{
    if (nodep == NULL)
	return FALSE;

    for (nodep = nodep->children; nodep; nodep = nodep->next)
	if (nodep->type != XML_TEXT_NODE)
	    return TRUE;

    return FALSE;
}

static const char *
jsonName (xmlNodePtr nodep)
{
    const char *name = jsonAttribValue(nodep, ATT_NAME);
    if (name == NULL)
	name = (const char *) nodep->name;

    return name;
}

static const char *
jsonValue (xmlNodePtr nodep)
{
    if (nodep && nodep->children && nodep->children->type == XML_TEXT_NODE)
	return (const char *) nodep->children->content;

    return NULL;
}

static const char *
jsonNameNeedsQuotes (const char *str, unsigned flags)
{
    static const char illegal[] = "\":;{} \n\t";

    if ((flags & JWF_OPTIONAL_QUOTES) && str != NULL
	    && xmlValidateName((const xmlChar *) str, FALSE) == 0
	    && strcspn(str, illegal) == strlen(str))
	return "";

    return "\"";
}

static const char *
jsonNeedsComma (xmlNodePtr nodep UNUSED)
{
    xmlNodePtr nextp;
    for (nextp = nodep->next; nextp; nextp = nextp->next)
	if (nextp->type != XML_TEXT_NODE)
	    return ",";

    return "";
}

static void
jsonEscape (char *buf, int bufsiz, const char *str)
{
    char esc;

    for ( ; str && *str; str++) {
	if (bufsiz < 1)
	    break;
	if (*str == '\b')
	    esc = 'b';
	else if (*str == '\n')
	    esc = 'n';
	else if (*str == '\r')
	    esc = 'r';
	else if (*str == '\t')
	    esc = 't';
	else {
	    *buf++ = *str;
	    bufsiz -= 1;
	    continue;
	}

	*buf++ = '\\';
	*buf++ = esc;
	bufsiz -= 2;
    }

    *buf = '\0';
}

static void
jsonWriteNewline (slax_writer_t *swp, int delta, unsigned flags)
{
    if (flags & JWF_PRETTY)
	slaxWriteNewline(swp, delta);
    else
	slaxWrite(swp, " ");
}

static int
jsonWriteNode (slax_writer_t *swp, xmlNodePtr nodep,
		       unsigned flags)
{
    const char *type = jsonAttribValue(nodep, ATT_TYPE);
    const char *name = jsonName(nodep);
    const char *quote = jsonNameNeedsQuotes(name, flags);
    const char *comma = jsonNeedsComma(nodep);

    if (name == NULL)
	return 0;

    int nlen = strlen(name) + 1;
    char ename[nlen * 2];
    jsonEscape(ename, sizeof(ename), name);
    name = ename;

    if (type) {
	if (streq(type, VAL_NUMBER) || streq(type, VAL_TRUE)
	        || streq(type, VAL_FALSE) || streq(type, VAL_NULL)) {
	    const char *value = jsonValue(nodep);
	    int vlen = strlen(value) + 1;
	    char evalue[vlen * 2];
	    jsonEscape(evalue, sizeof(evalue), value);
	    value = evalue;

	    if (!(flags & JWF_ARRAY))
		slaxWrite(swp, "%s%s%s: ", quote, name, quote);
	    slaxWrite(swp, "%s%s", value, comma);

	    jsonWriteNewline(swp, 0, flags);
	    return 0;

	} else if (streq(type, VAL_ARRAY)) {
	    if (!(flags & JWF_ARRAY))
		slaxWrite(swp, "%s%s%s: ", quote, name, quote);
	    slaxWrite(swp, "[");
	    jsonWriteNewline(swp, NEWL_INDENT, flags);

	    jsonWriteChildren(swp, nodep, JWF_ARRAY | flags);

	    slaxWrite(swp, "]%s", comma );
	    jsonWriteNewline(swp, NEWL_OUTDENT, flags);
	    return 0;

	} else if (streq(type, VAL_MEMBER)) {
	    flags |= JWF_ARRAY;
	    /* fall thru */
	}
    }

    if (jsonHasChildNodes(nodep)) {
	if (!(flags & JWF_ARRAY))
	    slaxWrite(swp, "%s%s%s: ", quote, name, quote);
	slaxWrite(swp, "{");
	jsonWriteNewline(swp, NEWL_INDENT, flags);

	jsonWriteChildren(swp, nodep, flags & ~JWF_ARRAY);

	slaxWrite(swp, "}%s", comma);
	jsonWriteNewline(swp, NEWL_OUTDENT, flags);

    } else {
	const char *value = jsonValue(nodep);
	int vlen = value ? strlen(value) + 1 : 0;
	char evalue[vlen * 2];
	if (value) {
	    jsonEscape(evalue, sizeof(evalue), value);
	    value = evalue;
	}

	if (!(flags & JWF_ARRAY))
	    slaxWrite(swp, "%s%s%s: ", quote, name, quote);

	slaxWrite(swp, "\"%s\"%s", value ?: "", comma);
	jsonWriteNewline(swp, 0, flags);
    }

    return 0;
}

static int
jsonWriteChildren (slax_writer_t *swp UNUSED, xmlNodePtr parent,
		   unsigned flags)
{
    xmlNodePtr nodep;
    int rc = 0;

    for (nodep = parent->children; nodep; nodep = nodep->next) {
	if (nodep->type == XML_ELEMENT_NODE)
	    jsonWriteNode(swp, nodep, flags);
    }

    return rc;
}

int
slaxJsonWriteNode (slaxWriterFunc_t func, void *data, xmlNodePtr nodep,
		       unsigned flags)
{
    slax_writer_t *swp = slaxGetWriter(func, data);
    const char *type = jsonAttribValue(nodep, ATT_TYPE);

    if (type && streq(type, VAL_ARRAY))
	flags |= JWF_ARRAY;

    slaxWrite(swp, (flags & JWF_ARRAY) ? "[" : "{");
    jsonWriteNewline(swp, NEWL_INDENT, flags);

    int rc = jsonWriteChildren(swp, nodep, flags);

    slaxWrite(swp, (flags & JWF_ARRAY) ? "]" : "}");
    slaxWriteNewline(swp, (flags & JWF_PRETTY) ? NEWL_OUTDENT : 0);

    slaxFreeWriter(swp);
    return rc;
}

int
slaxJsonWriteDoc (slaxWriterFunc_t func, void *data, xmlDocPtr docp,
		      unsigned flags)
{
    xmlNodePtr nodep = xmlDocGetRootElement(docp);
    return slaxJsonWriteNode(func, data, nodep, flags | JWF_ROOT);
}
