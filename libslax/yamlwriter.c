/*
 * Copyright (c) 2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * yamlwriter.c -- turn yaml-oriented XML into yaml text
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
/* #include "yamllexer.h" */
#include "yamlwriter.h"

/* Forward declarations */
static int
yamlWriteArray (slax_writer_t *swp, xmlNodePtr parent, unsigned flags);
static int
yamlWriteChildren (slax_writer_t *swp, xmlNodePtr parent, unsigned flags);

static const char *
yamlAttribValue (xmlNodePtr nodep, const char *name)
{
    xmlAttrPtr attr = xmlHasProp(nodep, (const xmlChar *) name);

    if (attr && attr->children
	    && attr->children->type == XML_TEXT_NODE)
	return (const char *) attr->children->content;

    return NULL;
}

static int
yamlHasChildNodes (xmlNodePtr nodep)
{
    if (nodep == NULL)
	return FALSE;

    for (nodep = nodep->children; nodep; nodep = nodep->next)
	if (nodep->type != XML_TEXT_NODE)
	    return TRUE;

    return FALSE;
}

static const char *
yamlName (xmlNodePtr nodep)
{
    const char *name = yamlAttribValue(nodep, ATT_NAME);
    if (name == NULL)
	name = (const char *) nodep->name;

    return name;
}

static const char *
yamlValue (xmlNodePtr nodep)
{
    if (nodep && nodep->children && nodep->children->type == XML_TEXT_NODE)
	return (const char *) nodep->children->content;

    return NULL;
}

static const char *
yamlNameNeedsQuotes (const char *str, unsigned flags)
{
    static const char illegal[] = "\":;{} \n\t";

    if ((flags & YWF_OPTIONAL_QUOTES) && str != NULL
	    && xmlValidateName((const xmlChar *) str, FALSE) == 0
	    && strcspn(str, illegal) == strlen(str))
	return "";

    return "\"";
}

static void
yamlEscape (char *buf, int bufsiz, const char *str)
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
yamlWriteNewline (slax_writer_t *swp, int delta, unsigned flags)
{
    if ((flags & YWF_IDENT) && delta >= 0) {
        slaxWriteNewline(swp, 0);
        slaxWriteIndent(swp, delta);
    } else {
        slaxWriteNewline(swp, delta);
    }
}

static int
yamlWriteNode (slax_writer_t *swp, xmlNodePtr nodep,
		       unsigned in_flags)
{
    unsigned flags = in_flags;
    const char *type = yamlAttribValue(nodep, ATT_TYPE);
    const char *name = yamlName(nodep);
    const char *quote = yamlNameNeedsQuotes(name, flags);

    if (name == NULL)
	return 0;

    slaxLog("wn: -> [%s] [%s]", name ?: "", type ?: "");

    int nlen = strlen(name) + 1;
    char ename[nlen * 2];
    yamlEscape(ename, sizeof(ename), name);
    name = ename;

    if (type) {
	if (streq(type, VAL_NUMBER) || streq(type, VAL_TRUE)
	        || streq(type, VAL_FALSE) || streq(type, VAL_NULL)) {
	    const char *value = yamlValue(nodep);
	    int vlen = strlen(value) + 1;
	    char evalue[vlen * 2];
	    yamlEscape(evalue, sizeof(evalue), value);
	    value = evalue;

	    slaxWrite(swp, "%s%s%s: ", quote, name, quote);
	    slaxWrite(swp, "%s", value);

	    yamlWriteNewline(swp, 0, flags);
	    return 0;

	} else if (streq(type, VAL_ARRAY)) {

	    return yamlWriteArray(swp, nodep, flags);

	} else if (streq(type, VAL_MEMBER)) {
	    slaxLog("invalid member found: [%s] [%s]", name, type);
	    return 0;
	}
    }

    if (yamlHasChildNodes(nodep)) {
	slaxWrite(swp, "%s%s%s: ", quote, name, quote);

	yamlWriteNewline(swp, NEWL_INDENT, flags);
	yamlWriteChildren(swp, nodep, flags);
	slaxWriteIndent(swp, NEWL_OUTDENT);

    } else {
	const char *value = yamlValue(nodep);
	int vlen = value ? strlen(value) + 1 : 0;
	char evalue[vlen * 2 + 1];
	if (value) {
	    yamlEscape(evalue, sizeof(evalue), value);
	    value = evalue;
	}

	slaxWrite(swp, "%s%s%s: ", quote, name, quote);

	slaxWrite(swp, "\"%s\"", value ?: "");
	yamlWriteNewline(swp, 0, flags);
    }

    slaxLog("wn: <- [%s] [%s]", name ?: "", type ?: "");

    return 0;
}

static int
yamlWriteArray (slax_writer_t *swp UNUSED, xmlNodePtr arrayp,
		   unsigned flags)
{
    xmlNodePtr memberp;
    xmlNodePtr nodep;
    int rc = 0;

    const char *name = yamlName(arrayp);
    const char *type = yamlAttribValue(arrayp, ATT_TYPE);
    slaxLog("wa: -> [%s] [%s]", name ?: "", type ?: "");

    const char *quote = yamlNameNeedsQuotes(name, flags);
    slaxWrite(swp, "%s%s%s: ", quote, name, quote);
    
    yamlWriteNewline(swp, NEWL_INDENT, flags);

    for (memberp = arrayp->children; memberp; memberp = memberp->next) {
	if (memberp->type != XML_ELEMENT_NODE)
	    continue;

	unsigned first = TRUE;
	for (nodep = memberp->children; nodep; nodep = nodep->next) {
	    if (nodep->type != XML_ELEMENT_NODE)
		continue;

	    if (first) {
		slaxWrite(swp, "-   ");
	    }

	    yamlWriteNode(swp, nodep, flags);

	    if (first) {
		slaxWriteIndent(swp, NEWL_INDENT);
		first = FALSE;
	    }    
        }
	if (!first)
	    slaxWriteIndent(swp, NEWL_OUTDENT);

    }

    yamlWriteNewline(swp, NEWL_OUTDENT, flags);

    slaxLog("wa: <- [%s] [%s]", name ?: "", type ?: "");
    return rc;
}

static int
yamlWriteChildren (slax_writer_t *swp UNUSED, xmlNodePtr parent,
		   unsigned flags)
{
    xmlNodePtr nodep;
    int rc = 0;

    const char *name = yamlName(parent);
    const char *type = yamlAttribValue(parent, ATT_TYPE);
    slaxLog("wc: -> [%s] [%s]", name ?: "", type ?: "");

    for (nodep = parent->children; nodep; nodep = nodep->next) {
	if (nodep->type != XML_ELEMENT_NODE)
	    continue;

	yamlWriteNode(swp, nodep, flags);
    }

    slaxLog("wc: <- [%s] [%s]", name ?: "", type ?: "");
    return rc;
}

int
slaxYamlWriteNode (slaxWriterFunc_t func, void *data, xmlNodePtr nodep,
		       unsigned flags)
{
    slax_writer_t *swp = slaxGetWriter(func, data);
    const char *type = yamlAttribValue(nodep, ATT_TYPE);

    /* If they want a header, make one */
    if (!(flags & YWF_NOHEADER)) {
        slaxWrite(swp, "---");
        slaxWriteNewline(swp, 0);
        flags |= YWF_NOHEADER;	/* Then turn it off */
    }

    int rc;

    if (type && streq(type, VAL_ARRAY))
	rc = yamlWriteArray(swp, nodep, flags);
    else
	rc = yamlWriteChildren(swp, nodep, flags);

    slaxFreeWriter(swp);
    return rc;
}

int
slaxYamlWriteDoc (slaxWriterFunc_t func, void *data, xmlDocPtr docp,
		      unsigned flags)
{
    xmlNodePtr nodep = xmlDocGetRootElement(docp);

    return slaxYamlWriteNode(func, data, nodep, flags | YWF_ROOT);
}
