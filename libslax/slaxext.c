/*
 * $Id$
 *
 * Copyright (c) 2010, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
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

#define SYSLOG_NAMES		/* Ask for all the names and typedef CODE */
#include <sys/syslog.h>

#include <libxslt/xsltutils.h>
#include <libxslt/transform.h>
#include <libxml/xpathInternals.h>

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

xmlChar slax_empty_string[1]; /* A non-const empty string */

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

	save_insert = ctxt->insert;
	ctxt->insert = (xmlNodePtr) container;

	/* Apply the template code inside the element */
	xsltApplyOneTemplate(ctxt, node,
			      inst->children, NULL, NULL);
	ctxt->insert = save_insert;

	value = xmlXPathNewValueTree((xmlNodePtr) container);
    } else return;

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
    case XPATH_NODESET:
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
    xmlChar *sel;
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

    /* Precompile the test attribute */
    sel = xmlGetNsProp(inst, (const xmlChar *) ATT_TEST, NULL);
    if (sel == NULL) {
	xsltGenericError(xsltGenericErrorContext,
			 "while: missing test attribute\n");
	return NULL;
    }

    comp->wp_test = xmlXPathCompile(sel);
    if (comp->wp_test == NULL) {
	xsltGenericError(xsltGenericErrorContext,
			 "while: test attribute does not compile: %s\n", sel);
	xmlFree(sel);
	return NULL;
    }

    xmlFree(sel);

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

static void
slaxExtBuildSequence (xmlXPathParserContextPtr ctxt, int nargs)
{
    long long num, low, high, step = 1;
    xsltTransformContextPtr tctxt;
    xmlNodePtr nodep;
    xmlDocPtr container;
    xmlXPathObjectPtr ret = NULL;
    char buf[BUFSIZ];

    if (nargs != 2) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    /* Pop args in reverse order */
    high = xmlXPathPopNumber(ctxt);
    if (xmlXPathCheckError(ctxt))
	return;

    low = xmlXPathPopNumber(ctxt);
    if (xmlXPathCheckError(ctxt))
	return;

    if (high < low)
	step = -1;

    slaxLog("build-sequence: %qd ... %qd + %qd", low, high, step);

    /* Return a result tree fragment */
    tctxt = xsltXPathGetTransformContext(ctxt);
    if (tctxt == NULL) {
	xsltTransformError(xsltXPathGetTransformContext(ctxt), NULL, NULL,
	      "exslt:tokenize : internal error tctxt == NULL\n");
	goto fail;
    }

    container = xsltCreateRVT(tctxt);
    if (container == NULL)
	goto fail;
    
    xsltRegisterLocalRVT(tctxt, container);
    ret = xmlXPathNewNodeSet(NULL);
    if (ret == NULL)
	goto fail;

    for (num = low; num <= high; num += step) {
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

	if (xop->type == XPATH_STRING && 
	    (!xop->stringval || !*xop->stringval)) {
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
		    tag[tlen + 1] = '\0';

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
slaxExtMakeTextNode (xmlNs *nsp, const char *name,
		     const char *content, int len)
{
    xmlNode *newp = xmlNewNode(nsp, (const xmlChar *) name);

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
	clone = slaxExtMakeTextNode(nsp, name, NULL, 0);
	if (clone) {
	    xmlXPathNodeSetAdd(results, clone);
	    xmlAddChild((xmlNodePtr) container, clone);
	}
	return;
    }

    cp = strchr(content, '\n');
    if (cp == NULL) {
	clone = slaxExtMakeTextNode(nsp, name, content, strlen(content));
	if (clone) {
	    xmlXPathNodeSetAdd(results, clone);
	    xmlAddChild((xmlNodePtr) container, clone);
	}
	return;
    }

    sp = content;
    last = NULL;

    for (;;) {
	clone = slaxExtMakeTextNode(nsp, name, sp, cp - sp);
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
    xsltTransformContextPtr tctxt;
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
    tctxt = xsltXPathGetTransformContext(ctxt);
    container = xsltCreateRVT(tctxt);
    xsltRegisterLocalRVT(tctxt, container);

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
    xsltTransformContextPtr tctxt;
    xmlXPathObjectPtr ret;
    xmlDocPtr container;

    if (nargs != 2) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    xmlChar *target_str = xmlXPathPopString(ctxt);
    char *target = (char *) target_str;
    xmlChar *pattern = xmlXPathPopString(ctxt);
    
    xmlNodeSet *results = xmlXPathNodeSetCreate(NULL);

    /*
     * Create a Result Value Tree container, and register it with RVT garbage 
     * collector. 
     */
    tctxt = xsltXPathGetTransformContext(ctxt);
    container = xsltCreateRVT(tctxt);
    xsltRegisterLocalRVT(tctxt, container);

    regex_t reg;
    int nmatch = 9;
    regmatch_t pm[nmatch];
    int rc;

    bzero(&pm, sizeof(pm));

    rc = regcomp(&reg, (const char *) pattern, REG_EXTENDED);
    if (rc)
	goto fail;

    rc = regexec(&reg, target, nmatch, pm, 0);
    if (rc && rc != REG_NOMATCH)
	goto fail;
   
    if (rc != REG_NOMATCH) {
	int i, len, max = 0;
	xmlNode *last = NULL;

	for (i = 0; i < nmatch; i++) {
	    if (pm[i].rm_so == 0 && pm[i].rm_eo == 0)
		continue;
	    if (pm[i].rm_so == -1 && pm[i].rm_eo == -1)
		continue;
	    max = i;
	}

	for (i = 0; i <= max; i++) {
	    len = pm[i].rm_eo - pm[i].rm_so;

	    xmlNode *newp = slaxExtMakeTextNode(NULL, "match",
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
	    if (xop->nodesetval->nodeNr != 0)
		empty = FALSE;

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
    xsltTransformContextPtr tctxt;
    xmlDocPtr container;
    xmlChar *string, *pattern;
    xmlXPathObjectPtr ret;
    xmlNodeSet *results;
    xmlNode *newp, *last = NULL;
    char buf[BUFSIZ], *strp, *endp;
    regex_t reg;
    regmatch_t pmatch[1];
    int rc, limit = -1;

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
    tctxt = xsltXPathGetTransformContext(ctxt);
    container = xsltCreateRVT(tctxt);
    xsltRegisterLocalRVT(tctxt, container);

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
	    newp = slaxExtMakeTextNode(NULL, "split", NULL, 0);
	} else {
	    newp = slaxExtMakeTextNode(NULL, "split", strp, pmatch[0].rm_so);
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

    newp = slaxExtMakeTextNode(NULL, "split", strp, endp - strp);
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

    return (LOG_MAKEPRI(fac, sev));
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
	goto bail;
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
slaxExtTimeCompare (const struct timeval *tv, double limit)
{
    double t = tv->tv_sec + tv->tv_usec * USEC_PER_SEC;

    return (t < limit) ? TRUE : FALSE;
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
    int fd, rc, max, freq_int;
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
    freq_int = (int) floor(freq_double);
    max = (int) xmlXPathPopNumber(ctxt);
    tag = (char *) xmlXPathPopString(ctxt);

    if (max <= 0 || freq_int <= 0) {
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
	if (!old_fp) {
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
	xmlXPathReturnFalse(ctxt);
	return;
    }

#if !defined(O_EXLOCK) && defined(LOCK_EX)
    if (flock(fd, LOCK_EX) < 0) {
	close(fd);
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
            if (diff.tv_sec < (freq_int * 60)
			|| slaxExtTimeCompare(&diff, freq_double)) {

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
    slaxRegisterFunction(namespace, "input", slaxExtGetInput); /*OLD*/
    slaxRegisterFunction(namespace, "is-empty", slaxExtEmpty);
    slaxRegisterFunction(namespace, "printf", slaxExtPrintf);
    slaxRegisterFunction(namespace, "regex", slaxExtRegex);
    slaxRegisterFunction(namespace, "sleep", slaxExtSleep);
    slaxRegisterFunction(namespace, "split", slaxExtSplit);
#if defined(HAVE_SYS_SYSCTL_H) && defined(HAVE_SYSCTLBYNAME)
    slaxRegisterFunction(namespace, "sysctl", slaxExtSysctl);
#endif
    slaxRegisterFunction(namespace, "syslog", slaxExtSyslog);

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

    slaxExtRegisterOther(NULL);

    slaxMvarRegister();
}
