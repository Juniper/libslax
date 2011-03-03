/*
 * $Id: slaxinternals.h,v 1.3 2008/05/21 02:06:12 phil Exp $
 *
 * Copyright (c) 2010, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 */

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "slaxparser.h"
#include <ctype.h>
#include <errno.h>

#include <libxslt/extensions.h>
#include <libexslt/exslt.h>

/**
 * Determine if a node's name and namespace match those given.
 * @nodep: the node to test
 * @uri: the namespace string
 * @name: the element name (localname)
 * @return non-zero if this is a match
 */
int
slaxNodeIs (xmlNodePtr nodep, const char *uri, const char *name)
{
    return (nodep && nodep->name && streq((const char *) nodep->name, name)
	    && nodep->ns && nodep->ns->href
	    && streq((const char *) nodep->ns->href, uri));
}

/**
 * Detect a particular xsl:* node
 *
 * @nodep: node to test
 * @name: <xsl:*> element name to test
 * @returns non-zero if the node matches the given name
 */
int
slaxNodeIsXsl (xmlNodePtr nodep, const char *name)
{
    return (nodep && nodep->ns && nodep->ns->href
	    && streq((const char *) nodep->name, name)
	    && streq((const char *) nodep->ns->href, XSL_URI));
}

/*
 * Look upward thru the stack to find a namespace that's the
 * default (one with no prefix).
 */
static xmlNsPtr
slaxFindDefaultNs (slax_data_t *sdp UNUSED)
{
    xmlNsPtr ns = NULL;
    xmlNodePtr nodep;

    for (nodep = sdp->sd_ctxt->node; nodep; nodep = nodep->parent)
	for (ns = nodep->nsDef; ns; ns = ns->next)
	    if (ns->prefix == NULL)
		return ns;

    return NULL;
}

/**
 * Find a namespace definition in the node's parent chain that
 * matches the given prefix.  Since the prefix string lies in situ
 * in the original tag, we give the length of the prefix.
 *
 * @param nodep the node where the search begins
 * @param prefix the prefix string
 * @param len the length of the prefix string
 * @return pointer to the existing name space 
 */
static xmlNsPtr
slaxFindNs (xmlNodePtr nodep, const char *prefix, int len)
{
    xmlChar *name;

    name = alloca(len + 1);
    if (name == NULL)
	return NULL;

    memcpy(name, prefix, len);
    name[len] = '\0';
    return xmlSearchNs(nodep->doc, nodep, name);
}

/*
 * Add an element to the top of the context stack
 */
void
slaxElementOpen (slax_data_t *sdp, const char *tag)
{
    xmlNodePtr nodep = sdp->sd_ctxt->node;
    xmlNsPtr ns = NULL;
    const char *cp;

    /*
     * Deal with namespaces.  If there's a prefix, we need to find
     * the definition of this namespace and pass it along.
     */
    cp = index(tag, ':');
    if (cp) {
	const char *prefix = tag;
	int len = cp - prefix;
	tag = cp + 1;
	ns = slaxFindNs(nodep, prefix, len);
    }

    /* If we don't have a namespace, use the parent's namespace */
    if (ns == NULL)
	ns = slaxFindDefaultNs(sdp);

    nodep = xmlNewNode(ns, (const xmlChar *) tag);
    if (nodep == NULL) {
	fprintf(stderr, "could not make node: %s\n", tag);
	return;
    }

    slaxAddChildLineNo(sdp->sd_ctxt, sdp->sd_ctxt->node, nodep);
    nodePush(sdp->sd_ctxt, nodep);
}

/*
 * Pop an element off the context stack
 */
void
slaxElementClose (slax_data_t *sdp)
{
    nodePop(sdp->sd_ctxt);
}

/**
 * Add a namespace to the node on the top of the stack
 *
 * @param sdp main slax data structure
 * @param prefix namespace prefix
 * @param uri namespace URI
 */
void
slaxNsAdd (slax_data_t *sdp, const char *prefix, const char *uri)
{
    xmlNsPtr ns;

    ns = xmlNewNs(sdp->sd_ctxt->node, (const xmlChar *) uri,
		  (const xmlChar *) prefix);

    /*
     * set the namespace node, making sure that if the default namspace
     * is unbound on a parent we simply keep it NULL
     */
    if (ns) {
	xmlNsPtr cur = sdp->sd_ctxt->node->ns;
	if (cur) {
	    if ((cur->prefix == NULL && ns->prefix == NULL)
		|| (cur->prefix && ns->prefix
		    && streq((const char *) cur->prefix,
			     (const char *) ns->prefix)))
		xmlSetNs(sdp->sd_ctxt->node, ns);
	} else {
	    if (ns->prefix == NULL)
		xmlSetNs(sdp->sd_ctxt->node, ns);
	}
    }
}

/*
 * Add a simple value attribute.  Our style parameter is limited to
 * SAS_NONE, SAS_XPATH, or SAS_SELECT.  See the other slaxAttribAdd*()
 * variants for other styles.
 */
void
slaxAttribAdd (slax_data_t *sdp, int style,
	       const char *name, slax_string_t *value)
{
    slax_string_t *ssp;
    char *buf;
    xmlNsPtr ns = NULL;
    xmlAttrPtr attr;
    const char *cp;
    int ss_flags = (style == SAS_SELECT) ? SSF_CONCAT  : 0;

    ss_flags |= SSF_QUOTES;	/* Always want the quotes */

    if (value == NULL)
	return;

    if (slaxLogIsEnabled)
	for (ssp = value; ssp; ssp = ssp->ss_next)
	    slaxLog("initial: xpath_value: %s", ssp->ss_token);

    /* If we need the "slax" namespace, add it to the parent node */
    if (slaxNeedsSlaxNs(value))
	slaxSetSlaxNs(sdp, sdp->sd_ctxt->node, FALSE);

    if (value->ss_next == NULL) {
	if (style == SAS_SELECT && value->ss_ttype == T_QUOTED
		&& (value->ss_flags & SSF_BOTHQS)) {
	    /*
	     * The string contains both types of quotes, which is bad news,
	     * but we are 'select' style, so we just throw the string into
	     * a text node.
	     */
	    xmlNodePtr tp;

	    tp = xmlNewText((const xmlChar *) value->ss_token);
	    slaxAddChildLineNo(sdp->sd_ctxt, sdp->sd_ctxt->node, tp);

	    return;
	}

	/*
	 * There is only one value, so we stuff it into the
	 * attribute give in the 'name' parameter.
	 */
	buf = slaxStringAsChar(value, SSF_BRACES | ss_flags);

    } else {
	/*
	 * There are multiple values, like: var $x=test/one _ test/two
	 * We stuff these values inside a call to the XPath "concat()"
	 * function, which will do all the hard work for us, without
	 * giving us a result fragment tree.
	 */
	buf = slaxStringAsChar(value, SSF_BRACES | ss_flags);
    }

    /*
     * Deal with namespaces.  If there's a prefix, we need to find
     * the definition of this namespace and pass it along.
     */
    cp = index(name, ':');
    if (cp) {
	const char *prefix = name;
	int len = cp - prefix;
	name = cp + 1;
	ns = slaxFindNs(sdp->sd_ctxt->node, prefix, len);
    }

    attr = xmlNewNsProp(sdp->sd_ctxt->node, ns, (const xmlChar *) name,
		      (const xmlChar *) buf);
    if (attr == NULL)
	fprintf(stderr, "could not make attribute: @%s=%s\n", name, buf);

    free(buf);
}

/*
 * Add a value to an attribute on an XML element.  The value consists of
 * one or more items, each of which can be a string, a variable reference,
 * or an xpath expression.  If the value (any items) does not contain an
 * xpath expression, we use the normal attribute substitution to achieve
 * this ({$foo}).  Otherwise, we use the xsl:attribute element to construct
 * the attribute.  Note that this function uses only the SAS_ATTRIB style
 * of quote handling.
 */
void
slaxAttribAddValue (slax_data_t *sdp, const char *name, slax_string_t *value)
{
    int len;
    xmlAttrPtr attr;
    xmlNsPtr ns = NULL;
    char *buf;
    const char *cp;

    buf = slaxStringAsValueTemplate(value, SSF_BRACES);
    if (buf == NULL)
	return;

    /*
     * Deal with namespaces.  If there's a prefix, we need to find
     * the definition of this namespace and pass it along.
     */
    cp = index(name, ':');
    if (cp) {
	const char *prefix = name;
	len = cp - prefix;
	name = cp + 1;
	ns = slaxFindNs(sdp->sd_ctxt->node, prefix, len);
    }

    attr = xmlNewNsProp(sdp->sd_ctxt->node, ns, (const xmlChar *) name,
		      (const xmlChar *) buf);
    if (attr == NULL)
	fprintf(stderr, "could not make attribute: @%s=%s\n", name, buf);

    xmlFreeAndEasy(buf);
}

/*
 * Add a simple value attribute as straight string.  Note that this function
 * always uses the SAS_XPATH style of quote handling.
 */
void
slaxAttribAddString (slax_data_t *sdp, const char *name,
		     slax_string_t *value, unsigned flags)
{
    slax_string_t *ssp;
    char *buf;
    xmlAttrPtr attr;

    if (value == NULL)
	return;

    if (slaxLogIsEnabled)
	for (ssp = value; ssp; ssp = ssp->ss_next)
	    slaxLog("initial: xpath_value: %s", ssp->ss_token);
	
    buf = slaxStringAsChar(value, flags);
    if (buf) {
	attr = xmlNewProp(sdp->sd_ctxt->node, (const xmlChar *) name,
			  (const xmlChar *) buf);
	if (attr == NULL)
	    fprintf(stderr, "could not make attribute: @%s=%s\n", name, buf);

	xmlFree(buf);
    }
}

/*
 * Add a literal value as an attribute to the current node
 */
void
slaxAttribAddLiteral (slax_data_t *sdp, const char *name, const char *val)
{
    xmlAttrPtr attr;

    attr = xmlNewProp(sdp->sd_ctxt->node, (const xmlChar *) name,
		      (const xmlChar *) val);
    if (attr == NULL)
	fprintf(stderr, "could not make attribute: @%s=%s\n", name, val);
}

/*
 * Extend the existing value for an attribute, appending the given value.
 */
static void
slaxNodeAttribExtend (slax_data_t *sdp, xmlNodePtr nodep,
		  const char *attrib, const char *value)
{
    const xmlChar *uattrib = (const xmlChar *) attrib;
    xmlChar *current = xmlGetProp(nodep, uattrib);
    int clen = current ? xmlStrlen(current) + 1 : 0;
    int vlen = strlen(value) + 1;
    xmlAttrPtr attr;

    unsigned char *newp = alloca(clen + vlen);
    if (newp == NULL) {
	xmlParserError(sdp->sd_ctxt, "%s:%d: out of memory",
		       sdp->sd_filename, sdp->sd_line);
	return;
    }

    if (clen) {
	memcpy(newp, current, clen - 1);
	newp[clen - 1] = ' ';
	xmlFree(current);
    }

    memcpy(newp + clen, value, vlen);

    attr = xmlSetProp(nodep, uattrib, newp);
}

/*
 * Extend the existing value for an attribute, appending the given value.
 */
void
slaxAttribExtend (slax_data_t *sdp, const char *attrib, const char *value)
{
    slaxNodeAttribExtend(sdp, sdp->sd_ctxt->node, attrib, value);
}

/*
 * Find or construct a (possibly temporary) namespace node
 * for the "func" exslt library and put the given node into
 * that namespace.  We also have to add this as an "extension"
 * namespace.
 */
static void 
slaxSetNs (slax_data_t *sdp, xmlNodePtr nodep,
	   const char *prefix, const xmlChar *uri, int local)
{
    xmlNsPtr nsp;
    xmlNodePtr root = xmlDocGetRootElement(sdp->sd_docp);

    nsp = xmlSearchNs(sdp->sd_docp, root, (const xmlChar *) prefix);
    if (nsp == NULL) {
	nsp = xmlNewNs(root, uri, (const xmlChar *) prefix);
	if (nsp == NULL) {
	    xmlParserError(sdp->sd_ctxt, "%s:%d: out of memory",
			   sdp->sd_filename, sdp->sd_line);
	    return;
	}

	/*
	 * Since we added this namespace, we need to add it to the
	 * list of extension prefixes.
	 */
	slaxNodeAttribExtend(sdp, root,
			     ATT_EXTENSION_ELEMENT_PREFIXES, prefix);
    }

    /* Add a distinct namespace to the current node */
    nsp = xmlNewNs(nodep, uri, (const xmlChar *) prefix);
    if (local)
	nodep->ns = nsp;
}

/*
 * Find or construct a (possibly temporary) namespace node
 * for the "func" exslt library and put the given node into
 * that namespace.  We also have to add this as an "extension"
 * namespace.
 */
void 
slaxSetFuncNs (slax_data_t *sdp, xmlNodePtr nodep)
{
    const char *prefix = FUNC_PREFIX;
    const xmlChar *uri = FUNC_URI;

    slaxSetNs(sdp, nodep, prefix, uri, TRUE);
}

void
slaxSetSlaxNs (slax_data_t *sdp, xmlNodePtr nodep, int local)
{
    const char *prefix = SLAX_PREFIX;
    const xmlChar *uri = (const xmlChar *) SLAX_URI;

    slaxSetNs(sdp, nodep, prefix, uri, local);
}

void
slaxSetExtNs (slax_data_t *sdp, xmlNodePtr nodep, int local)
{
    const char *prefix = EXT_PREFIX;
    const xmlChar *uri = (const xmlChar *) EXT_URI;

    slaxSetNs(sdp, nodep, prefix, uri, local);
}

/*
 * Backup the stack up 'til the given node is seen
 */
slax_string_t *
slaxStackClear (slax_data_t *sdp UNUSED, slax_string_t **sspp,
		slax_string_t **top)
{
    slax_string_t *ssp;

    while (*sspp == NULL && sspp < top)
	sspp += 1;

    for ( ; sspp <= top; sspp++) {
	ssp = *sspp;
	if (ssp) {
	    slaxStringFree(ssp);
	    *sspp = NULL;
	}
    }

    return NULL;
}

/*
 * Backup the stack up 'til the given node is seen; return the given node.
 */
slax_string_t *
slaxStackClear2 (slax_data_t *sdp, slax_string_t **sspp,
		 slax_string_t **top, slax_string_t **retp)
{
    slax_string_t *ssp;

    ssp = *retp;
    *retp = NULL;
    (void) slaxStackClear(sdp, sspp, top);
    return ssp;
}

/*
 * Add an XSL element to the node at the top of the context stack
 */
xmlNodePtr
slaxElementAdd (slax_data_t *sdp, const char *tag,
	       const char *attrib, const char *value)
{
    xmlNodePtr nodep;

    nodep = xmlNewNode(sdp->sd_xsl_ns, (const xmlChar *) tag);
    if (nodep == NULL) {
	fprintf(stderr, "could not make node: %s\n", tag);
	return NULL;
    }

    slaxAddChildLineNo(sdp->sd_ctxt, sdp->sd_ctxt->node, nodep);

    if (attrib) {
	xmlAttrPtr attr = xmlNewProp(nodep, (const xmlChar *) attrib,
				     (const xmlChar *) value);
	if (attr == NULL)
	    fprintf(stderr, "could not make attribute: %s/@%s\n", tag, attrib);
    }

    return nodep;
}

/*
 * Add an XSL element to the node at the top of the context stack
 */
xmlNodePtr
slaxElementAddString (slax_data_t *sdp, const char *tag,
	       const char *attrib, slax_string_t *value)
{
    xmlNodePtr nodep;

    nodep = xmlNewNode(sdp->sd_xsl_ns, (const xmlChar *) tag);
    if (nodep == NULL) {
	fprintf(stderr, "could not make node: %s\n", tag);
	return NULL;
    }

    slaxAddChildLineNo(sdp->sd_ctxt, sdp->sd_ctxt->node, nodep);

    if (attrib) {
	char *full = slaxStringAsChar(value, 0);
	xmlAttrPtr attr = xmlNewProp(nodep, (const xmlChar *) attrib,
				     (const xmlChar *) full);
	if (attr == NULL)
	    fprintf(stderr, "could not make attribute: %s/@%s\n", tag, attrib);

	xmlFreeAndEasy(full);
    }

    return nodep;
}

/*
 * Add an XSL element to the top of the context stack
 */
xmlNodePtr
slaxElementPush (slax_data_t *sdp, const char *tag,
		 const char *attrib, const char *value)
{
    xmlNodePtr nodep;

    nodep = slaxElementAdd(sdp, tag, attrib, value);
    if (nodep == NULL) {
	xmlParserError(sdp->sd_ctxt, "%s:%d: could not add element '%s'",
		       sdp->sd_filename, sdp->sd_line, tag);
	return NULL;
    }

    nodePush(sdp->sd_ctxt, nodep);
    return nodep;
}

/*
 * Pop an XML element off the context stack
 */
void
slaxElementPop (slax_data_t *sdp)
{
    nodePop(sdp->sd_ctxt);
}
