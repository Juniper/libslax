/*
 * Copyright (c) 2010-2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "slaxparser.h"
#include <ctype.h>
#include <errno.h>

#include <libxml/xpathInternals.h>
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
    if (nodep == NULL || nodep->type == XML_DOCUMENT_NODE)
	return FALSE;

    return (nodep->ns && nodep->ns->href
	    && (name == NULL || streq((const char *) nodep->name, name))
	    && streq((const char *) nodep->ns->href, XSL_URI));
}

/*
 * Extend the existing value for an attribute, appending the given value.
 */
static void
slaxNodeAttribExtend (slax_data_t *sdp, xmlNodePtr nodep,
		      const char *attrib, const char *value, const char *uri)
{
    const xmlChar *uattrib = (const xmlChar *) attrib;
    xmlChar *current = uri ? xmlGetProp(nodep, uattrib)
	: xmlGetNsProp(nodep, uattrib, (const xmlChar *) uri);
    int clen = current ? xmlStrlen(current) + 1 : 0;
    int vlen = strlen(value) + 1;

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

    if (uri == NULL)
	xmlSetProp(nodep, uattrib, newp);
    else {
	xmlNsPtr nsp = xmlSearchNsByHref(sdp->sd_docp, nodep,
					 (const xmlChar *) uri);
	xmlSetNsProp(nodep, nsp, uattrib, newp);
    }
}

/*
 * Extend the existing value for an attribute, appending the given value.
 */
void
slaxAttribExtend (slax_data_t *sdp, const char *attrib, const char *value)
{
    slaxNodeAttribExtend(sdp, sdp->sd_ctxt->node, attrib, value, NULL);
}

/*
 * Extend the existing value for an attribute, appending the given value.
 */
void
slaxAttribExtendXsl (slax_data_t *sdp, const char *attrib, const char *value)
{
    slaxNodeAttribExtend(sdp, sdp->sd_ctxt->node, attrib, value, XSL_URI);
}

/*
 * Find or construct a (possibly temporary) namespace node
 * for the "func" exslt library and put the given node into
 * that namespace.  We also have to add this as an "extension"
 * namespace.
 */
static xmlNsPtr
slaxSetNs (slax_data_t *sdp, xmlNodePtr nodep,
	   const char *prefix, const xmlChar *uri, int local, int is_extension)
{
    xmlNsPtr nsp;
    xmlNodePtr root = xmlDocGetRootElement(sdp->sd_docp);

    nsp = xmlSearchNs(sdp->sd_docp, root, (const xmlChar *) prefix);
    if (nsp == NULL) {
	nsp = xmlNewNs(root, uri, (const xmlChar *) prefix);
	if (nsp == NULL) {
	    xmlParserError(sdp->sd_ctxt, "%s:%d: out of memory",
			   sdp->sd_filename, sdp->sd_line);
	    return NULL;
	}

	/*
	 * Since we added this namespace, we need to add it to the
	 * list of extension prefixes.
	 */
	if (is_extension)
	    slaxNodeAttribExtend(sdp, root,
			     ATT_EXTENSION_ELEMENT_PREFIXES, prefix, NULL);
    }

    if (nodep) {
	/* Add a distinct namespace to the current node */
	nsp = xmlNewNs(nodep, uri, (const xmlChar *) prefix);
	if (local)
	    nodep->ns = nsp;
    }

    return nsp;
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

    slaxSetNs(sdp, nodep, prefix, uri, TRUE, TRUE);
}

xmlNsPtr
slaxSetSlaxNs (slax_data_t *sdp, xmlNodePtr nodep, int local)
{
    const char *prefix = SLAX_PREFIX;
    const xmlChar *uri = (const xmlChar *) SLAX_URI;

    return slaxSetNs(sdp, nodep, prefix, uri, local, TRUE);
}

static int
slaxIsSlaxNs (const char *prefix, int len)
{
    static char sp[] = SLAX_PREFIX;
    if (len != sizeof(sp) - 1)
	return FALSE;
    if (strncmp(sp, prefix, len) != 0)
	return FALSE;
    return TRUE;
}

void
slaxSetExtNs (slax_data_t *sdp, xmlNodePtr nodep, int local)
{
    const char *prefix = EXT_PREFIX;
    const xmlChar *uri = (const xmlChar *) EXT_URI;

    slaxSetNs(sdp, nodep, prefix, uri, local, TRUE);
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

static xmlNsPtr
slaxMakeStdNs (slax_data_t *sdp, const char *name)
{
    char uri[MAXPATHLEN];

    int rc = slaxDynFindPrefix(uri, sizeof(uri), name);
    if (rc < 0)
	return NULL;

    return slaxSetNs(sdp, NULL, name, (const xmlChar *) uri, FALSE, rc);
}

/**
 * Find a namespace definition in the node's parent chain that
 * matches the given prefix.  Since the prefix string lies in situ
 * in the original tag, we give the length of the prefix.  If the
 * prefix has a standard prefix mapping, use that mapping's namespace.
 *
 * @param sdp pointer to main slax data structure
 * @param nodep the node where the search begins
 * @param prefix the prefix string
 * @param len the length of the prefix string
 * @return pointer to the name space 
 */
xmlNsPtr
slaxFindNs (slax_data_t *sdp, xmlNodePtr nodep, const char *prefix, int len)
{
    xmlNsPtr ns;
    char *name;

    name = alloca(len + 1);
    if (name == NULL)
	return NULL;

    memcpy(name, prefix, len);
    name[len] = '\0';
    ns = xmlSearchNs(nodep->doc, nodep, (xmlChar *) name);
    if (ns == NULL)
	ns = slaxMakeStdNs(sdp, name);

    return ns;
}

void
slaxCheckFunction (slax_data_t *sdp, const char *fname)
{
    xmlNodePtr nodep = sdp->sd_ctxt->node;
    xmlNsPtr ns = NULL;
    const char *cp;

    if (sdp->sd_parse == M_PARSE_SLAX
	|| sdp->sd_parse == M_PARSE_FULL
	|| sdp->sd_parse == M_PARSE_PARTIAL) {
	cp = index(fname, ':');
	if (cp) {
	    const char *prefix = fname;
	    int len = cp - prefix;

	    if (!slaxIsSlaxNs(prefix, len)) {
		ns = slaxFindNs(sdp, nodep, prefix, len);
		if (ns == NULL) {
		    sdp->sd_errors += 1;
		    xmlParserError(sdp->sd_ctxt, "unknown prefix '%.*s' in %s",
				   len, prefix, prefix);
		}
	    }
        }
    }
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
	if (!slaxIsSlaxNs(prefix, len)) {
	    ns = slaxFindNs(sdp, nodep, prefix, len);
	    if (ns == NULL) {
		sdp->sd_errors += 1;
		xmlParserError(sdp->sd_ctxt, "unknown prefix '%.*s' in %s",
			       len, prefix, prefix);
	    }
        }
    }

    /* If we don't have a namespace, use the parent's namespace */
    if (ns == NULL)
	ns = slaxFindDefaultNs(sdp);

    nodep = xmlNewDocNode(sdp->sd_docp, ns, (const xmlChar *) tag, NULL);
    if (nodep == NULL) {
	fprintf(stderr, "could not make node: %s\n", tag);
	return;
    }

    slaxLexerAddChild(sdp, NULL, nodep);
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

void
slaxAttribAddSimple (slax_data_t *sdp, const char *name, const char *value)
{
    xmlNsPtr ns = NULL;
    xmlAttrPtr attr;
    const char *cp;

    /*
     * Deal with namespaces.  If there's a prefix, we need to find
     * the definition of this namespace and pass it along.
     */
    cp = index(name, ':');
    if (cp) {
	const char *prefix = name;
	int len = cp - prefix;

	name = cp + 1;
	if (slaxIsSlaxNs(prefix, len)) {
	    ns = slaxSetSlaxNs(sdp, sdp->sd_ctxt->node, FALSE);

	} else {
	    ns = slaxFindNs(sdp, sdp->sd_ctxt->node, prefix, len);
	    if (ns == NULL) {
		sdp->sd_errors += 1;
		xmlParserError(sdp->sd_ctxt, "unknown prefix '%.*s' in %s",
			       len, prefix, prefix);
	    }
        }
    }

    attr = xmlNewNsProp(sdp->sd_ctxt->node, ns, (const xmlChar *) name,
		      (const xmlChar *) value);
    if (attr == NULL)
	fprintf(stderr, "could not make attribute: @%s=%s\n", name, value);
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
    unsigned ss_flags = (style == SAS_SELECT) ? SSF_CONCAT  : 0;

    if (style != SAS_VALUE)
	ss_flags |= SSF_QUOTES;	/* Always want the quotes */

    /* Trigger NOPARENS behavior */
    if (style == SAS_XPATH_NOPARENS)
	ss_flags |= SSF_NOPARENS;

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
	    if (tp)
		slaxLexerAddChild(sdp, NULL, tp);

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
	slaxTernaryExpand(sdp, value, ss_flags);
	buf = slaxStringAsChar(value, SSF_BRACES | ss_flags);
    }

    slaxAttribAddSimple(sdp, name, buf);

    free(buf);
}

/*
 * Add a simple data_value, which is a string may or may not be quoted.
 */
void
slaxAttribAddDataValue (slax_data_t *sdp, const char *name,
			slax_string_t *value)
{
    char *buf, *bp;

    buf = slaxStringAsChar(value, 0);
    if (buf == NULL)
	return;

    bp = buf;
    if (*bp == '"') {		/* Trim quotes */
	bp += 1;
	char *ep = bp + strlen(bp);
	if (ep > bp && ep[-1] == '"')
	    ep[-1] = '\0';
    }

    slaxAttribAddSimple(sdp, name, bp);

    xmlFreeAndEasy(buf);
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
    char *buf;

    buf = slaxStringAsValueTemplate(value, SSF_BRACES);
    if (buf == NULL)
	return;

    slaxAttribAddSimple(sdp, name, buf);

    xmlFreeAndEasy(buf);
}

/*
 * Add an XPath to an attribute on an XML element.  The value consists of
 * one or more items, each of which can be a string, a variable reference,
 * or an xpath expression.  If the value (any items) does not contain an
 * xpath expression, we use the normal attribute substitution to achieve
 * this ({$foo}).  Otherwise, we use the xsl:attribute element to construct
 * the attribute.  Note that this function uses only the SAS_ATTRIB style
 * of quote handling.
 */
void
slaxAttribAddXpath (slax_data_t *sdp, const char *name, slax_string_t *value)
{
    char *buf;

    buf = slaxStringAsValueTemplate(value, SSF_XPATH);
    if (buf == NULL)
	return;

    slaxAttribAddSimple(sdp, name, buf);

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

    attr = xmlSetProp(sdp->sd_ctxt->node, (const xmlChar *) name,
		      (const xmlChar *) val);
    if (attr == NULL)
	fprintf(stderr, "could not make attribute: @%s=%s\n", name, val);
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

    nodep = xmlNewDocNode(sdp->sd_docp, sdp->sd_xsl_ns,
			  (const xmlChar *) tag, NULL);
    if (nodep == NULL) {
	fprintf(stderr, "could not make node: %s\n", tag);
	return NULL;
    }

    slaxLexerAddChild(sdp, NULL, nodep);

    if (attrib) {
	xmlAttrPtr attr = xmlNewProp(nodep, (const xmlChar *) attrib,
				     (const xmlChar *) value);
	if (attr == NULL)
	    fprintf(stderr, "could not make attribute: %s/@%s\n", tag, attrib);
    }

    return nodep;
}


/*
 * Add an XSL element to the node at the top of the context stack, but
 * URL-escape the attribute value.
 */
xmlNodePtr
slaxElementAddEscaped (slax_data_t *sdp, const char *tag,
		       const char *attrib, const char *value)
{
    if (value) {
	char *newurl = alloca(strlen(value) * 3 + 1);
	char *np = newurl;
	const char *up = value;

	while (*up) {
	    if (*up == ' ' || *up == '%') {
		/* Turn ' ' into '%20' */
		*np++ = '%';
		*np++ = ((*up >> 4) & 0x0F) + '0';
		*np++ = (*up & 0x0F) + '0';
		up += 1;

	    } else {
		*np++ = *up++;
	    }
	}

	*np = '\0';
	value = newurl;
    }

    return slaxElementAdd(sdp, tag, attrib, value);
}

/*
 * Add an XSL element to the node at the top of the context stack
 */
static xmlNodePtr
slaxElementAddVar (slax_data_t *sdp, const char *attrib, const char *value)
{
    const char *tag = ELT_VARIABLE;
    xmlNodePtr nodep;
    xmlNodePtr sib;

    /*
     * Check if we're inside a with-param.  If so, we need to move
     * any temporary variables up one level, since we're in a call.
     */
    sib = sdp->sd_ctxt->node;
    if (slaxNodeIsXsl(sib, ELT_WITH_PARAM))
	sib = sib->parent;

    /*
     * Templates limit their initial children to params, so if
     * we're at the beginning of a template, we have to make
     * hidden params.
     */
    if (slaxNodeIsXsl(sib, ELT_PARAM)
		&& slaxNodeIsXsl(sib->parent, ELT_TEMPLATE))
	tag = ELT_PARAM;

    nodep = xmlNewDocNode(sdp->sd_docp, sdp->sd_xsl_ns,
			  (const xmlChar *) tag, NULL);
    if (nodep == NULL) {
	fprintf(stderr, "could not make node: %s\n", tag);
	return NULL;
    }

    xmlAddPrevSibling(sib, nodep);
    nodep->line = sdp->sd_ctxt->node->line;

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

    nodep = xmlNewDocNode(sdp->sd_docp, sdp->sd_xsl_ns,
			  (const xmlChar *) tag, NULL);
    if (nodep == NULL) {
	fprintf(stderr, "could not make node: %s\n", tag);
	return NULL;
    }

    slaxLexerAddChild(sdp, NULL, nodep);

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
 * Add an XSL element to the top of the context stack
 */
static xmlNodePtr
slaxElementPushVar (slax_data_t *sdp, const char *attrib, const char *value)
{
    xmlNodePtr nodep;

    nodep = slaxElementAddVar(sdp, attrib, value);
    if (nodep == NULL) {
	xmlParserError(sdp->sd_ctxt, "%s:%d: could not add element '%s'",
		       sdp->sd_filename, sdp->sd_line, ELT_VARIABLE);
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

/*
 * We've hit a ternary operator (?:) in an otherwise innocent
 * expression.  Ternary expressions are threaded (by
 * slaxTernaryRewrite()) to have ss_concat (overloaded to) point to
 * the next operand in the expression.  We use this to pull out the
 * pieces and expand them into local variables.  The pieces are linked as:
 *    M_TERNARY -> L_QUESTION -> L_COLON -> M_TERNARY_END
 *
 * The process is:
 * - find ternary expression scond, strue, and sfalse
 * - make local variable
 * - make <xsl:choose>
 * - construct :when/:otherwise from scond, strue, and sfalse
 * - replace ternary with variable name
 */
void
slaxTernaryExpand (slax_data_t *sdp, slax_string_t *value,
		   unsigned flags UNUSED)
{
    slax_string_t *ssp, *next;
    slax_string_t *tsp, *qsp, *csp, *esp;
    static const char varfmt[] =
	SLAX_TERNARY_PREFIX "%s" SLAX_TERNARY_COND_SUFFIX;
    static const char condfmt[] = "%s-cond";
    char varname[sizeof(varfmt) + SLAX_TERNARY_VAR_FORMAT_WIDTH];
    char condname[sizeof(varname) + sizeof(condfmt)];
    int no_second_term;
    char *vp;
    unsigned len;

    for (ssp = value; ssp; ssp = next) {
	if (ssp->ss_ttype != M_TERNARY) {
	    next = ssp->ss_next;
	    continue;
	}

	/* Hit a ternary operator */
	tsp = ssp;
	qsp = tsp->ss_concat;
	csp = qsp->ss_concat;
	esp = csp->ss_concat;
	no_second_term = (qsp->ss_next == csp);
	vp = strchr(tsp->ss_token, '(');
	if (vp == NULL) {
	    vp = tsp->ss_token;
	    len = strlen(vp);
	} else {
	    vp += 1;		  /* Trim '(' */
	    len = strlen(vp) - 1; /* Trim ')' */
	}
	if (len >= sizeof(varname) - 1)
	    len = sizeof(varname) - 1;

	strncpy(varname, vp, len);
	varname[len] = '\0';
	slaxLog("ternary expand: %s (%d) -> '%s'", vp, len, varname);

	for (ssp = tsp; ssp && ssp->ss_next != qsp; ssp = ssp->ss_next)
	    continue;
	ssp->ss_next = NULL;

	for (ssp = qsp; ssp && ssp->ss_next != csp; ssp = ssp->ss_next)
	    continue;
	ssp->ss_next = NULL;

	for (ssp = csp; ssp && ssp->ss_next != esp; ssp = ssp->ss_next)
	    continue;
	ssp->ss_next = NULL;

	/*
	 * We've pulled apart the original pieces.  Now we transform
	 * them into a local variable containing an <xsl:choose>
	 * statement.
	 */

	slaxElementPushVar(sdp, ATT_NAME, varname + 1);

	if (no_second_term) {
	    /* Need a local variable to hold the conditional value */
	    snprintf(condname, sizeof(condname), condfmt, varname);

	    slaxElementPush(sdp, ELT_VARIABLE, ATT_NAME, condname + 1);
	    slaxAttribAdd(sdp, 0, ATT_SELECT, tsp->ss_next);
	    slaxElementPop(sdp);
	}

	slaxElementPush(sdp, ELT_CHOOSE, NULL, NULL);
	slaxElementPush(sdp, ELT_WHEN, NULL, NULL);

	if (no_second_term) {
	    slaxAttribAddLiteral(sdp, ATT_TEST, condname);
	    slaxElementPush(sdp, ELT_COPY_OF, NULL, NULL);
	    slaxAttribAddLiteral(sdp, ATT_SELECT, condname);
	    slaxElementPop(sdp); /* Pop the <xsl:copy-of> */

	} else {
	    slaxAttribAdd(sdp, 0, ATT_TEST, tsp->ss_next);
	    slaxElementPush(sdp, ELT_COPY_OF, NULL, NULL);
	    slaxAttribAdd(sdp, 0, ATT_SELECT, qsp->ss_next);
	    slaxElementPop(sdp); /* Pop the <xsl:copy-of> */
	}

	slaxElementPop(sdp);	/* Pop the <xsl:when> */

	slaxElementPush(sdp, ELT_OTHERWISE, NULL, NULL);
	slaxElementPush(sdp, ELT_COPY_OF, NULL, NULL);
	slaxAttribAdd(sdp, 0, ATT_SELECT, csp->ss_next);
	slaxElementPop(sdp);	/* Pop the <xsl:copy-of> */
	slaxElementPop(sdp);	/* Pop the <xsl:otherwise> */
	slaxElementPop(sdp);	/* Pop the <xsl:choose> */
	slaxElementPop(sdp);	/* Pop the <xsl:variable> */

	/* esp->ss_next can point to a trailing expression */
	slaxStringFree(tsp->ss_next);
	next = tsp->ss_next = esp->ss_next;
	esp->ss_next = NULL;

	tsp->ss_ttype = T_VAR;	/* Recast as normal variable */
	tsp->ss_concat = NULL;

	slaxStringFree(qsp);
	slaxStringFree(csp);
	slaxStringFree(esp);
    }
}

/*
 * In libxslt while evaluating global variables, when a global
 * variable needs to evaluate another global variable it deletes the
 * node-set returned by the extension function. For example,
 *
 * var $var1 = jcs:split();
 * var $var2 = $var1;
 *
 * In the above if $var2 is evaluated before $var1 (libxslt uses
 * hashes for evaluation, so the evaluation order depends on the name
 * of the variables) while evaluating $var2, since $var1 is not
 * evaluated yet, it will be recursively evaluated, in this scenario
 * the node-set returned by slax:split() is getting deleted.
 *
 * This is PR in libxslt 
 * https://bugzilla.gnome.org/show_bug.cgi?id=495995
 * 
 * The fix in libxslt for this is calling 
 * xsltExtensionInstructionResultRegister() in the extension functions.
 *
 * This function set the preserve flag so that the garbage collector
 * won't free the RVT (which has the node-set), till the execution of
 * template is over.
 *
 * template my_templ() {
 *     for-each (ten-iteration) {
 *         var $ret = jcs:invoke('get-software-information');
 *     }
 * }
 *
 * The node-set returned by the jcs:invoke() in all the ten-iterations
 * will be finally freed only when the execution of whole 'my_templ'
 * is over.  Unnecessarily it is occupying the space for node-set
 * returned in each iteration even after the scope is over. This may
 * be problem for our high-intensity scripts, where a single for-each
 * can have multiple iterations and each iteration jcs:invoke can
 * return huge node-set.
 *
 * The fix we are taking to this is to call
 * xsltExtensionInstructionResultRegister() only for global
 * variables. So, while executing the extension functions we need to
 * check whether it is evaluated for global or local variable. There
 * is no function in libxslt which tells us this. The transformation
 * context has member to hold the local variable, this member will be
 * NULL when evaluating global variables, we can use that to check
 * whether the evaluation is for global or local.  All the global
 * variables will be evaluated before any local variable
 * evaluation. With the node-set returned for the global var will be
 * deleted in the final stage when full execution of script is over.
 *
 * Set the preserve bit only for gloabl variables.
 */
void
slaxSetPreserveFlag (xsltTransformContextPtr tctxt, xmlXPathObjectPtr ret)
{
    xmlNodePtr parent;

    if (tctxt->vars) {
	if (tctxt == NULL || tctxt->inst == NULL)
	    return;

	parent = tctxt->inst->parent;
	if (parent == NULL || parent->type != XML_ELEMENT_NODE)
	    return;
	
	if (!slaxNodeIsXsl(parent, ELT_STYLESHEET))
	    return;
    }

    xsltExtensionInstructionResultRegister(tctxt, ret);
}

/*
 * The W3C XSLT spec requires that imports preceed just about
 * anything else.  We want to be somewhat more flexible, so
 * we move them up as needed.
 */
void
slaxMoveImport (slax_data_t *sdp UNUSED, xmlNodePtr curp)
{
    xmlNodePtr nodep, prev = NULL;

    for (nodep = curp; nodep; nodep = nodep->prev) {
	if (nodep->type == XML_COMMENT_NODE)
	    continue;
	if (nodep->prev == NULL)
	    break;
	if (nodep->prev->type == XML_COMMENT_NODE)
	    continue;;
	prev = nodep->prev;
	if (slaxNodeIsXsl(prev, ELT_IMPORT)) {
	    prev = prev->next;
	    break;
	}
    }

    if (prev && prev != curp) {
	xmlUnlinkNode(curp);
	xmlAddPrevSibling(prev, curp);
    }
}

xmlXPathObjectPtr
slaxXpathEval (xmlNodePtr node, xmlNodePtr inst, xmlXPathContextPtr xpctxt,
	       xsltStylesheetPtr script, const char *expr)
{
    struct {
	xmlDocPtr o_doc;
	xmlNsPtr *o_nslist;
	xmlNodePtr o_node;
	int o_position;
	int o_contextsize;
	int o_nscount;
    } old;

    xmlNsPtr *nsList;
    int nscount;
    xmlXPathCompExprPtr comp;
    xmlXPathObjectPtr res;

    char *sexpr = NULL;

    
    sexpr = slaxSlaxToXpath("select", 1, (const char *) expr, NULL);
    if (sexpr)
	expr = sexpr;

    comp = xsltXPathCompile(script, (const xmlChar *) expr);
    if (comp == NULL) {
	xmlFreeAndEasy(sexpr);
	return NULL;
    }

    nsList = xmlGetNsList(inst->doc, inst);
    for (nscount = 0; nsList && nsList[nscount]; nscount++)
	continue;
    
    /* Save old values */
    old.o_doc = xpctxt->doc;
    old.o_node = xpctxt->node;
    old.o_position = xpctxt->proximityPosition;
    old.o_contextsize = xpctxt->contextSize;
    old.o_nscount = xpctxt->nsNr;
    old.o_nslist = xpctxt->namespaces;

    /* Fill in context */
    xpctxt->node = node;
    xpctxt->namespaces = nsList;
    xpctxt->nsNr = nscount;

    /* Run the compiled expression */
    res = xmlXPathCompiledEval(comp, xpctxt);

    /* Restore saved values */
    xpctxt->doc = old.o_doc;
    xpctxt->node = old.o_node;
    xpctxt->contextSize = old.o_contextsize;
    xpctxt->proximityPosition = old.o_position;
    xpctxt->nsNr = old.o_nscount;
    xpctxt->namespaces = old.o_nslist;

    xmlFreeAndEasy(sexpr);
    xmlXPathFreeCompExpr(comp);
    xmlFree(nsList);

    return res;
}

xmlNodeSetPtr
slaxXpathSelect (xmlDocPtr docp, xmlNodePtr nodep, const char *expr)
{
    xmlXPathObjectPtr objp;
    xmlNodeSetPtr results = NULL;
    xmlXPathContextPtr xpath_context;
    xmlNsPtr nsp, *nsList;
    int nscount;
    char *sexpr = NULL;

    /* Translate a SLAX XPath into a native one */
    sexpr = slaxSlaxToXpath("select", 1, (const char *) expr, NULL);
    if (sexpr)
	expr = sexpr;

    /*
     * Some XPath initialization: Create xpath evaluation context
     */
    xpath_context = xmlXPathNewContext(docp);
    if (xpath_context == NULL) {
	xmlFreeAndEasy(sexpr);
	return NULL;
    }

    xmlXPathRegisterNs(xpath_context, (const xmlChar *) XSL_PREFIX,
                       (const xmlChar *) XSL_URI);

    nsList = xmlGetNsList(docp, nodep);
    for (nscount = 0; nsList && nsList[nscount]; nscount++) {
	nsp = nsList[nscount];
	if (nsp->href == NULL)
	    continue;
	if (streq((const char *) nsp->href, XSL_URI))
	    continue;
	xmlXPathRegisterNs(xpath_context, nsp->prefix, nsp->href);
    }

    if (nodep == NULL)
        nodep = xmlDocGetRootElement(docp);

    xpath_context->node = nodep;

    objp = xmlXPathEvalExpression((const xmlChar *) expr, xpath_context);
    if (objp == NULL) {
        LX_ERR("could not evaluate xpath expression: %s", expr);
	xmlFreeAndEasy(sexpr);
        return NULL;
    }

    if (objp->type == XPATH_NODESET) {
        results = objp->nodesetval;
        objp->nodesetval = NULL; /* Prevent double free */
    }

    xmlXPathFreeContext(xpath_context);
    xmlXPathFreeObject(objp);
    xmlFreeAndEasy(sexpr);

    return results;
}

void
slaxDumpTree (xmlNodePtr node, const char *pref, int indent)
{
    int lineno;

    for ( ; node; node = node->next) {
	lineno = xmlGetLineNo(node);

	slaxOutput("%*s%s element '%s' line %d", indent, "", pref ?: "",
		   node->name ? (const char *) node->name : "--", lineno);

	xmlAttrPtr attr;
	for (attr = node->properties; attr; attr = attr->next) {
	    slaxOutput("%*s%s attr '%s'", indent + 4, "", pref ?: "",
		   attr->name ? (const char *) attr->name : "--");
	}

	if (node->children)
	    slaxDumpTree(node->children, pref, indent + 2);
    }
}
