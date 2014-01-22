/*
 * Copyright (c) 2006-2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * slaxloader.c -- load slax files into XSL trees
 *
 * This file contains the lexer for slax, as well as the utility
 * functions called by the productions in slaxparser.y.  It also
 * contains the hooks to attach the slax parser into the libxslt
 * parsing mechanism.
 */

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "slaxparser.h"
#include <ctype.h>
#include <sys/queue.h>
#include <errno.h>

#include <libxslt/extensions.h>
#include <libxslt/documents.h>
#include <libexslt/exslt.h>
#include <libslax/slaxdata.h>
#include <libslax/slaxdyn.h>

static xsltDocLoaderFunc slaxOriginalXsltDocDefaultLoader;
xmlExternalEntityLoader slaxOriginalEntityLoader;

static int slaxEnabled;		/* Global enable (SLAX_*) */

/* Stub to handle xmlChar strings in "?:" expressions */
const xmlChar slaxNull[] = "";

static slax_data_list_t slaxIncludes;
static int slaxIncludesInited;

/*
 * Add a directory to the list of directories searched for files
 */
void
slaxIncludeAdd (const char *dir)
{
    if (!slaxIncludesInited) {
	slaxIncludesInited = TRUE;
	slaxDataListInit(&slaxIncludes);
    }

    slaxDataListAddNul(&slaxIncludes, dir);
}

/*
 * Add a set of directories to the list of directories searched for files
 */
void
slaxIncludeAddPath (const char *dir)
{
    char *buf = NULL;
    int buflen = 0;
    const char *cp;

    while (dir && *dir) {
	cp = strchr(dir, ':');
	if (cp == NULL) {
	    slaxIncludeAdd(dir);
	    break;
	}

	if (cp - dir > 1) {
	    if (buflen < cp - dir + 1) {
		buflen = cp - dir + 1 + BUFSIZ;
		buf = alloca(buflen);
	    }

	    memcpy(buf, dir, cp - dir);
	    buf[cp - dir] = '\0';

	    slaxIncludeAdd(buf);
	}

	if (*cp == '\0')
	    break;
	dir = cp + 1;
    }
}

/**
 * Check the version string.
 *
 * @param major major version number
 * @param minor minor version number
 */
void
slaxVersionMatch (slax_data_t *sdp, const char *vers)
{
    if (streq(vers, "1.0") || streq(vers, "1.1") || streq(vers, "1.2"))
	return;

    fprintf(stderr, "invalid version number: %s\n", vers);
    sdp->sd_errors += 1;
}

/*
 * Relocate the most-recent "sort" node, if the parent was a "for"
 * loop.  The "sort" should be at the top of the stack.  If the parent
 * is a "for", then the parser will have built two nested "for-each"
 * loops, and we really want the sort to apply to the parent (outer)
 * loop.  So if we've hitting these conditions, move the sort node.
 */
void
slaxRelocateSort (slax_data_t *sdp)
{
    xmlNodePtr nodep = sdp->sd_ctxt->node;

    if (nodep && nodep->parent) {
	xmlNodePtr parent = nodep->parent;
        xmlChar *sel = xmlGetProp(parent, (const xmlChar *) ATT_SELECT);

	if (sel && strncmp((char *) sel, "$slax-dot-", 10) == 0) {
	    slaxLog("slaxRelocateSort: %s:%d: must relocate",
		      nodep->name, xmlGetLineNo(nodep));
	    /*
	     * "parent" is the inner for-each loop, and "parent->parent"
	     * is the outer for-each loop.  Add the sort as the first
	     * child of the outer loop.  xmlAddPrevSibling will unlink
	     * nodep from its current location.
	     */
	    xmlAddPrevSibling(parent->parent->children, nodep);
	}

	xmlFreeAndEasy(sel);
    }
}

/*
 * Add an xsl:comment node
 */
xmlNodePtr
slaxCommentAdd (slax_data_t *sdp, slax_string_t *value)
{
    xmlNodePtr nodep;
    nodep = slaxElementAdd(sdp, ELT_COMMENT, NULL, NULL);
    if (nodep) {
	if (slaxStringIsSimple(value, T_QUOTED)) {
	    xmlNodePtr tp = xmlNewText((const xmlChar *) value->ss_token);

	    if (tp)
		slaxAddChild(sdp, nodep, tp);

	} else {
	    xmlNodePtr attrp;
	    nodePush(sdp->sd_ctxt, nodep);

	    attrp = slaxElementAdd(sdp, ELT_VALUE_OF, NULL, NULL);
	    if (attrp) {
		nodePush(sdp->sd_ctxt, attrp);
		slaxAttribAdd(sdp, SAS_SELECT, ATT_SELECT, value);
		nodePop(sdp->sd_ctxt);
	    }
	    nodePop(sdp->sd_ctxt);
	}
    }

    return nodep;
}

/*
 * If we know we're about to assign a result tree fragment (RTF)
 * to a variable, punt and do The Right Thing.
 *
 * When we're called, the variable statement has ended, so the
 * <xsl:variable> should be the node on the top of the stack.
 * If there's something inside the element (as opposed to a
 * "select" attribute or no content), then this must be building
 * an RTF, which we dislike.  Instead we convert the current variable
 * into a temporary variable, and build the real variable using a
 * call to "ext:node-set()".  We'll need define the "ext" namespace
 * on the same (new) element, so we can ensure that it's been created.
 */
void
slaxAvoidRtf (slax_data_t *sdp)
{
    static const char new_value_format[] = EXT_PREFIX ":node-set($%s)";
    static const char node_value_format[] = EXT_PREFIX ":node-set(%s)";
    static const char temp_name_format[] = "%s-temp-%u";

    static unsigned temp_count;

    xmlNodePtr nodep = sdp->sd_ctxt->node;
    xmlNodePtr newp;
    xmlChar *name;
    char *temp_name, *value, *old_value;
    const char *format;
    int clen, vlen;
    xmlChar *sel;

    /*
     * Parameters and variables can be assigned values in two ways,
     * via the 'select' attribute and via contents of the element
     * itself.  In most cases, the select attribute is not an RTF
     * and doesn't need the node-set() functionality.  The main exception
     * is <func:function>s, which are cursed to return RTFs.  Since
     * we can't "know" if the expression is a func:function and since
     * the cost of node-set() is minimal, we just wrap the select
     * expression in a node-set call.
     */
    sel = xmlGetProp(nodep, (const xmlChar *) ATT_SELECT);
    if (sel) {
	if (nodep->children != NULL) {
	    xmlParserError(sdp->sd_ctxt,
			   "%s:%d: %s cannot have both select and children\n",
			   sdp->sd_filename, sdp->sd_line, nodep->name);
	    return;
	}

	format = node_value_format;
	old_value = (char *) sel;
	vlen = strlen(old_value);

    } else {

	name = xmlGetProp(nodep, (const xmlChar *) ATT_NAME);
	clen = name ? xmlStrlen(name) + 1 : 0;

	/*
	 * Generate the new temporary variable name using the original
	 * name, the string "-temp-", and a monotonically increasing
	 * number.
	 */
	vlen = clen + strlen(temp_name_format) + 10 + 1; /* 10 is max %u */
	temp_name = alloca(vlen);
	snprintf(temp_name, vlen, temp_name_format, name, ++temp_count);

	(void) xmlSetProp(sdp->sd_ctxt->node, (const xmlChar *) ATT_NAME,
			  (xmlChar *) temp_name);

	/*
	 * Now generate the new element, using the original, user-provided
	 * name and a call to "slax:node-set" function.
	 */
	newp = xmlNewDocNode(sdp->sd_docp, sdp->sd_xsl_ns, nodep->name, NULL);
	if (newp == NULL) {
	    fprintf(stderr, "could not make node: %s\n", nodep->name);
	    xmlFreeAndEasy(name);
	    return;
	}

	xmlAddChild(nodep->parent, newp);

	/* Use the line number from the original node */
	if (sdp->sd_ctxt->linenumbers)
	    newp->line = nodep->line;

	xmlNewProp(newp, (const xmlChar *) ATT_NAME, (const xmlChar *) name);

	format = new_value_format;
	old_value = temp_name;
	nodep = newp;
	xmlFreeAndEasy(name);
    }

    /* Build the value for the new variable */
    vlen += strlen(format) + 1;
    value = alloca(vlen);
    snprintf(value, vlen, format, old_value);

    slaxLog("AvoidRTF: '%s'", value);

    xmlSetNsProp(nodep, NULL, (const xmlChar *) ATT_SELECT, (xmlChar *) value);

    /* Add the namespace for 'slax' */
    slaxSetExtNs(sdp, nodep, FALSE);

    xmlFreeAndEasy(sel);
}

/**
 * Create namespace alias between the prefix given and the
 * containing (current) prefix.
 *
 * @param sdp main slax data structure
 * @param style stylesheet prefix value (as declared in the "ns" statement)
 * @param results result prefix value
 */
void
slaxNamespaceAlias (slax_data_t *sdp, slax_string_t *style,
		    slax_string_t *results)
{
    const char *alias = style->ss_token;

    xmlNodePtr nodep;
    nodep = slaxElementPush(sdp, ELT_NAMESPACE_ALIAS,
			    ATT_STYLESHEET_PREFIX, alias);

    if (nodep) {
	slaxAttribAddString(sdp, ATT_RESULT_PREFIX, results, 0);
	slaxElementPop(sdp);
    }
}

/**
 * Add an XPath expression as a statement.  If this is a string,
 * we place it inside an <xsl:text> element.  Otherwise we
 * put the value inside an <xsl:value-of>'s select attribute.
 *
 * @param sdp main slax data structure
 * @param value XPath expression to add
 * @param text_as_elt always use <xsl:text> for text data
 * @param disable_escaping add the "disable-output-escaping" attribute
 */
void
slaxElementXPath (slax_data_t *sdp, slax_string_t *value,
		  int text_as_elt, int disable_escaping)
{
    char *buf;
    xmlNodePtr nodep;
    xmlAttrPtr attr;

    if (slaxStringIsSimple(value, T_QUOTED)) {
	char *cp = value->ss_token;
	int len = strlen(cp);

	/*
	 * if the text string has leading or trailing whitespace.
	 * then we must use a text element.
	 */
	if (disable_escaping)
	    text_as_elt = TRUE;
	else if (*cp == '\t' || *cp == ' ' || *cp == '\0')
	    text_as_elt = TRUE;
	else {
	    char *ep = cp + len - 1;
	    if (*ep == '\t' || *ep == ' ')
		text_as_elt = TRUE;
	}

	nodep = xmlNewText((const xmlChar *) cp);
	if (nodep == NULL)
	    fprintf(stderr, "could not make node: text\n");
	else if (text_as_elt) {
	    xmlNodePtr textp = xmlNewDocNode(sdp->sd_docp, sdp->sd_xsl_ns,
					     (const xmlChar *) ELT_TEXT, NULL);
	    if (textp == NULL) {
		fprintf(stderr, "could not make node: %s\n", ELT_TEXT);
		return;
	    }

	    if (disable_escaping) {
		xmlNewNsProp(textp, NULL,
			     (const xmlChar *) ATT_DISABLE_OUTPUT_ESCAPING,
			     (const xmlChar *) "yes");

	    }

	    slaxAddChild(sdp, textp, nodep);
	    xmlAddChild(sdp->sd_ctxt->node, textp);

	} else {
	    slaxAddChild(sdp, NULL, nodep);
	}

	return;
    }

    nodep = slaxElementPush(sdp, ELT_VALUE_OF, NULL, NULL);
    if (nodep == NULL)
	return;

    /* If we need the "slax" namespace, add it to the value-of node */
    if (slaxNeedsSlaxNs(value))
	slaxSetSlaxNs(sdp, nodep, FALSE);

    slaxTernaryExpand(sdp, value, 0);
    buf = slaxStringAsChar(value, SSF_CONCAT | SSF_QUOTES);
    if (buf == NULL) {
	xmlParserError(sdp->sd_ctxt,
		       "%s:%d: could not make attribute string: @%s=%s",
		       sdp->sd_filename, sdp->sd_line, ATT_SELECT, buf);
	slaxElementPop(sdp);
	return;
    }

    attr = xmlNewProp(nodep, (const xmlChar *) ATT_SELECT,
		      (const xmlChar *) buf);
    if (attr == NULL) {
	xmlParserError(sdp->sd_ctxt,
		       "%s:%d: could not make attribute: @%s=%s",
		       sdp->sd_filename, sdp->sd_line, ATT_SELECT, buf);
	xmlFree(buf);
	slaxElementPop(sdp);
	return;
    }

    xmlFree(buf);

    if (disable_escaping) {
	xmlNewNsProp(nodep, NULL,
		     (const xmlChar *) ATT_DISABLE_OUTPUT_ESCAPING,
		     (const xmlChar *) "yes");
    }

    slaxElementPop(sdp);
}

/*
 * Check to see if an "if" statement had no "else".  If so,
 * we can rewrite it from an "xsl:choose" into an "xsl:if".
 */
void
slaxCheckIf (slax_data_t *sdp, xmlNodePtr choosep)
{
    xmlNodePtr nodep;
    int count = 0;

    if (choosep == NULL)
	return;

    for (nodep = choosep->children; nodep; nodep = nodep->next) {
	if (count++ > 0)
	    return;

	if (nodep->type != XML_ELEMENT_NODE)
	    return;

	if (!streq((const char *) nodep->name, ELT_WHEN))
	    return;
    }

    /*
     * So we know that choosep has exactly one child that is
     * an "xsl:when".  We need to turn the "when" into an "if"
     * and re-parent it under the current context node.
     */
    nodep = choosep->children;

    xmlUnlinkNode(nodep);
    if (!xmlDictOwns(sdp->sd_ctxt->dict, nodep->name))
	xmlFree(const_drop(nodep->name));

    nodep->name = xmlDictLookup(sdp->sd_ctxt->dict,
				(const xmlChar *) ELT_IF, -1);
    if (nodep->name == NULL)
	return;

    xmlUnlinkNode(choosep);
    xmlFreeNode(choosep);
    xmlAddChild(sdp->sd_ctxt->node, nodep);
}

void 
slaxHandleEltArgPrep (slax_data_t *sdp)
{
    static const char varfmt[] = SLAX_ELTARG_FORMAT;
    static unsigned varcount;
    char varname[sizeof(varfmt) + SLAX_ELTARG_WIDTH];

    snprintf(varname, sizeof(varname), varfmt, ++varcount);

    slaxLog("slaxHandleEltArgPrep: '%s'", varname);

    slaxElementPush(sdp, ELT_VARIABLE, ATT_NAME, varname);
}

xmlNodePtr
slaxHandleEltArgSafeInsert (xmlNodePtr base)
{
    const char *name;
    xmlNodePtr nodep = base;

    for ( ; nodep; nodep = nodep->parent) {
	if (!slaxNodeIsXsl(base, NULL))
	    break;

	name = (const char *) nodep->name;

	if (streq(name, ELT_VARIABLE))
	    return nodep;

	/* These statements can't contain variables */
	if (!(streq(name, ELT_WITH_PARAM)
	      || streq(name, ELT_WHEN)
	      || streq(name, ELT_OTHERWISE)))
	    break;
    }

    if (base == nodep)
	return NULL;

    return nodep;
}

static void
slaxPrintNode (const char *txt, xmlNodePtr nodep)
{
    const char *localname = (const char *) nodep->name;
    xmlNodePtr parent = nodep->parent;
    const char *plocalname = NULL;
    char *name = slaxGetAttrib(nodep, ATT_NAME);
    char *pname = NULL;

    if (parent) {
	plocalname = (const char *) parent->name;
	pname = slaxGetAttrib(parent, ATT_NAME);
    }

    slaxLog("%s (<%s @name=%s> parent=<%s @name=%s>)",
	    txt, localname, name ?: "", plocalname ?: "", pname ?: "");

    xmlFreeAndEasy(pname);
    xmlFreeAndEasy(name);
}

slax_string_t *
slaxHandleEltArg (slax_data_t *sdp, int var_on_stack)
{
    static const char new_value_format[] = EXT_PREFIX ":node-set($%s)";
    char str[BUFSIZ];
    char *varname;
    xmlNodePtr nodep = NULL;
    slax_string_t *ssp;
    xmlNodePtr varp;
    xmlNodePtr insert;

    if (!var_on_stack) {
	slaxHandleEltArgPrep(sdp);
    }
    varp = nodePop(sdp->sd_ctxt);
    if (varp == NULL)
	return NULL;

    xmlUnlinkNode(varp);

    varname = slaxGetAttrib(varp, ATT_NAME);
    slaxLog("slaxHandleEltArg: varname '%s', var_on_stack %s",
	    varname, var_on_stack ? "yes" : "no");
    slaxPrintNode("slaxHandleEltArg: varp", varp);

    if (var_on_stack) {
	nodep = sdp->sd_ctxt->node;
	if (nodep == NULL)
	    return NULL;

    } else {
	nodep = nodePop(sdp->sd_ctxt);
	if (nodep == NULL)
	    return NULL;

	xmlUnlinkNode(nodep);
	xmlAddChild(varp, nodep);
    }

    insert = slaxHandleEltArgSafeInsert(sdp->sd_ctxt->node);

    slaxLog("slaxHandleEltArg: insert of '%s' is '%s'",
	    (const char *) sdp->sd_ctxt->node->name,
	    insert ? (const char *) insert->name : "");

    if (insert) {
	slaxPrintNode("slaxHandleEltArg: insert", insert);
	slaxSetExtNs(sdp, sdp->sd_ctxt->node, FALSE);
	xmlAddPrevSibling(insert, varp);

    } else {
	/*
	 * Add this to a list of delayed nodes that will be inserted
	 * before the next node added to the parse tree.
	 */
	slaxAddInsert(sdp, varp);
    }

    /* Use the line number from the original node */
    if (sdp->sd_ctxt->linenumbers)
	varp->line = nodep->line;

    snprintf(str, sizeof(str), new_value_format, varname);
    ssp = slaxStringLiteral(str, T_BARE);

    return ssp;
}

/*
 * We are cleaning up after a "main <elt> { ... }" statement, where
 * we aren't sure if the optional element is on the stack.  So if the
 * top item isn't a template, then we pop it.
 */
void
slaxMainElement (slax_data_t *sdp)
{
    slaxLog("slaxMainElement: %p", sdp);

    if (!slaxNodeIsXsl(sdp->sd_ctxt->node, ELT_TEMPLATE))
	slaxElementPop(sdp);
    slaxElementPop(sdp);
}

/*
 * Build the initial document, with the heading and top-level element.
 */
static xmlDocPtr
slaxBuildDoc (slax_data_t *sdp, xmlParserCtxtPtr ctxt)
{
    xmlDocPtr docp;
    xmlNodePtr nodep;

    docp = xmlNewDoc((const xmlChar *) XML_DEFAULT_VERSION);
    if (docp == NULL)
	return NULL;

    docp->standalone = 1;
    if (ctxt->dict)
	docp->dict = ctxt->dict;
    else {
	docp->dict = xmlDictCreate();
	if (ctxt->dict == NULL)
	    ctxt->dict = docp->dict;
    }

    nodep = xmlNewDocNode(docp, NULL, (const xmlChar *) ELT_STYLESHEET, NULL);
    if (nodep) {

	sdp->sd_xsl_ns = xmlNewNs(nodep, (const xmlChar *) XSL_URI,
				  (const xmlChar *) XSL_PREFIX);
	xmlSetNs(nodep, sdp->sd_xsl_ns); /* Noop if NULL */

	xmlDocSetRootElement(docp, nodep);
	nodePush(ctxt, nodep);

	xmlAttrPtr attr = xmlNewDocProp(docp, (const xmlChar *) ATT_VERSION,
			  (const xmlChar *) XSL_VERSION);
	if (attr)
	    xmlAddProp(nodep, attr);
    }

    if (ctxt->dict) {
	docp->dict = ctxt->dict;
	xmlDictReference(docp->dict);
    }

    return docp;
}

void
slaxDataCleanup (slax_data_t *sdp)
{
    if (sdp->sd_buf) {
	xmlFree(sdp->sd_buf);
	sdp->sd_buf = NULL;
	sdp->sd_cur = sdp->sd_size = 0;
    }

    sdp->sd_ns = NULL;		/* We didn't allocate this */

    if (sdp->sd_ctxt) {
	xmlFreeParserCtxt(sdp->sd_ctxt);
	sdp->sd_ctxt = NULL;
    }

    if (sdp->sd_docp) {
	xmlFreeDoc(sdp->sd_docp);
	sdp->sd_docp = NULL;
    }
}

/**
 * Read a SLAX file from an open file pointer
 *
 * @param filename Name of the file (or "-")
 * @param file File pointer for input
 * @param dict libxml2 dictionary
 * @param partial TRUE if parsing partial SLAX contents
 * @return xml document pointer
 */
xmlDocPtr
slaxLoadFile (const char *filename, FILE *file, xmlDictPtr dict, int partial)
{
    slax_data_t sd;
    xmlDocPtr res;
    int rc;
    xmlParserCtxtPtr ctxt = xmlNewParserCtxt();

    if (ctxt == NULL)
	return NULL;

    /*
     * Turn on line number recording in each node
     */
    ctxt->linenumbers = 1;

    if (dict) {
	if (ctxt->dict)
	    xmlDictFree(ctxt->dict);

    	ctxt->dict = dict;
 	xmlDictReference(ctxt->dict);
    }

    bzero(&sd, sizeof(sd));

    /* We want to parse SLAX, either full or partial */
    sd.sd_parse = sd.sd_ttype = partial ? M_PARSE_PARTIAL : M_PARSE_FULL;

    strncpy(sd.sd_filename, filename, sizeof(sd.sd_filename));
    sd.sd_file = file;

    sd.sd_ctxt = ctxt;

    ctxt->version = xmlCharStrdup(XML_DEFAULT_VERSION);
    ctxt->userData = &sd;

    /*
     * Fake up an inputStream so the error mechanisms will work
     */
    if (filename)
	xmlSetupParserForBuffer(ctxt, (const xmlChar *) "", filename);

    sd.sd_docp = slaxBuildDoc(&sd, ctxt);
    if (sd.sd_docp == NULL) {
	slaxDataCleanup(&sd);
	return NULL;
    }

    if (filename != NULL)
        sd.sd_docp->URL = (xmlChar *) xmlStrdup((const xmlChar *) filename);

    rc = slaxParse(&sd);

    if (sd.sd_errors) {
	slaxError("%s: %d error%s detected during parsing (%d)\n",
	  sd.sd_filename, sd.sd_errors, (sd.sd_errors == 1) ? "" : "s", rc);

	slaxDataCleanup(&sd);
	return NULL;
    }

    /* Save docp before slaxDataCleanup nukes it */
    res = sd.sd_docp;
    sd.sd_docp = NULL;
    slaxDataCleanup(&sd);

    slaxDynLoad(res);		/* Check dynamic extensions */

    return res;
}

/**
 * Read a SLAX file from a memory buffer
 *
 * @param filename Name of the file (or "-")
 * @param input Input data
 * @param dict libxml2 dictionary
 * @param partial TRUE if parsing partial SLAX contents
 * @return xml document pointer
 */
xmlDocPtr
slaxLoadBuffer (const char *filename, char *input,
		xmlDictPtr dict, int partial)
{
    slax_data_t sd;
    xmlDocPtr res;
    int rc;
    xmlParserCtxtPtr ctxt = xmlNewParserCtxt();

    if (ctxt == NULL)
	return NULL;

    /*
     * Turn on line number recording in each node
     */
    ctxt->linenumbers = 1;

    if (dict) {
	if (ctxt->dict)
	    xmlDictFree(ctxt->dict);

    	ctxt->dict = dict;
 	xmlDictReference(ctxt->dict);
    }

    bzero(&sd, sizeof(sd));

    /* We want to parse SLAX, either full or partial */
    sd.sd_parse = sd.sd_ttype = partial ? M_PARSE_PARTIAL : M_PARSE_FULL;

    strncpy(sd.sd_filename, filename, sizeof(sd.sd_filename));
    sd.sd_ctxt = ctxt;

    /*
     * Fake up an inputStream so the error mechanisms will work
     */
    xmlSetupParserForBuffer(ctxt, (const xmlChar *) "", filename);

    ctxt->version = xmlCharStrdup(XML_DEFAULT_VERSION);
    ctxt->userData = &sd;

    sd.sd_docp = slaxBuildDoc(&sd, ctxt);
    if (sd.sd_docp == NULL) {
	slaxDataCleanup(&sd);
	return NULL;
    }

    sd.sd_buf = input;
    sd.sd_len = strlen(input);

    if (filename != NULL)
        sd.sd_docp->URL = (xmlChar *) xmlStrdup((const xmlChar *) filename);

    rc = slaxParse(&sd);

    if (sd.sd_errors) {
	slaxError("%s: %d error%s detected during parsing (%d)\n",
	  sd.sd_filename, sd.sd_errors, (sd.sd_errors == 1) ? "" : "s", rc);

	slaxDataCleanup(&sd);
	return NULL;
    }

    /* Save docp before slaxDataCleanup nukes it */
    res = sd.sd_docp;
    sd.sd_docp = NULL;
    slaxDataCleanup(&sd);

    slaxDynLoad(res);		/* Check dynamic extensions */

    return res;
}

/*
 * Turn a SLAX expression into an XPath one.  Returns a freshly
 * allocated string, or NULL.
 */
char *
slaxSlaxToXpath (const char *filename, int lineno,
		 const char *slax_expr, int *errorsp)
{
    slax_data_t sd;
    int rc, errors = 0;
    xmlParserCtxtPtr ctxt;
    xmlNodePtr fakep = NULL;
    char *buf;

    if (errorsp)
	*errorsp = 0;

    if (slax_expr == NULL || *slax_expr == '\0')
	return (char *) xmlCharStrdup("\"\"");

    ctxt = xmlNewParserCtxt();
    if (ctxt == NULL)
	return NULL;

    bzero(&sd, sizeof(sd));
    sd.sd_parse = sd.sd_ttype = M_PARSE_SLAX; /* We want to parse SLAX */
    sd.sd_ctxt = ctxt;
    sd.sd_flags |= SDF_NO_SLAX_KEYWORDS;

    ctxt->version = xmlCharStrdup(XML_DEFAULT_VERSION);
    ctxt->userData = &sd;

    /*
     * Fake up an inputStream so the error mechanisms will work
     */
    xmlSetupParserForBuffer(ctxt, (const xmlChar *) "", filename);
    sd.sd_line = lineno;

    fakep = xmlNewNode(NULL, (const xmlChar *) ELT_STYLESHEET);
    if (fakep == NULL)
	goto fail;

    sd.sd_xsl_ns = xmlNewNs(fakep, (const xmlChar *) XSL_URI,
			    (const xmlChar *) XSL_PREFIX);
    xmlSetNs(fakep, sd.sd_xsl_ns); /* Noop if NULL */

    nodePush(ctxt, fakep);

    sd.sd_len = strlen(slax_expr);
    buf = alloca(sd.sd_len + 1);
    if (buf == NULL)		/* Should not occur */
	goto fail;
    memcpy(buf, slax_expr, sd.sd_len + 1);
    sd.sd_buf = buf;

    rc = slaxParse(&sd);

    fakep = nodePop(ctxt);
    if (fakep)
	xmlFreeNode(fakep);

    xmlFreeParserCtxt(ctxt);
    ctxt = NULL;

    if (sd.sd_errors) {
	errors += sd.sd_errors;
	xmlParserError(ctxt, "%s: %d error%s detected during parsing (%d)\n",
             sd.sd_filename, sd.sd_errors, (sd.sd_errors == 1) ? "" : "s", rc);
	goto fail;
    }

    if (sd.sd_xpath == NULL) {
	/*
	 * Something's wrong so we punt and return the original string
	 */
	slaxLog("slax: xpath conversion failed: nothing returned");
	errors += 1;
	goto fail;
    }

    buf = slaxStringAsChar(sd.sd_xpath, SSF_QUOTES);

    if (buf == NULL) {
	slaxLog("slax: xpath conversion failed: no buffer");
	goto fail;
    }

    slaxLog("slax: xpath conversion: %s", buf);
    slaxStringFree(sd.sd_xpath);

    if (errorsp)
	*errorsp = errors;
    return buf;

 fail:
    if (ctxt)
	xmlFreeParserCtxt(ctxt);

    if (errorsp)
	*errorsp = errors;

    /*
     * SLAX XPath expressions are completely backwards compatible
     * with XSLT, so we can make an ugly version of the expression
     * by simply returning a copy of our input.  You get "and" instead
     * of "&&", but it's safe enough.
     */
    return (char *) xmlCharStrdup(slax_expr);
}

/*
 * Determine if a file is a SLAX file.  This is currently based on
 * the filename extension, but should one day be more.
 */
static int
slaxIsSlaxFile (const char *filename)
{
    static char slax_ext[] = ".slax";
    size_t len = strlen(filename);

    if (slaxEnabled == SLAX_FORCE)
	return TRUE;

    if (len < sizeof(slax_ext) - 1
	|| memcmp(&filename[len - sizeof(slax_ext) + 1],
		  slax_ext, sizeof(slax_ext)) != 0)
	return FALSE;

    return TRUE;
}

/*
 * Find and open a file, filling in buf with the name of the file
 */
FILE *
slaxFindIncludeFile (const char *url, char *buf, size_t bufsiz)
{
    FILE *file;
    const char *lastsegment = (const char *) url;
    const char *iter;
    slax_data_node_t *dnp;

    if (strchr(url, ':'))	/* It's a real URL */
	return NULL;

    file = fopen((const char *) url, "r");
    if (file) {			/* Straight file name was found */
	strlcpy(buf, url, bufsiz);
	return file;
    }

    for (iter = lastsegment; *iter != 0; iter++)
	if (*iter == '/')
	    lastsegment = iter + 1;

    SLAXDATALIST_FOREACH(dnp, &slaxIncludes) {
	char *dir = dnp->dn_data;
	int dirlen = strlen(dir);
	int lastlen = strlen(lastsegment);
	size_t len = dirlen + lastlen + 2;

	if (len > bufsiz)
	    return NULL;

	memcpy(buf, dir, dirlen);
	buf[dirlen] = '/';
	memcpy(buf + dirlen + 1, lastsegment, lastlen + 1);

	file = fopen((const char *) buf, "r");
	if (file) {
	    return file;
	}
    }

    return NULL;
}

/*
 * A hook into the XSLT parsing mechanism, that gives us first
 * crack at parsing an entity (import or include).
 */
static xmlParserInputPtr
slaxLoadEntity (const char *url, const char *id,
		xmlParserCtxtPtr ctxt)
{
    FILE *file;
    char buf[BUFSIZ];

    file = slaxFindIncludeFile((const char *) url, buf, sizeof(buf));
    if (file == NULL)
	return slaxOriginalEntityLoader(url, id, ctxt);

    fclose(file);
    return slaxOriginalEntityLoader(buf, id, ctxt);
}

/*
 * A hook into the XSLT parsing mechanism, that gives us first
 * crack at parsing a stylesheet.
 */
static xmlDocPtr
slaxLoader (const xmlChar *url, xmlDictPtr dict, int options,
	   void *callerCtxt, xsltLoadType type)
{
    FILE *file;
    xmlDocPtr docp;
    char buf[BUFSIZ];

    if (!slaxIsSlaxFile((const char *) url))
	return slaxOriginalXsltDocDefaultLoader(url, dict, options,
					    callerCtxt, type);

    if (url[0] == '-' && url[1] == 0)
	file = stdin;
    else {
	/* Unescape the URL to remove '%20' etc */
	xmlChar *newurl = alloca(strlen((const char *) url) + 1);
	xmlChar *np = newurl;
	const xmlChar *up = url;

	while (*up) {
	    if (*up == '%') {
		/* Turn '%20' into ' ' */
		int val = (up[1] ? *++up - '0' : 0) * 0x10;
		val += up[1] ? *++up - '0' : 0;
		*np++ = val;
		up += 1;

	    } else {
		*np++ = *up++;
	    }
	}

	*np = '\0';
	url = newurl;
	
	file = slaxFindIncludeFile((const char *) url, buf, sizeof(buf));
	if (file == NULL) {
	    slaxLog("slax: file open failed for '%s': %s",
		    url, strerror(errno));
	    return NULL;
	}
    }

    docp = slaxLoadFile((const char *) url, file, dict, 0);

    if (file != stdin)
	fclose(file);

    slaxDynLoad(docp);

    return docp;
}

/*
 * Read a SLAX stylesheet from an open file descriptor.
 * Written as a clone of libxml2's xmlCtxtReadFd().
 */
xmlDocPtr
slaxCtxtReadFd(xmlParserCtxtPtr ctxt, int fd, const char *URL,
	       const char *encoding, int options)
{
    FILE *file;
    xmlDocPtr docp;

    if (!slaxIsSlaxFile((const char *) URL))
	return xmlCtxtReadFd(ctxt, fd, URL, encoding, options);

    if (fd == 0)
	file = stdin;
    else {
	file = fdopen(fd, "r");
	if (file == NULL) {
	    slaxLog("slax: file open failed for %s: %s",
		      URL, strerror(errno));
	    return NULL;
	}
    }

    docp = slaxLoadFile(URL, file, ctxt->dict, 0);

    if (file != stdin)
	fclose(file);

    return docp;
}

/*
 * Turn on the SLAX XSLT document parsing hook.  This must be
 * called before SLAX files can be parsed.
 */
void
slaxEnable (int enable)
{
    if (enable == SLAX_CLEANUP) {
	xsltSetLoaderFunc(NULL);
	if (slaxIncludesInited)
	    slaxDataListClean(&slaxIncludes);

	slaxEnabled = 0;
	return;
    }

    if (!slaxEnabled && enable) {
	/* Register EXSLT function functions so our function keywords work */
	exsltFuncRegister();
	slaxExtRegister();

	if (!slaxIncludesInited) {
	    slaxIncludesInited = TRUE;
	    slaxDataListInit(&slaxIncludes);
	}

	slaxDynInit();

	/*
	 * Save the original doc loader to pass non-slax file into
	 */
	if (slaxOriginalXsltDocDefaultLoader == NULL)
	    slaxOriginalXsltDocDefaultLoader = xsltDocDefaultLoader;
	xsltSetLoaderFunc(slaxLoader);

	if (slaxOriginalEntityLoader == NULL)
	    slaxOriginalEntityLoader = xmlGetExternalEntityLoader();
	xmlSetExternalEntityLoader(slaxLoadEntity);

    } else if (slaxEnabled && !enable) {
	slaxDynClean();
	if (slaxIncludesInited)
	    slaxDataListClean(&slaxIncludes);

	xsltSetLoaderFunc(NULL);
	if (slaxOriginalEntityLoader)
	    xmlSetExternalEntityLoader(slaxOriginalEntityLoader);
    }

    slaxEnabled = enable;
}

/*
 * Prefer text expressions be stored in <xsl:text> elements.
 * THIS FUNCTION IS DEPRECATED.
 */
void
slaxSetTextAsElement (int enable UNUSED)
{
    return; /* deprecated */
}

#ifdef UNIT_TEST

int
main (int argc, char **argv)
{
    xmlDocPtr docp;
    int ch;
    const char *filename = "-";

    while ((ch = getopt(argc, argv, "df:ty")) != EOF) {
        switch (ch) {
        case 'd':
	    slaxDebug = TRUE;
	    break;

        case 'f':
	    filename = optarg;
	    break;

	case 'y':
	    slaxDebug = TRUE;
	    break;

	default:
	    fprintf(stderr, "invalid option\n");
	    return -1;
	}
    }

    /*
     * Start the XML API
     */
    xmlInitParser();
    slaxEnable(SLAX_FORCE);

    docp = slaxLoader(filename, NULL, XSLT_PARSE_OPTIONS,
                               NULL, XSLT_LOAD_START);

    if (docp == NULL) {
	xsltTransformError(NULL, NULL, NULL,
                "xsltParseStylesheetFile : cannot parse %s\n", filename);
        return -1;
    }

    slaxDump(docp);

    return 0;
}

#endif /* UNIT_TEST */
