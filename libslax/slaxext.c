/*
 * $Id$
 *
 * Copyright (c) 2010-2011, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * SLAX extension functions, including:
 *
 * - <trace>
 * - sequences
 * - mutable variables
 */

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "slaxparser.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <paths.h>
#include <regex.h>
#ifdef HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif
#include <sys/types.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <resolv.h>
#include <sys/queue.h>
#include <tzfile.h>

#include <libslax/slaxdata.h>

#define SYSLOG_NAMES		/* Ask for all the names and typedef CODE */
#include <sys/syslog.h>

#include <libxslt/xsltutils.h>
#include <libxslt/transform.h>
#include <libxml/xpathInternals.h>
#include <libxml/parserInternals.h>
#include <libxml/uri.h>

#ifdef O_EXLOCK
#define DAMPEN_O_FLAGS (O_CREAT | O_RDWR | O_EXLOCK)
#else
#define DAMPEN_O_FLAGS (O_CREAT | O_RDWR)
#endif /* O_EXLOCK */

#ifndef PATH_DAMPEN_DIR
#if defined(_PATH_VARTMP)
#define PATH_DAMPEN_DIR _PATH_VARTMP
#else
#define PATH_DAMPEN_DIR "/var/tmp/"
#endif
#endif /* PATH_DAMPEN_DIR */

#ifndef PATH_DAMPEN_FILE
#define PATH_DAMPEN_FILE "slax.dampen"
#endif

static xmlChar slax_empty_string[1]; /* A non-const empty string */

/*
 * Macosx issue: their version of c_name lacks the "const" and
 * GCC nicks us for putting constant strings into a "char *".
 * Somehow this works for <sys/syslog.h> but not for us....
 */
typedef struct const_code {
    const char *c_name;
    int c_val;
} XCODE;

XCODE junos_facilitynames[] = {
    { "dfc",      LOG_LOCAL1 }, /**< Local DFC facility */
    { "external", LOG_LOCAL2 },  /**< Local external facility */
    { "firewall", LOG_LOCAL3 }, /**< Local firewall facility */
    { "pfe",      LOG_LOCAL4 }, /**< Local PFE facility */
    { "conflict", LOG_LOCAL5 }, /**< Local conflict facility */
    { "change",   LOG_LOCAL6 }, /**< Local change facility */
    { "interact", LOG_LOCAL7 }, /**< Local interact facility */
    { NULL,       -1         }
};

/*
 * More macosx breakage: their version of LOG_MAKEPRI() does the "<<3"
 * shift on fac values that are already shifted.
 */
#define LOG_MAKEPRI_REAL(fac, pri) ((fac) | (pri))

/*
 * Emit an error using a parser context
 */
void
slaxTransformError (xmlXPathParserContextPtr ctxt, const char *fmt, ...)
{
    char buf[BUFSIZ];
    va_list vap;
    xsltTransformContextPtr tctxt = xsltXPathGetTransformContext(ctxt);

    va_start(vap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, vap);
    va_end(vap);
    buf[sizeof(buf) - 1] = '\0'; /* Ensure NUL terminated string */

    xsltTransformError(tctxt, tctxt ? tctxt->style : NULL,
		       tctxt? tctxt->inst : NULL, "%s\n", buf);
}

/*
 * Emit an error using a transform context
 */
void
slaxTransformError2 (xsltTransformContextPtr tctxt, const char *fmt, ...)
{
    char buf[BUFSIZ];
    va_list vap;

    va_start(vap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, vap);
    va_end(vap);
    buf[sizeof(buf) - 1] = '\0'; /* Ensure NUL terminated string */

    xsltTransformError(tctxt, tctxt ? tctxt->style : NULL,
		       tctxt? tctxt->inst : NULL, "%s\n", buf);
}

/*
 * Many functions return RTFs (Resultant Tree Fragments).  These are
 * containers hung off the current context so they can be released
 * when the context is popped.  So we make an RTF (RVT is the older
 * name) and register it with the context.
 */
xmlDocPtr
slaxMakeRtf (xmlXPathParserContextPtr ctxt)
{
    xmlDocPtr container;
    xsltTransformContextPtr tctxt;

    /* Return a result tree fragment */
    tctxt = xsltXPathGetTransformContext(ctxt);
    if (tctxt == NULL) {
	slaxTransformError(ctxt,
		       "slax:makeRtf: internal error: tctxt is NULL");
	return NULL;
    }

    container = xsltCreateRVT(tctxt);
    if (container == NULL)
	return NULL;
    
    xsltRegisterLocalRVT(tctxt, container);
    return container;
}

/*
 * The following code supports the "trace" statement in SLAX, which is
 * turned into the XML element <slax:trace> by the parser.  If the
 * environment has provided a callback function (via slaxTraceEnable()),
 * then script-level tracing data is passed to that callback.  This
 * makes a great way of keeping trace data in the script in a
 * maintainable way.
 */
typedef struct trace_precomp_s {
    xsltElemPreComp tp_comp;	   /* Standard precomp header */
    xmlXPathCompExprPtr tp_select; /* Compiled select expression */
    xmlNsPtr *tp_nslist;	   /* Prebuilt namespace list */
    int tp_nscount;		   /* Number of namespaces in tp_nslist */
} trace_precomp_t;

/*
 * This is the callback function that we use to pass trace data
 * up to the caller.
 */
static slaxTraceCallback_t slaxTraceCallback;
static void *slaxTraceCallbackData;

/**
 * Deallocates a trace_precomp_t
 *
 * @comp the precomp data to free (a trace_precomp_t)
 */
static void
slaxTraceFreeComp (trace_precomp_t *comp)
{
    if (comp == NULL)
	return;

    if (comp->tp_select)
	xmlXPathFreeCompExpr(comp->tp_select);

    xmlFreeAndEasy(comp->tp_nslist);
    xmlFree(comp);
}

/**
 * Precompile a trace element, so make running it faster
 *
 * @style the current stylesheet
 * @inst this instance
 * @function the transform function (opaquely passed to xsltInitElemPreComp)
 */
static xsltElemPreCompPtr
slaxTraceCompile (xsltStylesheetPtr style, xmlNodePtr inst,
	       xsltTransformFunction function)
{
    xmlChar *sel;
    trace_precomp_t *comp;

    comp = xmlMalloc(sizeof(*comp));
    if (comp == NULL) {
	xsltGenericError(xsltGenericErrorContext, "trace: malloc failed\n");
	return NULL;
    }

    memset(comp, 0, sizeof(*comp));

    xsltInitElemPreComp((xsltElemPreCompPtr) comp, style, inst, function,
			 (xsltElemPreCompDeallocator) slaxTraceFreeComp);
    comp->tp_select = NULL;

    /* Precompile the select attribute */
    sel = xmlGetNsProp(inst, (const xmlChar *) ATT_SELECT, NULL);
    if (sel != NULL) {
	comp->tp_select = xmlXPathCompile(sel);
	xmlFree(sel);
    }

    /* Prebuild the namespace list */
    comp->tp_nslist = xmlGetNsList(inst->doc, inst);
    if (comp->tp_nslist != NULL) {
	int i = 0;
	while (comp->tp_nslist[i] != NULL)
	    i++;
	comp->tp_nscount = i;
    }

    return &comp->tp_comp;
}

/**
 * Handle a <trace> element, as manufactured by the "trace" statement
 *
 * @ctxt transform context
 * @node current input node
 * @inst the <trace> element
 * @comp the precompiled info (a trace_precomp_t)
 */
static void
slaxTraceElement (xsltTransformContextPtr ctxt,
		  xmlNodePtr node UNUSED, xmlNodePtr inst,
		  xsltElemPreCompPtr precomp)
{
    trace_precomp_t *comp = (trace_precomp_t *) precomp;
    xmlXPathObjectPtr value;

    if (slaxTraceCallback == NULL)
	return;

    if (comp->tp_select) {
	/*
	 * Adjust the context to allow the XPath expresion to
	 * find the right stuff.  Save old info like namespace,
	 * install fake ones, eval the expression, then restore
	 * the saved values.
	 */
	xmlNsPtr *save_nslist = ctxt->xpathCtxt->namespaces;
	int save_nscount = ctxt->xpathCtxt->nsNr;
	xmlNodePtr save_context = ctxt->xpathCtxt->node;

	ctxt->xpathCtxt->namespaces = comp->tp_nslist;
	ctxt->xpathCtxt->nsNr = comp->tp_nscount;
	ctxt->xpathCtxt->node = node;

	value = xmlXPathCompiledEval(comp->tp_select, ctxt->xpathCtxt);

	ctxt->xpathCtxt->node = save_context;
	ctxt->xpathCtxt->nsNr = save_nscount;
	ctxt->xpathCtxt->namespaces = save_nslist;

    } else if (inst->children) {
	/*
	 * The content of the element is a template that generates
	 * the value.
	 */
	xmlNodePtr save_insert;
	xmlDocPtr container;

	/* Fake an RVT to hold the output of the template */
	container = xsltCreateRVT(ctxt);
	if (container == NULL) {
	    xsltGenericError(xsltGenericErrorContext, "trace: no memory\n");
	    return;
	}

	xsltRegisterLocalRVT(ctxt, container);	

	/* Set up the insertion point for new output */
	save_insert = ctxt->insert;
	ctxt->insert = (xmlNodePtr) container;

	/* Apply the template code inside the element */
	xsltApplyOneTemplate(ctxt, node,
			      inst->children, NULL, NULL);
	ctxt->insert = save_insert;

	value = xmlXPathNewValueTree((xmlNodePtr) container);

    } else
	return;

    if (value == NULL)
	return;

    /*
     * Mark it as a function result in order to avoid garbage
     * collecting of tree fragments before the function exits.
     */
    xsltExtensionInstructionResultRegister(ctxt, value);

    switch (value->type) {
    case XPATH_STRING:
	if (value->stringval)
	    slaxTraceCallback(slaxTraceCallbackData, NULL,
			      "%s", value->stringval);
	break;

    case XPATH_NUMBER:
	slaxTraceCallback(slaxTraceCallbackData, NULL, "%f", value->floatval);
	break;

    case XPATH_XSLT_TREE:
    case XPATH_NODESET:
	if (value->nodesetval) {
	    int i;
	    xmlNodeSetPtr tab = value->nodesetval;
	    for (i = 0; i < tab->nodeNr; i++) {
		slaxTraceCallback(slaxTraceCallbackData,
				  tab->nodeTab[i], NULL);
	    }
	}
	break;

    case XPATH_BOOLEAN:
	slaxTraceCallback(slaxTraceCallbackData, NULL, "%s",
			  value->boolval ? "true" : "false");
	break;

    case XPATH_UNDEFINED:
    case XPATH_POINT:
    case XPATH_RANGE:
    case XPATH_LOCATIONSET:
    case XPATH_USERS:
    default:
	;
    }
}

/**
 * Enable tracing with a callback
 *
 * @func callback function
 * @data opaque data passed to callback
 */
void
slaxTraceEnable (slaxTraceCallback_t func, void *data)
{
    slaxTraceCallback = func;
    slaxTraceCallbackData = data;
}

/* ---------------------------------------------------------------------- */

/*
 * The following code supports the "while" statement in SLAX, which is
 * turned into the XML element <slax:while> by the parser.  A while is
 * basically an "if" in a loop, and is fairly simple.
 */
typedef struct while_precomp_s {
    xsltElemPreComp wp_comp;	   /* Standard precomp header */
    xmlXPathCompExprPtr wp_test;   /* Compiled test expression */
    xmlNsPtr *wp_nslist;	   /* Prebuilt namespace list */
    int wp_nscount;		   /* Number of namespaces in tp_nslist */
} while_precomp_t;

/**
 * Deallocates a while_precomp_t
 *
 * @comp the precomp data to free (a while_precomp_t)
 */
static void
slaxWhileFreeComp (while_precomp_t *comp)
{
    if (comp == NULL)
	return;

    if (comp->wp_test)
	xmlXPathFreeCompExpr(comp->wp_test);

    xmlFreeAndEasy(comp->wp_nslist);
    xmlFree(comp);
}

/**
 * Precompile a while element, so make running it faster
 *
 * @style the current stylesheet
 * @inst this instance
 * @function the transform function (opaquely passed to xsltInitElemPreComp)
 */
static xsltElemPreCompPtr
slaxWhileCompile (xsltStylesheetPtr style, xmlNodePtr inst,
	       xsltTransformFunction function)
{
    xmlChar *test;
    while_precomp_t *comp;

    comp = xmlMalloc(sizeof(*comp));
    if (comp == NULL) {
	xsltGenericError(xsltGenericErrorContext, "while: malloc failed\n");
	return NULL;
    }

    memset(comp, 0, sizeof(*comp));

    xsltInitElemPreComp((xsltElemPreCompPtr) comp, style, inst, function,
			 (xsltElemPreCompDeallocator) slaxWhileFreeComp);
    comp->wp_test = NULL;

    if (inst->children == NULL) {
	xsltGenericError(xsltGenericErrorContext,
			 "warning: tight while loop was detected\n");
    }

    /* Get the test attribute value */
    test = xmlGetNsProp(inst, (const xmlChar *) ATT_TEST, NULL);
    if (test == NULL) {
	xsltGenericError(xsltGenericErrorContext,
			 "while: missing test attribute\n");
	return NULL;
    }

    /* Precompile the test attribute */
    comp->wp_test = xmlXPathCompile(test);
    if (comp->wp_test == NULL) {
	xsltGenericError(xsltGenericErrorContext,
			 "while: test attribute does not compile: %s\n", test);
	xmlFree(test);
	return NULL;
    }

    xmlFree(test);

    /* Prebuild the namespace list */
    comp->wp_nslist = xmlGetNsList(inst->doc, inst);
    if (comp->wp_nslist != NULL) {
	int i = 0;
	while (comp->wp_nslist[i] != NULL)
	    i++;
	comp->wp_nscount = i;
    }

    return &comp->wp_comp;
}

/**
 * Handle a <slax:while> element, as manufactured by the "while" statement
 *
 * @ctxt transform context
 * @node current input node
 * @inst the <while> element
 * @comp the precompiled info (a while_precomp_t)
 */
static void
slaxWhileElement (xsltTransformContextPtr ctxt,
		  xmlNodePtr node UNUSED, xmlNodePtr inst,
		  xsltElemPreCompPtr precomp)
{
    int value;
    while_precomp_t *comp = (while_precomp_t *) precomp;

    if (comp->wp_test == NULL) {
	xsltGenericError(xsltGenericErrorContext,
			 "while: test attribute was not compiled\n");
	return;
    }

    for (;;) {
	/*
	 * Adjust the context to allow the XPath expresion to
	 * find the right stuff.  Save old info like namespace,
	 * install fake ones, eval the expression, then restore
	 * the saved values.
	 */
	xmlNsPtr *save_nslist = ctxt->xpathCtxt->namespaces;
	int save_nscount = ctxt->xpathCtxt->nsNr;
	xmlNodePtr save_context = ctxt->xpathCtxt->node;

	ctxt->xpathCtxt->namespaces = comp->wp_nslist;
	ctxt->xpathCtxt->nsNr = comp->wp_nscount;
	ctxt->xpathCtxt->node = node;

	/* If the user typed 'quit' or 'run' at the debugger prompt, bail */
        if (ctxt->debugStatus == XSLT_DEBUG_QUIT
			|| ctxt->debugStatus == XSLT_DEBUG_RUN_RESTART)
	    break;

        if (ctxt->debugStatus != XSLT_DEBUG_NONE)
            xslHandleDebugger(inst, node, ctxt->templ, ctxt);

	value = xmlXPathCompiledEvalToBoolean(comp->wp_test, ctxt->xpathCtxt);

	ctxt->xpathCtxt->node = save_context;
	ctxt->xpathCtxt->nsNr = save_nscount;
	ctxt->xpathCtxt->namespaces = save_nslist;

	if (value < 0) {
	    xsltGenericError(xsltGenericErrorContext, "while: test fails\n");
	    break;
	} else if (value == 0)
	    break;

	/* Apply the template code inside the element */
	xsltApplyOneTemplate(ctxt, node,
			      inst->children, NULL, NULL);
    }
}

/* ---------------------------------------------------------------------- */

/*
 * Return a sequence of fictional nodes, based on the two arguments.
 * "1 .. 10" builds <item> 1, <item> 2, thru <item> 10, where
 * "44 .. 40" builds <item> 44, <item> 43, thur <item> 40.
 * These can be used to make cheap iterators, but have the sad side effect
 * of making all the nodes at one time.
 */
static void
slaxExtBuildSequence (xmlXPathParserContextPtr ctxt, int nargs)
{
    long long num, start, last, step = 1;
    xmlNodePtr nodep;
    xmlDocPtr container;
    xmlXPathObjectPtr ret = NULL;
    char buf[BUFSIZ];

    if (nargs != 2) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    /* Pop args in reverse order */
    last = xmlXPathPopNumber(ctxt);
    if (xmlXPathCheckError(ctxt))
	return;

    start = xmlXPathPopNumber(ctxt);
    if (xmlXPathCheckError(ctxt))
	return;

    if (start <= last)
	last += 1;
    else {
	step = -1;
	last = last - 1;
    }

    slaxLog("build-sequence: %qd ... %qd + %qd", start, last, step);

    /* Return a result tree fragment */
    container = slaxMakeRtf(ctxt);
    if (container == NULL)
	goto fail;

    ret = xmlXPathNewNodeSet(NULL);
    if (ret == NULL)
	goto fail;

    for (num = start; num != last; num += step) {
	snprintf(buf, sizeof(buf), "%qd", num);
	nodep = xmlNewDocRawNode(container, NULL,
				 (const xmlChar *) "item", (xmlChar *) buf);
	if (nodep) {
	    xmlAddChild((xmlNodePtr) container, nodep);
	    xmlXPathNodeSetAdd(ret->nodesetval, nodep);
	}
    }

fail:
    if (ret != NULL)
	valuePush(ctxt, ret);
    else
	valuePush(ctxt, xmlXPathNewNodeSet(NULL));
}

/* ----------------------------------------------------------------------
 * Extension functions created for commit scripts in JUNOS that are fairly
 * generic.  We include them here because they are quite useful.
 */

/*
 * Return the first non-NULL member of the argument list
 *
 * Usage: <output> slax:first-of($input, $argument, $default, "huh?");
 */
static void
slaxExtFirstOf (xmlXPathParserContext *ctxt, int nargs)
{
    int i;
    xmlXPathObjectPtr results = NULL;

    for (i = 0; i < nargs; i++) {
	xmlXPathObjectPtr xop = valuePop(ctxt);
	if (xop == NULL)
	    continue;

	if (xop->type == XPATH_NODESET && xop->nodesetval == NULL) {
	    xmlXPathFreeObject(xop);
	    continue;
	}
	
	if (xop->nodesetval && xop->nodesetval->nodeNr == 0) {
	    xmlXPathFreeObject(xop);
	    continue;
	}

	if (xop->type == XPATH_STRING
		    && (!xop->stringval || !*xop->stringval)) {
	    xmlXPathFreeObject(xop);
	    continue;
	}

	/*
	 * We're popping off the stack, so we just need to hold on
	 * to the most recent value.  Free any previous value.
	 */
	if (results)
	    xmlXPathFreeObject(results);

	results = xop;
    }

    /*
     * If everything was NULL, we toss an empty node set onto the
     * stack to avoid an error.
     */
    if (results == NULL)
	results = xmlXPathNewNodeSet(NULL);

    /* Push the results back onto the stack */
    valuePush(ctxt, results);
}

/* ---------------------------------------------------------------------- */

typedef struct slax_printf_buffer_s {
    char *pb_buf;		/* Start of the buffer */
    int pb_bufsiz;		/* Size of the buffer */
    char *pb_cur;		/* Current insertion point */
    char *pb_end;		/* End of the buffer (buf + bufsiz) */
} slax_printf_buffer_t;

static int
slaxExtPrintExpand (slax_printf_buffer_t *pbp, int min_add)
{
    min_add = (min_add < BUFSIZ) ? BUFSIZ : roundup(min_add, BUFSIZ);

    if (min_add < pbp->pb_bufsiz)
	min_add = pbp->pb_bufsiz;

    int size = pbp->pb_bufsiz + min_add;

    char *newp = xmlRealloc(pbp->pb_buf, size);
    if (newp == NULL)
	return TRUE;

    pbp->pb_end = newp + size - 1;
    pbp->pb_cur = newp + (pbp->pb_cur - pbp->pb_buf);
    pbp->pb_buf = newp;
    pbp->pb_bufsiz = size;

    return FALSE;
}

static void
slaxExtPrintAppend (slax_printf_buffer_t *pbp, const xmlChar *chr, int len)
{
    if (len == 0)
	return;

    if (pbp->pb_end - pbp->pb_cur >= len || !slaxExtPrintExpand(pbp, len)) {
	memcpy(pbp->pb_cur, chr, len);

	/*
	 * Deal with escape sequences.  Currently the only one
	 * we care about is '\n', but we may add others.
	 */
	char *sp, *np, *ep;
	for (np = pbp->pb_cur, ep = pbp->pb_cur + len; np < ep; ) {
	    int shift = FALSE;
	    sp = memchr(np, '\\', ep - np);
	    if (sp == NULL)
		break;
	    
	    switch (sp[1]) {
		case 'n':
		    *sp = '\n';
		    shift = TRUE;
		    break;
		case 't':
		    *sp = '\t';
		    shift = TRUE;
		    break;
		case '\\':
		    shift = TRUE;
		    *sp = '\\';
	    }

	    /*
	     * Not optimal, but good enough for a rare operation
	     */
	    if (shift) {
		memmove(sp + 1, sp + 2, ep - (sp + 2));
		len -= 1;
		ep -= 1;
	    }

	    np = sp + 1;
	}

	pbp->pb_cur += len;
	*pbp->pb_cur = '\0';
    }
}

static int
slaxExtPrintfToBuf (slax_printf_buffer_t *pbp, char *field, xmlChar *arg,
		   int args_used, int width, int precision)
{
    int left = pbp->pb_end - pbp->pb_cur;

    if (args_used == 1)
	return snprintf(pbp->pb_cur, left, field, arg);

    if (args_used == 2)
	return snprintf(pbp->pb_cur, left, field, width, arg);

    if (args_used == 3)
	return snprintf(pbp->pb_cur, left, field, width, precision, arg);

    return 0;
}

/*
 * Free an array of strings, and then the array itself
 */
static void *
slaxExtPrintFreeArgs (int argc, xmlChar **argv)
{
    if (argv) {
	int i;
	xmlChar **saved = argv;

	for (i = 0; i < argc; i++)
	    if (argv[i])
		xmlFree(argv[i]);
	xmlFree(saved);
    }

    return NULL;
}

/*
 * This is the brutal guts of the printf code.  We pull out each
 * field and format it into a buffer, which we then return.
 */
static char *
slaxExtPrintIt (const xmlChar *fmtstr, int argc, xmlChar **argv)
{
    slax_printf_buffer_t pb;
    const xmlChar *fmt;
    int arg_ndx = 1;
    int save_argc = argc;

    /* Use this local buffer to extract the format for each field */
    int flen = xmlStrlen(fmtstr); /* Worst case */
    char *field = alloca(flen + 1);
    char *fp, *efp = field + flen;

    bzero(&pb, sizeof(pb));
    field[0] = '%';

    /* 
     * We keep the last set of arguments if any of the fields
     * had "%j1" turned on.  But if this printf call uses a
     * difference format string, we nuke them.
     */
    static xmlChar **last_argv;
    static int last_argc;
    xmlChar **new_argv = NULL;	/* Build new 'last' here */

    if (last_argv && last_argv[0] && !xmlStrEqual(fmtstr, last_argv[0]))
	last_argv = slaxExtPrintFreeArgs(last_argc, last_argv);

    for (fmt = fmtstr; *fmt; ) {
	const xmlChar *percent = xmlStrchr(fmt, '%');
	if (percent == NULL) {
	    /* No percent means that the string is dull.  Copy it */
	    slaxExtPrintAppend(&pb, fmt, xmlStrlen(fmt));
	    break;
	}

	if (percent != fmt) {
	    /* Copy the leading portion of the string */
	    slaxExtPrintAppend(&pb, fmt, percent - fmt);
	    fmt = percent;
	}

	if (percent[1] == '%') { /* Double percent */
	    slaxExtPrintAppend(&pb, percent, 1);
	    fmt = percent + 2;
	    continue;
	}

	int args_used = 1;
	int done = FALSE;
	int capitalize = FALSE;
	char *tag = NULL;
	int tlen = 0;
	int once = FALSE;

	fmt += 1;		/* Skip percent */

	/*
	 * First we traverse each field, converting a portion of the input
	 * format string (fmt) into a single field's format string (field).
	 */
	for (fp = field + 1; !done && *fmt && fp < efp; fmt++) {
	    switch (*fmt) {
	    case 'l':
		/* Ignore these */
		break;

	    case 'd': case 'i': case 'o': case 'u': case 'x':
	    case 'e': case 'E': case 'f': case 'g': case 'G':
		/* We treat these as strings */
		/* fallthru */

	    case 's':
		*fp++ = 's';
		done = TRUE;
		break;

	    case 'j':
		switch (*++fmt) {
		case 'c':
		    /*
		     * "%jc" turns on the 'capitalize' flag, which
		     * capitalizes the first letter of the field.
		     */
		    capitalize = TRUE;
		    break;

		case 't':
		    /*
		     * "%jt{TAG}" prepends the tag to the output
		     * if the printf element is non-empty.
		     */
		    if (fmt[1] != '{')
			break;

		    const xmlChar *tep = xmlStrchr(fmt, '}');
		    if (tep == NULL)
			break;

		    /* Copy the tag into 'tag' */
		    fmt += 2;	/* Skip '{' */
		    tep -= 1;	/* Skip '}' */

		    tlen = tep - fmt + 1;
		    tag = alloca(tlen + 1);
		    memcpy(tag, fmt, tlen);
		    tag[tlen] = '\0';

		    fmt = tep + 1; /* More part the '}' */
		    break;

		case '1':
		    once = TRUE;
		    if (new_argv == NULL) {
			/* 2 == one for fmtstr, one for NULL */
			new_argv = xmlMalloc((save_argc + 2)
					     * sizeof(*new_argv));
			if (new_argv) {
			    new_argv[0] = xmlStrdup(fmtstr);
			    bzero(&new_argv[1], (save_argc + 1)
				  * sizeof(*new_argv));
			}
		    }
		    break;
		}

		break;

	    case '*':
		args_used += 1;
		/* fallthru */

	    case '.': case '+': case '-':
		*fp++ = *fmt;
		break;

	    default:
		if (isdigit(*fmt)) {
		    while (isdigit(*fmt))
			*fp++ = *fmt++;
		    fmt -= 1;	/* Back up after the number's done */
		} else {
		    /* Anything else is ignored */
		}
	    }
	}

	if (args_used > argc) {
	    /*
	     * Should do something more here, but ...
	     */
	    break;
	}

	*fp = '\0';

	int width = (args_used > 1) ? xmlAtoi(*argv++) : 0;
	int precision = (args_used > 2) ? xmlAtoi(*argv++) : 0;

	xmlChar *arg = *argv++;
	arg_ndx += 1;

	/*
	 * The XPath string conversion should never give us NULLs,
	 * but it never hurts to be paranoid.
	 */
	if (arg == NULL)
	    arg = slax_empty_string;

	if (once) {
	    if (last_argv && last_argv[arg_ndx]
			&& xmlStrEqual(arg, last_argv[arg_ndx])) {
		/*
		 * If the string is identical to the last time we were
		 * called, we use an empty string, rather than repeat
		 * the same data.  We also move the copy into the new_argv
		 * so we see it next time thru.
		 */
		arg = slax_empty_string;
		new_argv[arg_ndx] = last_argv[arg_ndx];
		last_argv[arg_ndx] = NULL;

	    } else if (new_argv)
		new_argv[arg_ndx] = xmlStrdup(arg);
	}

	int left = pb.pb_end - pb.pb_cur;

	/*
	 * Prepend the tag ("%jt{TAG}") if the argument is non-empty
	 */
	if (tag && arg && *arg) {
	    if (left >= tlen || !slaxExtPrintExpand(&pb, tlen)) {
		memcpy(pb.pb_cur, tag, tlen);
		pb.pb_cur += tlen;
	    }
	    left = pb.pb_end - pb.pb_cur;
	}

	int needed = slaxExtPrintfToBuf(&pb, field, arg,
				       args_used, width, precision);

	if (needed > left) {
	    if (slaxExtPrintExpand(&pb, needed + 1))
		needed = 0;
	    else {
		left = pb.pb_end - pb.pb_cur;
		needed = slaxExtPrintfToBuf(&pb, field, arg,
					   args_used, width, precision);
	    }
	}

	if (capitalize && islower((int) *pb.pb_cur))
	    *pb.pb_cur = toupper((int) *pb.pb_cur);

	pb.pb_cur += needed;

	argc -= args_used;
    }

    /*
     * Save/free the previous/new set of arguments.  If we didn't see
     * a "%j1" field, then new_argv will be NULL, so we just free the old
     * arguments.
     */
    if (new_argv) {
	slaxExtPrintFreeArgs(last_argc, last_argv);
	last_argv = new_argv;
	last_argc = arg_ndx;
    } else {
	last_argv = slaxExtPrintFreeArgs(last_argc, last_argv);
	last_argc = 0;
    }

    return pb.pb_buf;
}


/*
 * printf -- C-style printf functionality with some juniper-specific
 * extensions.
 *
 * Usage: <output> slax:printf("%j1-6s %jcs", $slot, $status);
 */
static void
slaxExtPrintf (xmlXPathParserContext *ctxt, int nargs)
{
    xmlChar *strstack[nargs];	/* Stack for strings */
    int ndx;

    if (nargs == 0) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    bzero(strstack, sizeof(strstack));
    for (ndx = nargs - 1; ndx >= 0; ndx--) {
	strstack[ndx] = xmlXPathPopString(ctxt);
	if (xmlXPathCheckError(ctxt))
	    goto bail;
    }

    /*
     * First argument is the format string.  If we don't
     * have one, we bail.
     */
    xmlChar *fmtstr = strstack[0];
    if (fmtstr == NULL) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    xmlChar *outstr = (xmlChar *) slaxExtPrintIt(fmtstr, nargs - 1, strstack + 1);

    xmlXPathReturnString(ctxt, outstr);

bail:
    for (ndx = nargs - 1; ndx >= 0; ndx--) {
	if (strstack[ndx])
	    xmlFree(strstack[ndx]);
    }
}

/*
 * sleep -- sub-second sleep function.
 *     sleep(seconds, millis)
 * where millis is optional (since xslt will tell me the number of
 * arguments).  sleep(1) means one second and sleep(0, 10) means
 * ten milliseconds.
 *
 * Usage:  expr slax:sleep(10);
 */
static void
slaxExtSleep (xmlXPathParserContext *ctxt, int nargs)
{
    if (nargs == 0) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    struct timespec tv;
    bzero(&tv, sizeof(tv));

    char *endp = NULL;
    char *str = (char *) xmlXPathPopString(ctxt);
    if (str == NULL) {
	xmlXPathSetArityError(ctxt);
	return;
    }	

    unsigned long arg1 = strtoul(str, &endp, 0);
    xmlFree(str);
    if (endp == str) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    if (nargs == 1) {
	tv.tv_sec = arg1;
    } else {
	int secs = 0;

	if (arg1 >= MSEC_PER_SEC) {
	    secs = arg1 / MSEC_PER_SEC;
	    arg1 = arg1 % MSEC_PER_SEC;
	}

	tv.tv_nsec = arg1 * NSEC_PER_MSEC;

	str = (char *) xmlXPathPopString(ctxt);
	endp = NULL;
	tv.tv_sec = secs + strtol(str, &endp, 0);

	xmlFree(str);
	if (endp == str) {
	    xmlXPathSetArityError(ctxt);
	    return;
	}
    }

    nanosleep(&tv, NULL);

    xmlChar *outstr = xmlStrdup(slax_empty_string);
    xmlXPathReturnString(ctxt, outstr);
}

/*
 * Build a node containing a text node
 */
static xmlNode *
slaxExtMakeTextNode (xmlDocPtr docp, xmlNs *nsp, const char *name,
		     const char *content, int len)
{
    xmlNode *newp = xmlNewDocNode(docp, nsp, (const xmlChar *) name, NULL);

    if (newp == NULL)
        return NULL;
    
    xmlNode *tp = xmlNewTextLen((const xmlChar *) content, len);
    if (tp == NULL) {
        xmlFreeNode(newp);
        return NULL;
    }

    xmlAddChildList(newp, tp);
    return newp;
}

/*
 * Break a string into the set of clone element, with each clone
 * containing one line of text.
 */
static void
slaxExtBreakString (xmlDocPtr container, xmlNodeSet *results, char *content,
		    xmlNsPtr nsp, const char *name)
{
    xmlNode *clone;
    xmlNode *last;
    char *cp, *sp;

    /* If there's no content, return an empty clone */
    if (content == NULL) {
	clone = slaxExtMakeTextNode(container, nsp, name, NULL, 0);
	if (clone) {
	    xmlXPathNodeSetAdd(results, clone);
	    xmlAddChild((xmlNodePtr) container, clone);
	}
	return;
    }

    cp = strchr(content, '\n');
    if (cp == NULL) {
	clone = slaxExtMakeTextNode(container, nsp, name,
				    content, strlen(content));
	if (clone) {
	    xmlXPathNodeSetAdd(results, clone);
	    xmlAddChild((xmlNodePtr) container, clone);
	}
	return;
    }

    sp = content;
    last = NULL;

    for (;;) {
	/*
	 * MS-DOS uses CRLF instead of just LF, and windows keeps this
	 * encoding alive.
	 */
	int dos_format = (cp <= sp) ? 0 : (cp[-1] == '\r') ? 1 : 0;
					  
	clone = slaxExtMakeTextNode(container, nsp, name,
				    sp, cp - sp - dos_format);
	if (clone) {
	    xmlXPathNodeSetAdd(results, clone);
	    if (last)
		xmlAddSibling(last, clone);

	    xmlAddChild((xmlNodePtr) container, clone);
	    last = clone;
	}

	sp = cp;
	if (*sp == '\0' || *++sp == '\0')
	    break;

	cp = strchr(sp, '\n');
	if (cp == NULL)
	    cp = sp + strlen(sp);
    }
}

/*
 * Break a simple element into multiple elements, delimited by
 * newlines.  This is especially useful for big <output> elements,
 * such as are produced by the "show pfe" commands.
 *
 * Usage:  var $lines = slax:break-lines($output);
 */
static void
slaxExtBreakLines (xmlXPathParserContext *ctxt, int nargs)
{
    xmlXPathObject *stack[nargs];	/* Stack for args as objects */
    xmlXPathObject *obj;
    xmlXPathObjectPtr ret;
    xmlDocPtr container;

    if (nargs == 0) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    int ndx;
    for (ndx = 0; ndx < nargs; ndx++)
	stack[nargs - 1 - ndx] = valuePop(ctxt);

    xmlNodeSet *results = xmlXPathNodeSetCreate(NULL);

    /*
     * Create a Result Value Tree container, and register it with RVT garbage 
     * collector. 
     */
    container = slaxMakeRtf(ctxt);
    if (container == NULL)
	goto fail;

    for (ndx = 0; ndx < nargs; ndx++) {
	if (stack[ndx] == NULL)	/* Should not occur */
	    continue;

	obj = stack[ndx];
	if (obj->nodesetval) {
	    int i;
	    for (i = 0; i < obj->nodesetval->nodeNr; i++) {
		xmlNode *nop = obj->nodesetval->nodeTab[i];
		if (nop == NULL || nop->children == NULL)
		    continue;

		/*
		 * If we're handed a fragment, assume they wanted the
		 * contents.
		 */
		if (XSLT_IS_RES_TREE_FRAG(nop))
		    nop = nop->children;

		/*
		 * Whiffle thru the children looking for a node
		 */
		xmlNode *cop;
		for (cop = nop->children; cop; cop = cop->next) {
		    if (cop->type != XML_TEXT_NODE)
			continue;

		    slaxExtBreakString(container, results,
				       (char *) cop->content,
				       nop->ns, (const char *) nop->name);
		}
	    }

	} else if (obj->stringval) {
	    slaxExtBreakString(container, results, (char *) obj->stringval,
			       NULL, ELT_TEXT);
	}
    }

    for (ndx = 0; ndx < nargs; ndx++)
	xmlXPathFreeObject(stack[ndx]);

 fail:
    ret = xmlXPathNewNodeSetList(results);
    valuePush(ctxt, ret);
    xmlXPathFreeNodeSet(results);
}

/*
 * Return the set of matches matched by the given regular expression.
 * This requires two arguments, the regex and the string to match on.
 *
 * Usage:
 *     var $pat = "([0-9]+)(:*)([a-z]*)";
 *     var $a = slax:regex($pat, "123:xyz");
 *     var $b = slax:regex($pat, "r2d2");
 *     var $c = slax:regex($pat, "test999!!!");
 *
 * $a[1] == "123:xyz"    # full string that matches the regex
 * $a[2] == "123"        # ([0-9]+)
 * $a[3] == ":"          # (:*)
 * $a[4] == "xyz"        # ([a-z]*)
 * $b[1] == "2d"         # full string that matches the regex
 * $b[2] == "2"          # ([0-9]+)
 * $b[3] == ""           # (:*)   [empty match]
 * $b[4] == "d"          # ([a-z]*)
 * $c[1] == "999"        # full string that matches the regex
 * $c[2] == "999"        # ([0-9]+)
 * $c[3] == ""           # (:*)   [empty match]
 * $c[4] == ""           # ([a-z]*)   [empty match]
 */
static void
slaxExtRegex (xmlXPathParserContext *ctxt, int nargs)
{
    char buf[BUFSIZ];
    xmlXPathObjectPtr ret;
    xmlDocPtr container;
    int cflags = REG_EXTENDED, eflags = 0;
    int return_boolean = FALSE;
    xmlChar *target_str, *pattern;
    char *target;
    xmlNodeSet *results = NULL;
    regex_t reg;
    int nmatch = 10;
    regmatch_t pm[nmatch];
    int rc;

    if (nargs == 3) {
	/* The optional third argument is a string containing flags */
	xmlChar *opts = xmlXPathPopString(ctxt);
	int i;

	for (i = 0; opts[i]; i++) {
	    if (opts[i] == 'i' || opts[i] == 'I')
		cflags |= REG_ICASE;
	    else if (opts[i] == 'b') {
		return_boolean = TRUE;
		cflags |= REG_NOSUB;
	    } else if (opts[i] == 'n')
		cflags |= REG_NEWLINE;
	    else if (opts[i] == '^')
		eflags |= REG_NOTBOL;
	    else if (opts[i] == '$')
		eflags |= REG_NOTEOL;
	}

	xmlFreeAndEasy(opts);

    } else if (nargs != 2) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    target_str = xmlXPathPopString(ctxt);
    target = (char *) target_str;
    pattern = xmlXPathPopString(ctxt);

    if (return_boolean)
	nmatch = 0;		/* Boolean doesn't care for patterns */

    /*
     * Create a Result Value Tree container, and register it with RVT garbage 
     * collector. 
     */
    container = slaxMakeRtf(ctxt);
    if (container == NULL)
	return;

    bzero(&pm, sizeof(pm));

    rc = regcomp(&reg, (const char *) pattern, cflags);
    if (rc)
	goto fail;

    rc = regexec(&reg, target, nmatch, pm, eflags);
    if (rc && rc != REG_NOMATCH)
	goto fail;
   
    if (rc != REG_NOMATCH) {
	int i, len, max = 0;
	xmlNode *last = NULL;

	if (return_boolean) {
	    regfree(&reg);
	    xmlFree(target_str);
	    xmlFree(pattern);
	    xmlXPathReturnBoolean(ctxt, TRUE);
	    return;
	}

	for (i = 0; i < nmatch; i++) {
	    if (pm[i].rm_so == 0 && pm[i].rm_eo == 0)
		continue;
	    if (pm[i].rm_so == -1 && pm[i].rm_eo == -1)
		continue;
	    max = i;
	}

	results = xmlXPathNodeSetCreate(NULL);

	for (i = 0; i <= max; i++) {
	    len = pm[i].rm_eo - pm[i].rm_so;

	    xmlNode *newp = slaxExtMakeTextNode(container, NULL, "match",
					       target + pm[i].rm_so, len);
	    if (newp) {
		xmlXPathNodeSetAdd(results, newp);

		if (last)
		    xmlAddSibling(last, newp);

		 xmlAddChild((xmlNodePtr) container, newp);
		last = newp;
	    }
	}
    }

 done:
    regfree(&reg);
    xmlFree(target_str);
    xmlFree(pattern);

    if (results) {
	ret = xmlXPathNewNodeSetList(results);
	valuePush(ctxt, ret);
	xmlXPathFreeNodeSet(results);

    } else if (return_boolean)
	xmlXPathReturnBoolean(ctxt, FALSE);
    else
	valuePush(ctxt, xmlXPathNewNodeSet(NULL));

    return;

 fail:
    regerror(rc, &reg, buf, sizeof(buf));
    xsltGenericError(xsltGenericErrorContext, "regex error: %s\n", buf);
    goto done;
}

/*
 * Return TRUE if the nodeset or string arguments are empty
 *
 * Usage:  if (slax:empty($set)) { .... }
 */
static void
slaxExtEmpty (xmlXPathParserContext *ctxt, int nargs)
{
    xmlXPathObject *xop;
    int empty = TRUE;		/* Empty until proved otherwise */
    int ndx;

    for (ndx = 0; ndx < nargs; ndx++) {
	xop = valuePop(ctxt);

	if (!empty) {
	    /*
	     * Not empty; we don't need to look anymore, but
	     * we need to keep popping (and freeing) the stack
	     */

	} else if (xop->nodesetval) {
	    if (xop->nodesetval->nodeNr > 1) {
		empty = FALSE;

	    } else if (xop->nodesetval->nodeNr == 1) {
		xmlNodePtr nop = xop->nodesetval->nodeTab[0];
		if (XSLT_IS_RES_TREE_FRAG(nop)) {
		    if (nop->children != NULL)
			empty = FALSE;
		} else 
		    empty = FALSE;
	    }

	} else if (xop->stringval) {
	    if (*xop->stringval)
		empty = FALSE;
	}

	xmlXPathFreeObject(xop);
    }

    if (empty)
	xmlXPathReturnTrue(ctxt);
    else 
	xmlXPathReturnFalse(ctxt);
}

#if defined(HAVE_SYS_SYSCTL_H) && defined(HAVE_SYSCTLBYNAME)
/*
 * Return the value of the given sysctl as a string or integer
 *
 * Usage:   var $value = slax:sysctl("kern.hostname", "s");
 *       "s" -> "string; "i" -> "integer"
 *       nothing else is supported; defaults to "s"
 */
static void
slaxExtSysctl (xmlXPathParserContext *ctxt, int nargs)
{
    xmlChar *name, *type = NULL;

    if (nargs == 0 || nargs > 2) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    if (nargs == 2)
	type = xmlXPathPopString(ctxt);
    name = xmlXPathPopString(ctxt);

    size_t size = 0;

    if (sysctlbyname((char *) name, NULL, &size, NULL, 0) || size == 0) {
    done:
	xsltGenericError(xsltGenericErrorContext,
			 "sysctl error: %s\n", strerror(errno));
	return;
    }

    char *buf = alloca(size);
    if (sysctlbyname((char *) name, buf, &size, NULL, 0))
	goto done;

    if (type && *type == 'i') {
	int value = * (int *) buf;
	const int int_width = 16;
	buf = alloca(int_width);
	snprintf(buf, int_width, "%d", value);
    }

    xmlFree(name);
    if (type)
	xmlFree(type);

    xmlXPathReturnString(ctxt, xmlStrdup((xmlChar *) buf));
}
#endif /* HAVE_SYS_SYSCTL_H */

/*
 * Usage: 
 *     var $substrings = slax:split($pattern, $string, [$limit]);
 *
 * Split string into an array of substrings on the regular expression pattern. 
 * If optional argument limit is specified, then only substrings up to limit 
 * are returned.
 */
static void
slaxExtSplit (xmlXPathParserContext *ctxt, int nargs)
{
    xmlDocPtr container;
    xmlChar *string, *pattern;
    xmlXPathObjectPtr ret;
    xmlNodeSet *results;
    xmlNode *newp, *last = NULL;
    char buf[BUFSIZ], *strp, *endp;
    regex_t reg;
    regmatch_t pmatch[1];
    int rc, limit = -1;

    bzero(&reg, sizeof(reg));

    if (nargs != 2 && nargs != 3) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    if (nargs == 3) {
	limit = (int) xmlXPathPopNumber(ctxt);

	if (limit == 0) 
	    /* If limit is zero or error, then do not consider it */
	    limit = -1;
    }

    string = xmlXPathPopString(ctxt);
    pattern = xmlXPathPopString(ctxt);

    strp = (char *) string;
    endp = strp + strlen(strp);

    results =  xmlXPathNodeSetCreate(NULL);

    /*
     * Create a Result Value Tree container, and register it with RVT garbage 
     * collector. 
     */
    container = slaxMakeRtf(ctxt);
    if (container == NULL)
	goto done;

    rc = regcomp(&reg, (char *) pattern, REG_EXTENDED);
    if (rc)
	goto fail;

    while ((limit == -1 || limit > 1) && 
	   !(rc = regexec(&reg, strp, 1, pmatch, 0))) {

	if (pmatch[0].rm_so == 0 &&  pmatch[0].rm_eo == 0)
	    goto done;
	if (pmatch[0].rm_so == -1 &&  pmatch[0].rm_eo == -1)
	    goto done;

	if (pmatch[0].rm_so == 0 && pmatch[0].rm_eo) {
	    /* Match at start of the string, create empty node */
	    newp = slaxExtMakeTextNode(container, NULL, "split", NULL, 0);
	} else {
	    newp = slaxExtMakeTextNode(container, NULL, "split",
				       strp, pmatch[0].rm_so);
	}

	strp += pmatch[0].rm_eo;

	if (newp) {
	    xmlXPathNodeSetAdd(results, newp);

	    if (last)
		xmlAddSibling(last, newp);

	    xmlAddChild((xmlNodePtr) container, newp);
	    last = newp;
	}

	if (limit != -1)
	    limit -= 1;
    }

    if (rc && rc != REG_NOMATCH)
	goto fail;

    newp = slaxExtMakeTextNode(container, NULL, "split", strp, endp - strp);
    if (newp) {
	xmlXPathNodeSetAdd(results, newp);

	if (last)
	    xmlAddSibling(last, newp);
	xmlAddChild((xmlNodePtr) container, newp);
    }

 done:
    regfree(&reg);
    xmlFree(string);
    xmlFree(pattern);

    ret = xmlXPathNewNodeSetList(results);
    valuePush(ctxt, ret);
    xmlXPathFreeNodeSet(results);
    return;

 fail:

    regerror(rc, &reg, buf, sizeof(buf));
    xsltGenericError(xsltGenericErrorContext, "regex error: %s\n", buf);
    goto done;
}

/*
 * Helper function for slaxExtSyslog() to decode the given priority.
 *
 */
static int
slaxExtDecode (const char *name, CODE *codetab)
{
    CODE *c;

    for (c = codetab; c->c_name; c++)
	if (!strcasecmp(name, c->c_name))
	    return c->c_val;

    return -1;
}

static int
slaxExtDecode2 (const char *name, XCODE *codetab)
{
    XCODE *c;

    for (c = codetab; c->c_name; c++)
	if (!strcasecmp(name, c->c_name))
	    return c->c_val;

    return -1;
}

/*
 * Helper function for slaxExtSyslog() to decode the given priority.
 *
 */
static int
slaxExtDecodePriority (const char *priority)
{
    int pri, sev;
    int fac = LOG_USER;
    char *cp;
	
    if (isdigit((int) *priority)) {
	pri = atoi(priority);

	fac = LOG_FAC(pri);
	sev = LOG_PRI(pri);

	if (fac >= LOG_NFACILITIES) {
	    xsltGenericError(xsltGenericErrorContext,
			     "syslog error: Invalid facility number\n");
	    return -1;
	}

	return pri;
    }

    cp = strchr(priority, '.');

    if (cp) {
	*cp = '\0';
	
	fac = slaxExtDecode2(priority, junos_facilitynames);
	if (fac < 0)
	    fac = slaxExtDecode(priority, facilitynames);
	if (fac < 0) {
	    xsltGenericError(xsltGenericErrorContext,
			     "syslog error: Unknown facility name: %s\n",
			     priority);
	    return -1;
	}

	*cp++ = '.';
    }

    sev = slaxExtDecode(cp ?: priority, prioritynames);
    if (sev < 0) {
	xsltGenericError(xsltGenericErrorContext,
			 "syslog error: Unknown severity name: %s\n",
			 cp ?: priority);
	return -1;
    }

    return (LOG_MAKEPRI_REAL(fac, sev));
}

/*
 * Usage: 
 *      expr slax:syslog(priority, "this message ", $goes, $to, " syslog");
 *
 * Logs the message in syslog with the specified priority. First argument
 * is mandatory and should be priority.
 *
 * The priority may be specified numerically or as a 'facility.severity' pair.
 */
static void
slaxExtSyslog (xmlXPathParserContext *ctxt, int nargs)
{
    xmlChar *strstack[nargs];	/* Stack for strings */
    int ndx, pri;
    xmlChar *priority;
    slax_printf_buffer_t pb;

    if (nargs == 0) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    bzero(&pb, sizeof(pb));

    bzero(strstack, sizeof(strstack));
    for (ndx = nargs - 1; ndx >= 0; ndx--) {
	strstack[ndx] = xmlXPathPopString(ctxt);
	if (xmlXPathCheckError(ctxt))
	    goto bail;
    }

    /*
     * First argument is the priority string.  If we don't
     * have one, we bail.
     */
    priority = strstack[0];
    if (priority == NULL) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    pri = slaxExtDecodePriority((char *) strstack[0]);
    if (pri < 0)
	goto bail;

    for (ndx = 1; ndx < nargs; ndx++) {
	xmlChar *str = strstack[ndx];
	if (str == NULL)
	    continue;

	slaxExtPrintAppend(&pb, str, xmlStrlen(str));
    }

    if (pb.pb_buf && pri) {
	syslog(pri, "%s", pb.pb_buf);
	xmlFree(pb.pb_buf);
    }

bail:
    for (ndx = nargs - 1; ndx >= 0; ndx--) {
	if (strstack[ndx])
	    xmlFree(strstack[ndx]);
    }

    /*
     * Nothing to return, we just push NULL
     */
    valuePush(ctxt, xmlXPathNewNodeSet(NULL));

}

/*
 * This function calculates the diffrence between times 'new' and 'old'
 * by subtracting 'old' from 'new' and put the result in 'diff'.
 */
static int
slaxExtTimeDiff (const struct timeval *new, const struct timeval *old,
	   struct timeval *diff)
{
    long sec = new->tv_sec, usec = new->tv_usec;

    if (new->tv_usec < old->tv_usec) {
	usec += USEC_PER_SEC;
	sec -= 1;
    }

    if (diff) {
	diff->tv_sec = sec - old->tv_sec;
	diff->tv_usec = usec - old->tv_usec;
    }

    return sec - old->tv_sec;
}

static int
slaxExtTimeCompare (const struct timeval *tvp, long secs)
{
    return (tvp->tv_sec < secs) ? TRUE : FALSE;
}

/*
 * Usage: 
 *     var $rc = slax:dampen($tag, max, frequency);
 *
 * dampen function returns true/false based on number of times the
 * function call made by a script. If dampen function is called less
 * than the 'max' times in last 'frequency' of minutes then it will
 * return 'true' which means a success otherwise it will return
 * 'false' which means the call is failed. The values for 'max' and
 * 'frequency' should be passed as greater than zero.
 */
static void
slaxExtDampen (xmlXPathParserContext *ctxt, int nargs)
{
    char filename[MAXPATHLEN + 1];
    char new_filename[MAXPATHLEN + 1];
    char buf[BUFSIZ], *cp, *tag;
    FILE *old_fp = NULL, *new_fp = NULL;
    struct stat sb;
    struct timeval tv, rec_tv, diff;
    int no_of_recs = 0;
    int fd, rc, max;
    long freq_in_secs;
    double freq_double;
    static const char timefmt[] = "%lu.%06lu\n";

    /* Get the time of invocation of this function */
    gettimeofday(&tv, NULL);
    
    if (nargs != 3) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    /* Pop our three arguments */
    freq_double = xmlXPathPopNumber(ctxt);
    freq_in_secs = lrint(freq_double * SECSPERMIN);
    max = (int) xmlXPathPopNumber(ctxt);
    tag = (char *) xmlXPathPopString(ctxt);

    if (max <= 0 || freq_in_secs <= 0) {
	xsltGenericError(xsltGenericErrorContext,
			 "invalid arguments (<= zero)\n");
	xmlXPathReturnFalse(ctxt);
	return;
    }

    /*
     * Creating the absolute filename by appending <record_filename> to
     * 'PATH_VAR_RUN_DIR' and by using the tag, and check whether this
     * file is already present
     */
    snprintf(filename, sizeof(filename), "%s%s-%s.%u", PATH_DAMPEN_DIR,
	     PATH_DAMPEN_FILE, tag, (unsigned) getuid());

    xmlFree(tag);

    rc = stat(filename, &sb);
    if (rc != -1) {
	/* If the file is already present then open it in the read mode */
	old_fp = fopen(filename, "r");
	if (old_fp == NULL) {
	    xsltGenericError(xsltGenericErrorContext,
			     "File open failed for file: %s: %s\n",
			     filename, strerror(errno));
	    xmlXPathReturnFalse(ctxt);
	    return;
	}
    }

    /*
     * Create the file to write the new records with the name of
     * <filename+> and associating a stream with this to write on.
     * We use open/fdopen to allow us to lock it exclusively.
     */
    snprintf(new_filename, sizeof(new_filename), "%s+", filename);
    fd = open(new_filename, DAMPEN_O_FLAGS,
	      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd == -1) {
	xsltGenericError(xsltGenericErrorContext,
			 "File open failed for file: %s: %s\n",
			 new_filename, strerror(errno));
	if (old_fp)
	    fclose(old_fp);
	xmlXPathReturnFalse(ctxt);
	return;
    }

#if !defined(O_EXLOCK) && defined(LOCK_EX)
    if (flock(fd, LOCK_EX) < 0) {
	close(fd);
	if (old_fp)
	    fclose(old_fp);
	xsltGenericError(xsltGenericErrorContext,
			 "File lock failed for file: %s: %s\n",
			 new_filename, strerror(errno));
	xmlXPathReturnFalse(ctxt);
	return;
    }
#endif /* O_EXLOCK */

    new_fp = fdopen(fd, "w+");

    /* If the old records are present corresponding to this tag */
    if (old_fp) {
	while (fgets(buf, sizeof(buf), old_fp)) {
	    if (buf[0] == '#')
		continue;

	    cp = strchr(buf, '.');
	    if (cp == NULL)
		continue;

	    *cp++ = '\0';

	    rec_tv.tv_sec = strtol(buf, NULL, 10);
	    rec_tv.tv_usec = strtol(cp, NULL, 10);

            /*
             * If the time difference between this record and current
             * time stamp then write it into the new file.
             */
            slaxExtTimeDiff(&tv, &rec_tv, &diff);
            if (diff.tv_sec < freq_in_secs
			|| slaxExtTimeCompare(&diff, freq_in_secs)) {

		snprintf(buf, sizeof(buf), timefmt,
			 (unsigned long) rec_tv.tv_sec,
			 (unsigned long) rec_tv.tv_usec);

		if (fputs(buf, new_fp) == EOF) {
		    xsltGenericError(xsltGenericErrorContext,
			     "Write operation failed: %s\n", strerror(errno));
		    fclose(new_fp);
		    fclose(old_fp);
		    xmlXPathReturnFalse(ctxt);
		    return;
		}

		no_of_recs += 1;
	    }
	}
	fclose(old_fp);
    }

    /*
     * Add our entry to the file's contents, if we didn't exceed the max.
     */
    if (no_of_recs < max) {
	snprintf(buf, sizeof(buf), timefmt,
		 (unsigned long) tv.tv_sec, (unsigned long) tv.tv_usec);
	if (fputs(buf, new_fp) == EOF) {
	    xsltGenericError(xsltGenericErrorContext,
			     "Write operation failed: %s\n", strerror(errno));
	    fclose(new_fp);
	    xmlXPathReturnFalse(ctxt);
	    return;
	}

	no_of_recs += 1;
	xmlXPathReturnTrue(ctxt);

    } else {
	xmlXPathReturnFalse(ctxt); /* Too many hits means failure */
    }

    /* Close <filename+> and move <filename+> to <filename> */
    fclose(new_fp);
    rename(new_filename, filename);
}

/*
 *
 */
static void
slaxExtGetInputFlags (xmlXPathParserContext *ctxt, int nargs, unsigned flags)
{
    char *res, *prompt;

    if (nargs != 1) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    prompt = (char *) xmlXPathPopString(ctxt);
    res = slaxInput(prompt, flags);
    xmlFreeAndEasy(prompt);

    xmlXPathReturnString(ctxt, (xmlChar *) res);
}

/*
 * Get a line of input without echoing it
 */
static void
slaxExtGetInput (xmlXPathParserContext *ctxt, int nargs)
{
    slaxExtGetInputFlags(ctxt, nargs, 0);
}

/*
 * Get a line of input without echoing it
 */
static void
slaxExtGetSecret (xmlXPathParserContext *ctxt, int nargs)
{
    slaxExtGetInputFlags(ctxt, nargs, SIF_SECRET);
}

/*
 * Get a line of input with history
 */
static void
slaxExtGetCommand (xmlXPathParserContext *ctxt, int nargs)
{
    slaxExtGetInputFlags(ctxt, nargs, SIF_HISTORY);
}

/*
 * Evaluate a SLAX expression
 */
static void
slaxExtEvaluate (xmlXPathParserContext *ctxt, int nargs)
{
    xmlChar *str = NULL;
    xmlXPathObjectPtr ret = NULL;
    char *sexpr;
    int errors = 0;

    if (ctxt == NULL)
	return;

    if (nargs != 1) {
	xsltPrintErrorContext(xsltXPathGetTransformContext(ctxt), NULL, NULL);
        xsltGenericError(xsltGenericErrorContext,
			 "slax:evalute: invalid number of args %d\n", nargs);
	ctxt->error = XPATH_INVALID_ARITY;
	return;
    }

    str = xmlXPathPopString(ctxt);
    if (!str || !xmlStrlen(str)) {
	/* Return an empty node-set if an empty string is passed in */
	if (str)
	    xmlFree(str);
	valuePush(ctxt, xmlXPathNewNodeSet(NULL));
	return;
    }

    /* Convert a SLAX expression into an XPath one */
    sexpr = slaxSlaxToXpath("slax:evaluate", 1, (const char *) str, &errors);
    if (sexpr == NULL || errors > 0) {
        xsltGenericError(xsltGenericErrorContext,
			 "slax:evalute: invalid expression: %s\n", str);
	valuePush(ctxt, xmlXPathNewNodeSet(NULL));
	xmlFreeAndEasy(sexpr);
	xmlFree(str);
	return;
    }

    xmlFree(str);

    ret = xmlXPathEval((const xmlChar *) sexpr, ctxt->context);
    if (ret)
	valuePush(ctxt, ret);
    else {
	xsltGenericError(xsltGenericErrorContext,
		"slax:evaluate: unable to evaluate expression '%s'\n", sexpr);
	valuePush(ctxt, xmlXPathNewNodeSet(NULL));
    }	

    xmlFree(sexpr);
    return;
}

static void
slaxExtDebug (xmlXPathParserContext *ctxt, int nargs)
{
    xsltTransformContextPtr tctxt;
    xmlNodePtr top, nodep, child;
    xmlDocPtr container;
    xmlXPathObjectPtr ret = NULL;
    int ti, tj, vi, vj;

    if (nargs != 0) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    tctxt = xsltXPathGetTransformContext(ctxt);
    container = slaxMakeRtf(ctxt);
    if (container == NULL)
	goto fail;

    ret = xmlXPathNewNodeSet(NULL);
    if (ret == NULL)
	goto fail;

    top = xmlNewDocNode(container, NULL,
			  (const xmlChar *) "debug", NULL);
    if (top == NULL)
	goto fail;

    xmlAddChild((xmlNodePtr) container, top);
    xmlXPathNodeSetAdd(ret->nodesetval, top);

    nodep = xmlNewDocNode(container, NULL,
			  (const xmlChar *) "templates", NULL);
    if (nodep == NULL)
	goto fail;

    xmlAddChild((xmlNodePtr) top, nodep);

    for (ti = 0, tj = tctxt->templNr - 1; ti < 15 && tj >= 0; ti++, tj--) {
	child = xmlNewDocNode(container, NULL,
			      (const xmlChar *) "template", NULL);

	if (child) {
	    xmlAddChild((xmlNodePtr) nodep, child);
	    if (tctxt->templTab[tj]->name != NULL)
		xmlAddChildContent(container, child, (const xmlChar *) "name",
				   tctxt->templTab[tj]->name);
	    if (tctxt->templTab[tj]->match != NULL)
		xmlAddChildContent(container, child, (const xmlChar *) "match",
				   tctxt->templTab[tj]->match);
	    if (tctxt->templTab[tj]->mode != NULL)
		xmlAddChildContent(container, child, (const xmlChar *) "mode",
				   tctxt->templTab[tj]->mode);
	}
    }

    nodep = xmlNewDocNode(container, NULL,
			  (const xmlChar *) "variables", NULL);
    if (nodep == NULL)
	goto fail;

    xmlAddChild((xmlNodePtr) top, nodep);

    for (vi = 0, vj = tctxt->varsNr - 1; vi < 15 && vj >= 0; vi++, vj--) {
        xsltStackElemPtr cur;

        if (tctxt->varsTab[vj] == NULL)
            continue;

	child = xmlNewDocNode(container, NULL,
			      (const xmlChar *) "scope", NULL);

	if (child) {
	    xmlAddChild(nodep, child);

	    cur = tctxt->varsTab[vj];
	    while (cur != NULL) {
		const char *tag =
		    (cur == NULL || cur->comp == NULL) ? "unknown"
		    : (cur->comp->type == XSLT_FUNC_PARAM) ? "param"
		    : (cur->comp->type == XSLT_FUNC_WITHPARAM) ? "with"
		    : (cur->comp->type == XSLT_FUNC_VARIABLE) ? "var"
		    : "other";

		xmlNodePtr grandchild = xmlNewDocNode(container, NULL,
					    (const xmlChar *) tag, NULL);
		if (grandchild) {
		    xmlAddChild(child, grandchild);
		    if (cur->name != NULL)
			xmlAddChildContent(container, grandchild,
					   (const xmlChar *) "name",
					   cur->name);

		    if (cur->select)
			xmlAddChildContent(container, grandchild,
					   (const xmlChar *) "select",
					   cur->select);

		    if (cur->value) {
			xmlChar *val = xmlXPathCastToString(cur->value);
			if (val) {
			    xmlAddChildContent(container, grandchild,
					       (const xmlChar *) "string",
					       val);
			    xmlFree(val);
			}
		    }
		}

		cur = cur->next;
	    }
	}
    }

 fail:
    if (ret != NULL)
	valuePush(ctxt, ret);
    else
	valuePush(ctxt, xmlXPathNewNodeSet(NULL));
}

struct slaxDocumentOptions {
    xmlCharEncoding sdo_encoding; /* Name of text encoding scheme */
    int sdo_base64;		/* Boolean: do base64 decode */
    xmlChar *sdo_non_xml;	/* Text to replace non-xml characters */
    xmlChar *sdo_rpath;		/* Relative path/base for document */
    int sdo_retain_returns;	/* Do not remove "\r" (keep DOS file hack) */
};

static void
slaxExtDocumentOptionsClear (struct slaxDocumentOptions *sdop)
{
    xmlFreeAndEasy(sdop->sdo_non_xml);
    xmlFreeAndEasy(sdop->sdo_rpath);
}

static void
slaxExtDocumentOptionsSet (struct slaxDocumentOptions *sdop,
			   const xmlChar *name, const xmlChar *value)
{
    if (streq((const char *) name, "format")) {
	if (streq((const char *) value, "base64"))
	    sdop->sdo_base64 = TRUE;
    } else if (streq((const char *) name, "encoding")) {
	sdop->sdo_encoding = xmlParseCharEncoding((const char *) value);
	if (sdop->sdo_encoding == XML_CHAR_ENCODING_NONE)
	    sdop->sdo_encoding = XML_CHAR_ENCODING_UTF8;
    } else if (streq((const char *) name, "relative-path")) {
	sdop->sdo_rpath = xmlStrdup(value);
    } else if (streq((const char *) name, "non-xml")) {
	sdop->sdo_non_xml = xmlStrdup(value);
    } else if (streq((const char *) name, "retain-returns")) {
	sdop->sdo_retain_returns = TRUE;
    }
}

static void
slaxExtDocumentOptions (struct slaxDocumentOptions *sdop,
			xmlXPathObjectPtr xop)
{
    if (xop == NULL)
	return;

    if (xop->type == XPATH_NODESET && xop->nodesetval == NULL)
	return;

    if (xop->nodesetval && xop->nodesetval->nodeNr == 0)
	return;

    if (xop->type == XPATH_STRING) {
	if  (!xop->stringval || !*xop->stringval)
	    return;

    } else if (xop->type == XPATH_NODESET || xop->type == XPATH_XSLT_TREE) {
	xmlNodePtr parent = xop->nodesetval->nodeTab[0];
	if (parent == NULL || parent->children == NULL)
	    return;

	xmlNodePtr child = parent->children;

#if 0
	/* For an RTF, we want the child nodes, not the parent */
	if (XSLT_IS_RES_TREE_FRAG(parent))
	    parent = parent->children;
#endif

	for (child = parent->children; child; child = child->next) {
	    slaxExtDocumentOptionsSet(sdop,
				      (const xmlChar *) xmlNodeName(child),
				      (const xmlChar *) xmlNodeValue(child));
	}
    }
}

static void
slaxExtRewriteNonXmlCharacters (char **datap, size_t *lenp,
				 const xmlChar *non_xml)
{
    char *data = *datap;
    size_t len = *lenp;
    char *cp;
    size_t i;

    if (non_xml[0] == '\0') {
	/* If the string is empty, remove non-xml characters */
	char *op;
	for (i = 0, op = cp = data; i < len; i++, cp++)
	    if (xmlIsChar_ch(*cp))
		*op++ = *cp;
	*op = '\0';
	len = op - data;

    } else if (non_xml[1] == '\0') {
	/* If the string is length == 1 , replace non-xml characters */
	for (i = 0, cp = data; i < len; i++, cp++)
	    if (!xmlIsChar_ch(*cp))
		*cp = non_xml[0];

    } else {
	int count;

	for (i = 0, count = 0, cp = data; i < len; i++, cp++)
	    if (!xmlIsChar_ch(*cp))
		count += 1;

	if (count) {
	    int add = xmlStrlen(non_xml);
	    size_t nlen = len + count * (add - 1);
	    char *newp = xmlMalloc(nlen + 1), *np = newp;

	    if (newp) {
		for (i = 0, count = 0, cp = data; i < len; i++, cp++)
		    if (!xmlIsChar_ch(*cp)) {
			memcpy(np, non_xml, add);
			np += add;
		    } else {
			*np++ = *cp;
		    }

		xmlFree(data);
		data = newp;
		len = nlen;
		data[len] = '\0';
	    }
	}
    }

    *datap = data;
    *lenp = len;
}

static void
slaxExtRemoveReturns (char *data, size_t *lenp)
{
    size_t len = *lenp;
    char *fp, *tp;
    size_t i, delta = 0;

    tp = data;
    fp = memchr(data, '\r', len);
    if (fp == NULL)
	return;

    /* We now know that we're stuck doing the find/copy loop */
    i = fp - data;
    tp = fp;
    for ( ; i < len; i++, fp++) {
	if (*fp == '\r') {
	    delta += 1;
	} else if (fp != tp) {
	    *tp++ = *fp;
	}
    }

    *tp = '\0';

    *lenp -= delta;		/* Mark the removed bytes */
}

static void
slaxExtDocument (xmlXPathParserContext *ctxt, int nargs)
{
    xmlXPathObjectPtr ret = NULL;
    xmlXPathObjectPtr xop = NULL;
    xmlChar *filename = NULL;
    xmlParserInputBufferPtr input;
    char *data;
    struct slaxDocumentOptions sdo;
    size_t len = 0;
    int rc;
    slax_data_list_t list;

    bzero(&sdo, sizeof(sdo));
    sdo.sdo_encoding = XML_CHAR_ENCODING_UTF8;

    if (nargs == 1) {
	filename = xmlXPathPopString(ctxt);

    } else if (nargs == 2) {
	xop = valuePop(ctxt);
	filename = xmlXPathPopString(ctxt);
	slaxExtDocumentOptions(&sdo, xop);

	if (sdo.sdo_rpath) {
	    /* Find the complete URL for our document */
	    xmlChar *file_uri = xmlBuildURI(filename, sdo.sdo_rpath);
	    if (file_uri == NULL)
		goto fail;
	    xmlFree(filename);
	    filename = file_uri;
	}

    } else {
	xmlXPathSetArityError(ctxt);
	return;
    }

    input = xmlParserInputBufferCreateFilename((char *) filename,
					       sdo.sdo_encoding);
    if (input == NULL) {
	slaxLog("slax:document: failed to parse URI ('%s')", filename);
        valuePush(ctxt, xmlXPathNewNodeSet(NULL));
	xmlFree(filename);
	slaxExtDocumentOptionsClear(&sdo);
        return;
    }

    xmlFree(filename);

    slaxDataListInit(&list);

    for (;;) {
	char buf[BUFSIZ];
	
        rc = input->readcallback(input->context, buf, sizeof(buf));
	if (rc <= 0)
	    break;
	slaxDataListAddLen(&list, buf, rc);
    }
    xmlFreeParserInputBuffer(input);

    /*
     * At this point, we've read all our data into the list
     * Now we turn it into a single blob of data
     */
    len = slaxDataListAsCharLen(&list, NULL);
    data = xmlMalloc(len);
    if (data == NULL)
	goto fail;

    slaxDataListAsChar(data, len, &list, NULL);
    len -= 1;			/* Remove trailing NUL */

    /*
     * Now we can apply any post-processing needed
     */
    if (sdo.sdo_base64) {
	size_t dlen;
	char *dec = slaxBase64Decode(data, len, &dlen);
	if (dec) {
	    xmlFree(data);
	    data = dec;
	    len = dlen;
	}
    }

    if (!sdo.sdo_retain_returns)
	slaxExtRemoveReturns(data, &len);

    /*
     * The non-xml value is a single character to replace
     * all non-xml characters.  This is required since XML documents
     * cannot contain some control characters (which is exceedingly lame).
     */
    if (sdo.sdo_non_xml)
	slaxExtRewriteNonXmlCharacters(&data, &len, sdo.sdo_non_xml);

    /* Generate our returnable object */
    ret = xmlXPathWrapString((xmlChar *) data);

    slaxDataListClean(&list);

 fail:
    slaxExtDocumentOptionsClear(&sdo);

    if (ret != NULL)
	valuePush(ctxt, ret);
    else
	valuePush(ctxt, xmlXPathNewNodeSet(NULL));
}

static void
slaxExtBase64Decode (xmlXPathParserContext *ctxt, int nargs)
{
    xmlChar *non_xml = NULL;
    char *data;

    if (nargs == 1) {
	data = (char *) xmlXPathPopString(ctxt);
    } else if (nargs == 2) {
	non_xml = xmlXPathPopString(ctxt);
	data = (char *) xmlXPathPopString(ctxt);
    } else {
	xmlXPathSetArityError(ctxt);
	return;
    }

    if (data == NULL) {
	xsltTransformError(xsltXPathGetTransformContext(ctxt), NULL, NULL,
	      "slax:base64-decode : internal error: data == NULL\n");
	return;
    }

    size_t len = strlen(data), dlen;

    char *dec = slaxBase64Decode(data, len, &dlen);
    if (dec) {
	xmlFree(data);
	data = dec;
	len = dlen;
    }

    if (non_xml && *non_xml)
	slaxExtRewriteNonXmlCharacters(&data, &len, non_xml);

    xmlXPathReturnString(ctxt, (xmlChar *) data);
}

static void
slaxExtBase64Encode (xmlXPathParserContext *ctxt, int nargs)
{
    char *data;

    if (nargs == 1) {
	data = (char *) xmlXPathPopString(ctxt);
    } else {
	xmlXPathSetArityError(ctxt);
	return;
    }

    if (data == NULL) {
	xsltTransformError(xsltXPathGetTransformContext(ctxt), NULL, NULL,
	      "slax:base64-encode : internal error: data == NULL\n");
	return;
    }

    size_t len = strlen(data), dlen;

    char *dec = slaxBase64Encode(data, len, &dlen);
    if (dec) {
	xmlFree(data);
	data = dec;
	len = dlen;
    }

    xmlXPathReturnString(ctxt, (xmlChar *) data);
}

static void
slaxExtValue (xmlXPathParserContext *ctxt, int nargs)
{
    xmlXPathObjectPtr ret = NULL;
    xmlXPathObjectPtr xop = NULL;
    xmlNodePtr parent, child;

    if (nargs != 1) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    xop = valuePop(ctxt);
    if (xop == NULL)
	goto fail;

    if (xop->type == XPATH_BOOLEAN)
	xmlXPathReturnBoolean(ctxt, xop->boolval);

    else if (xop->type == XPATH_NUMBER)
	xmlXPathReturnNumber(ctxt, xop->floatval);

    else if (xop->type == XPATH_STRING)
	valuePush(ctxt, xmlXPathNewString(xop->stringval));
 
    else if (xop->type == XPATH_XSLT_TREE) {
	if (xop->nodesetval == NULL || xop->nodesetval->nodeNr == 0)
	    goto fail;

	parent = xop->nodesetval->nodeTab[0];
	if (parent == NULL || parent->children == NULL)
	    goto fail;

	/* If there's one TEXT child, use it directly */
	child = parent->children;
	if (child && child->next == NULL && child->type == XML_TEXT_NODE)
	    xmlXPathReturnString(ctxt, xmlStrdup(child->content));
	else {
	    ret = xmlXPathNewNodeSet(NULL);
	    if (ret == NULL)
		goto fail;

	    xmlXPathNodeSetAdd(ret->nodesetval, parent);
	    valuePush(ctxt, ret);
	}

    } else if (xop->type == XPATH_NODESET) {
	if (xop->nodesetval == NULL)
	    goto fail;

	valuePush(ctxt, xop);

    } else {
	valuePush(ctxt, xop);
    }

 fail2:
    if (xop)
	xmlXPathFreeObject(xop);
    return;

 fail:
    if (ret != NULL)
	valuePush(ctxt, ret);
    else
	valuePush(ctxt, xmlXPathNewNodeSet(NULL));
    goto fail2;
}

/*
 * Remove illegal non-XML characters from the input string.
 */
static void
slaxExtRemoveNonXmlChars (char *input)
{
    int len, i, count = 0;

    if (!input)
	return;

    len = strlen(input);

    for (i = 0; i < len; i++) {
	/*
	 * Allow newline, carriage return and tab characters but no
	 * other non-xml characters.  One of the most shocking sins
	 * of XML is that it cannot encode non-xml characters, so
	 * we have no choice but to drop them.
	 */
	if (input[i] != '\n' && input[i] != '\r'&& input[i] != '\t') {
	    if (iscntrl((int) input[i])) {
		count++;
		input[i] = '\0';
	    }
	} else if (count) {
	    input[i - count] = input[i];
	    input[i] = '\0';
	}
    }
}

static void
slaxExtMessage (xmlXPathParserContext *ctxt, int nargs,
	     void (*func)(const char *buf))
{
    xmlChar *strstack[nargs];	/* Stack for strings */
    int ndx;
    slax_printf_buffer_t pb;

    bzero(&pb, sizeof(pb));

    bzero(strstack, sizeof(strstack));
    for (ndx = nargs - 1; ndx >= 0; ndx--) {
	strstack[ndx] = xmlXPathPopString(ctxt);
	if (xmlXPathCheckError(ctxt))
	    goto bail;
    }

    for (ndx = 0; ndx < nargs; ndx++) {
	xmlChar *str = strstack[ndx];
	if (str == NULL)
	    continue;

	slaxExtPrintAppend(&pb, str, xmlStrlen(str));
    }

    /*
     * If we made any output, toss it to the function
     */
    if (pb.pb_buf) {
	/*
	 * Remove the non-xml characters from the string. This string is
	 * output for the functions 'jcs:output', 'jcs:progress' and
	 * 'jcs:trace'.
	 */
	slaxExtRemoveNonXmlChars(pb.pb_buf);
	func(pb.pb_buf);
	xmlFree(pb.pb_buf);
    }

bail:
    for (ndx = nargs - 1; ndx >= 0; ndx--) {
	if (strstack[ndx])
	    xmlFree(strstack[ndx]);
    }

    /*
     * Nothing to return, we just push NULL
     */
    valuePush(ctxt, xmlXPathNewNodeSet(NULL));
}    

static void
slaxExtOutputCallback (const char *str)
{
    slaxOutput("%s", str);
}

/*
 * Emit an output message for "slax:output" calls
 *
 * Usage: expr slax:output("this message ", $goes, $to, " the user");
 */
static void
slaxExtOutput (xmlXPathParserContextPtr ctxt, int nargs)
{
    slaxExtMessage(ctxt, nargs, slaxExtOutputCallback);
}

/*
 * This is the callback function that we use to pass trace data
 * up to the caller.
 */
static slaxProgressCallback_t slaxProgressCallback;
static void *slaxProgressCallbackData;
static int slaxExtEmitProgressMessages;

int
slaxEmitProgressMessages (int allow)
{
    int old = slaxExtEmitProgressMessages;
    slaxExtEmitProgressMessages = allow;
    return old;
}

/**
 * Enable progress messages with a callback
 *
 * @func callback function
 * @data opaque data passed to callback
 */
void
slaxProgressEnable (slaxProgressCallback_t func, void *data)
{
    slaxExtEmitProgressMessages = (func != NULL);
    slaxProgressCallback = func;
    slaxProgressCallbackData = data;
}

static void
slaxExtProgressCallback (const char *str)
{
    if (slaxExtEmitProgressMessages) {
	if (slaxProgressCallback)
	    slaxProgressCallback(slaxProgressCallbackData,  "%s", str);
	else
	    slaxOutput("%s", str);
    } else if (slaxTraceCallback)
	slaxTraceCallback(slaxTraceCallbackData, NULL, "%s", str);
    else
	slaxLog("%s", str);
}

/*
 * Emit a progress message for "slax:progress" calls
 *
 * Usage: expr slax:progress($only, "gets displayed if 'detail'", $is-used);
 */
static void
slaxExtProgress (xmlXPathParserContextPtr ctxt, int nargs)
{
    slaxExtMessage(ctxt, nargs, slaxExtProgressCallback);
}

static void
slaxExtTraceCallback (const char *str)
{
    if (slaxTraceCallback)
	slaxTraceCallback(slaxTraceCallbackData, NULL, "%s", str);
    else
	slaxLog("%s", str);
}

/*
 * Emit a trace message for "slax:trace" calls
 *
 * Usage: expr slax:trace($only, "gets displayed if 'detail'", $is-used);
 */
static void
slaxExtTrace (xmlXPathParserContextPtr ctxt, int nargs)
{
    slaxExtMessage(ctxt, nargs, slaxExtTraceCallback);
}

/*
 * An ugly attempt to seed the random number generator with the best
 * value possible.  Ugly, but localized ugliness.
 */
void
slaxInitRandomizer (void)
{
#if defined(HAVE_SRANDDEV)
    sranddev();

#elif defined(HAVE_SRAND)
#if defined(HAVE_GETTIMEOFDAY)

    struct timeval tv;
    int seed;

    gettimeofday(&tv, NULL);
    seed = ((int) tv.tv_sec) + ((int) tv.tv_usec);
    srand(seed);

#else /* HAVE_GETTIMEOFDAY */
    srand((int) time(NULL));

#endif /* HAVE_GETTIMEOFDAY */
#else /* HAVE_SRAND */
    fprintf(stderr, "could not initialize random number generator\n");
#endif /* HAVE_SRAND */
}
 
/*
 * Register our extension functions.
 */
int
slaxExtRegisterOther (const char *namespace)
{
    if (namespace == NULL)
	namespace = SLAX_URI;

    slaxRegisterFunction(namespace, "break-lines", slaxExtBreakLines);
    slaxRegisterFunction(namespace, "break_lines", slaxExtBreakLines); /*OLD*/
    slaxRegisterFunction(namespace, "dampen", slaxExtDampen);
    slaxRegisterFunction(namespace, "empty", slaxExtEmpty);
    slaxRegisterFunction(namespace, "first-of", slaxExtFirstOf);
    slaxRegisterFunction(namespace, "get-command", slaxExtGetCommand);
    slaxRegisterFunction(namespace, "get-input", slaxExtGetInput);
    slaxRegisterFunction(namespace, "get-secret", slaxExtGetSecret);
    slaxRegisterFunction(namespace, "getsecret", slaxExtGetSecret); /*OLD*/
    slaxRegisterFunction(namespace, "input", slaxExtGetInput); /*OLD*/
    slaxRegisterFunction(namespace, "is-empty", slaxExtEmpty);
    slaxRegisterFunction(namespace, "output", slaxExtOutput);
    slaxRegisterFunction(namespace, "progress", slaxExtProgress);
    slaxRegisterFunction(namespace, "printf", slaxExtPrintf);
    slaxRegisterFunction(namespace, "regex", slaxExtRegex);
    slaxRegisterFunction(namespace, "sleep", slaxExtSleep);
    slaxRegisterFunction(namespace, "split", slaxExtSplit);
    slaxRegisterFunction(namespace, "sprintf", slaxExtPrintf);
#if defined(HAVE_SYS_SYSCTL_H) && defined(HAVE_SYSCTLBYNAME)
    slaxRegisterFunction(namespace, "sysctl", slaxExtSysctl);
#endif
    slaxRegisterFunction(namespace, "syslog", slaxExtSyslog);
    slaxRegisterFunction(namespace, "trace", slaxExtTrace);

    return 0;
}

/* ---------------------------------------------------------------------- */

/**
 * Registers the SLAX extensions
 */
void
slaxExtRegister (void)
{
    slaxRegisterElement(SLAX_URI, ELT_TRACE,
			slaxTraceCompile, slaxTraceElement);

    slaxRegisterElement(SLAX_URI, ELT_WHILE,
			slaxWhileCompile, slaxWhileElement);

    slaxRegisterFunction(SLAX_URI, FUNC_BUILD_SEQUENCE, slaxExtBuildSequence);

    slaxRegisterFunction(SLAX_URI, "base64-decode", slaxExtBase64Decode);
    slaxRegisterFunction(SLAX_URI, "base64-encode", slaxExtBase64Encode);
    slaxRegisterFunction(SLAX_URI, "debug", slaxExtDebug);
    slaxRegisterFunction(SLAX_URI, "document", slaxExtDocument);
    slaxRegisterFunction(SLAX_URI, "evaluate", slaxExtEvaluate);
    slaxRegisterFunction(SLAX_URI, "value", slaxExtValue);

    slaxExtRegisterOther(NULL);

    slaxMvarRegister();
}
