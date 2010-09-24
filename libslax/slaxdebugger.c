/*
 * $Id: debugger.c 384736 2010-06-16 04:41:59Z deo $
 *
 * Copyright (c) 2010, Juniper Networks, Inc.
 * All rights reserved.
 *
 * Debugger for scripts
 *
 * The debugger ties into the libxslt debugger hooks via the
 * xsltSetDebuggerCallbacks() API.  This is a fairly weak and
 * undocumented API, so it is very likely that I am not using it
 * correctly.
 *
 * The API has three callbacks, one called to add stack frames
 * (addFrame), one called to drop (pop) stack frames (dropFrame), and
 * one called as each instruction is executed (Handler), where
 * "instruction" is an XSLT element.  Note that there is not always a
 * 1:1 mapping between SLAX statements and XSLT instructions due to
 * the nature of SLAX.
 *
 * The addFrame callback takes two arguments, a template and an
 * instruction.  The template node is NULL when executing initializers
 * for global variables, and when executing the <xsl:call-template>
 * instruction.  The instruction should never be NULL.
 *
 * The dropFrame callback takes no arguments, but it only called when
 * the corresponding addFrame call returns non-zero.  We always record
 * frames. Our dropFrame just discards the top stack frame.
 *
 * The Handler callback is called for each instruction before it is
 * executed.  It takes four parameters, the instruction being
 * executed, the current context node (the node being examined aka
 * "."), the template being executed (which the instruction is part
 * of), and the XSLT transformation context.  If we are evaluating an
 * initializer for a global variable, both the template and the
 * context node will be NULL.  The instruction and context will never be
 * NULL.
 *
 * So this simple API puts all the work on our side of the fence,
 * which is fine.  Missing features include a real prototype for
 * xsltSetDebuggerCallbacks() and a means of passing opaque data
 * through the API to the callbacks, without which we have to resort
 * to global data.
 *
 * Our data consists of three items: slaxDebugState represents the
 * current state of the debugger; slaxDebugStack is the stack of stack
 * frames maintained by the addFrame and dropFrame callbacks; and
 * slaxDebugBreakpoint is the list of breakpoints.
 *
 * At our upper API, we have slaxDebugRegister() which turns on the
 * debugger and records callbacks for getting input and sending
 * output.  slaxDebugSetStylesheet() and slaxDebugSetIncludes() set
 * the current stylesheet and include/import search path.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/queue.h>

#include <libxml/xmlsave.h>
#include <libxslt/variables.h>

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "config.h"

#define MAXARGS	256

/*
 * Information about the current point of debugging
 */
typedef struct slaxDebugState_s {
    xsltStylesheetPtr ds_stylesheet; /* Current top-level stylesheet */
    xmlNodePtr ds_inst;	/* Current libxslt node being executed */
    xmlNodePtr ds_last_inst;	/* Last libxslt node being executed */
    xmlNodePtr ds_node;	/* Current context node */
    xsltTemplatePtr ds_template; /* Current template being executed */
    xsltTransformContextPtr ds_ctxt; /* Transformation context */
    int ds_count;		     /* Command count */
    int ds_flags;		     /* Global state flags */
} slaxDebugState_t;

/* Flags for ds_flags */
#define DSF_NEXT 	(1<<0)	/* Step over next call */
#define DSF_DISPLAY	(1<<1)	/* Show instruction before next command */
#define DSF_CALLFLOW	(1<<2)	/* Report call flow */

slaxDebugState_t slaxDebugState;
const char **slaxDebugIncludes;

/* Arguments to our command functions */
#define DC_ARGS \
        slaxDebugState_t *statep UNUSED, \
	const char *commandline UNUSED, \
	const char **argv UNUSED

/*
 * Commands supported in debugger
 */
typedef void (*slaxDebugCommandFunc_t)(DC_ARGS);

typedef struct slaxDebugCommand_s {
    const char *dc_command;	/* Command name */
    slaxDebugCommandFunc_t dc_func; /* Function pointer */
    const char *dc_help;	/* Help text */
} slaxDebugCommand_t;
static slaxDebugCommand_t slaxDebugCommandTable[];

/*
 * Doubly linked list to store the templates call sequence
 */
typedef struct slaxDebugStackFrame_s {
    TAILQ_ENTRY(slaxDebugStackFrame_s) dsf_link;
    xsltTemplatePtr dsf_template; /* Template (parent) */
    xmlNodePtr dsf_inst;	  /* Instruction (code) */
    unsigned dsf_flags; /* DSFF_* flags for this stack frame */
} slaxDebugStackFrame_t;

/* Flags for dsf_flags */
#define DSFF_STOPWHENPOP	(1<<0) /* Stop when this frame is popped */
#define DSFF_PARAM		(1<<1) /* Frame is a with-param insn */

TAILQ_HEAD(slaxDebugStack_s, slaxDebugStackFrame_s) slaxDebugStack;

/*
 * Double linked list to hold the breakpoints
 */
typedef struct slaxDebugBreakpoint_s {
    TAILQ_ENTRY(slaxDebugBreakpoint_s) dbp_link;
    xmlNodePtr dbp_inst;	/* Node we are breaking on */
    uint dbp_num;		/* Breakpoint number */
} slaxDebugBreakpoint_t;

TAILQ_HEAD(slaxDebugBpList_s, slaxDebugBreakpoint_s) slaxDebugBreakpoints;

static uint slaxDebugBreakpointNumber;

/*
 * Various display mode 
 */
typedef enum slaxDebugDisplayMode_s {
    CLI_MODE,
    EMACS_MODE,
} slaxDebugDisplayMode_t;

slaxDebugDisplayMode_t slaxDebugDisplayMode;

typedef struct slaxDebugOutputInfo_s {
    const char *doi_prefix;	/* Content to emit before each line */
    int doi_plen;		/* Length of the prefix */
} slaxDebugOutputInfo_t;

/* Callback functions for input and output */
static slaxDebugInputCallback_t slaxDebugInputCallback;
static slaxDebugOutputCallback_t slaxDebugOutputCallback;
static xmlOutputWriteCallback slaxDebugIOWrite;

static const xmlChar *null = (const xmlChar *) "";
#define NAME(_x) (((_x) && (_x)->name) ? (_x)->name : null)

/**
 * Use the input callback to get data
 * @prompt the prompt to be displayed
 */
static char *
slaxDebugInput (const char *prompt)
{
    char *res;
    /* slaxTrace("slaxDebugInput: -> [%s]", prompt); */
    res = slaxDebugInputCallback ? slaxDebugInputCallback(prompt) : NULL;
    /* slaxTrace("slaxDebugInput: <- [%s]", res ?: "null"); */
    return res;
}

/**
 * Use the callback to output a string
 * @fmt printf-style format string
 */
static void
slaxDebugOutput (const char *fmt, ...)
{
    if (slaxDebugOutputCallback) {
	char buf[BUFSIZ];
	va_list vap;

	va_start(vap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, vap);
	va_end(vap);

	/* slaxTrace("slaxDebugOutput: [%s]", buf); */
	slaxDebugOutputCallback("%s\n", buf);
    }
}

/**
 * 
 */
static void
slaxDebugOutputNode (xmlNodePtr node, const char *prefix)
{
    slaxDebugOutputInfo_t info;
    xmlSaveCtxtPtr handle;

    bzero(&info, sizeof(info));
    info.doi_prefix = prefix;
    info.doi_plen = prefix ? strlen(prefix) : 0;

    handle = xmlSaveToIO(slaxDebugIOWrite, NULL, NULL, NULL,
		 XML_SAVE_FORMAT | XML_SAVE_NO_DECL | XML_SAVE_NO_XHTML);
    if (handle) {
        xmlSaveTree(handle, node);
        xmlSaveFlush(handle);
	xmlSaveClose(handle);
    }
}

#if 0
static void
slaxDebugOutputElement (xmlNode node, const char *prefix)
{
    char buf[BUFSIZ], *cp = buf, *ep = buf + bufsiz;

    cp += snprintf(cp, ep - cp, "<%s", node->name);
    
}
#endif

/**
 * Return the current debugger state object.  This is currently
 * just a single global instance, but if we ever support threads,
 * it will be in thread-specific data.
 * @return current debugger state object
 */
static slaxDebugState_t *
slaxDebugGetState (void)
{
    return &slaxDebugState;
}

static xsltStylesheetPtr
slaxDebugGetFile (xsltStylesheetPtr style, const char *filename)
{
    char buf[BUFSIZ];
    char *slash;
    const char **inc;

    for ( ; style; style = style->next) {
	if (streq((const char *) style->doc->URL, filename))
	    return style;

	for (inc = slaxDebugIncludes; inc && *inc; inc++) {
	    snprintf(buf, sizeof(buf), "%s%s", *inc, filename);
	    if (streq((const char *) style->doc->URL, buf))
		return style;
	}
    
	/*
	 * Just the filname match
	 */
	slash = strrchr((const char *) style->doc->URL, '/');
	if (slash && streq(slash + 1, filename))
	    return style;

	if (style->imports) {
	    xsltStylesheetPtr answer;
	    answer = slaxDebugGetFile(style->imports, filename);
	    if (answer)
		return answer;
	}
    }

    return NULL;
}

/*
 * Return XML node for given template name  
 */
static xmlNodePtr
slaxDebugGetTemplateNodebyName (slaxDebugState_t *statep, const char *name)
{
    xsltTemplatePtr tmp;

    for (tmp = statep->ds_stylesheet->templates; tmp; tmp = tmp->next) {
	if ((tmp->match && streq((const char *) tmp->match, name))
	    || (tmp->name && streq((const char *) tmp->name, name)))
	    return tmp->elem;
    }

    return NULL;
}

static xmlNodePtr
slaxDebugGetNodeByLine (xmlNodePtr node, int lineno)
{
    for ( ; node; node = node->next) {
	if (lineno == xmlGetLineNo(node))
	    return node;

	if (node->children) {
	    xmlNodePtr answer;
	    answer = slaxDebugGetNodeByLine(node->children, lineno);
	    if (answer)
		return answer;
	}
    }

    return NULL;
}

/*
 * Return xmlnode for the given line number
 */
static xmlNodePtr
slaxDebugGetNode (slaxDebugState_t *statep,
		  const char *filename, int lineno)
{
    xsltStylesheetPtr style;
    xmlNodePtr node;

    style = slaxDebugGetFile(statep->ds_stylesheet, filename);
    if (style == NULL)
	return NULL;

    node = slaxDebugGetNodeByLine(style->doc->children, lineno);

    return node;
}

/*
 * Return xmlnode for the given script:linenum
 */
static xmlNodePtr
slaxDebugGetScriptNode (slaxDebugState_t *statep, const char *arg)
{
    xsltStylesheetPtr style = NULL;
    xmlNodePtr node;
    char *script = ALLOCADUP(arg);
    char *cp, *endp;
    int lineno;

    cp = strchr(script, ':');
    if (cp == NULL)
	return NULL;
    *cp++ = '\0';

    lineno = strtol(cp, &endp, 0);
    if (lineno <= 0 || cp == endp)
	return NULL;

    style = slaxDebugGetFile(statep->ds_stylesheet, script);
    if (!style)
	return NULL;

    /*
     * Get the node for the given linenumber from the stylesheet
     */
    node = slaxDebugGetNodeByLine((xmlNodePtr) style->doc, lineno);

    return node;
}

/*
 * Print the given nodeset. First we print the nodeset in a temp file.
 * Then read that file and send the the line to mgd one by one.
 */
static void
slaxDebugPrintNodeset (xmlNodeSetPtr nodeset)
{
    int i;

    for (i = 0; i < nodeset->nodeNr; i++)
	slaxDebugOutputNode(nodeset->nodeTab[i], "");
}

/*
 * Print the given XPath object
 */
static void
slaxDebugPrintXpath (xmlXPathObjectPtr xpath)
{
    if (xpath == NULL)
	return;

    switch (xpath->type) {
    case XPATH_BOOLEAN:
	slaxDebugOutput("[boolean] %s", xpath->boolval ? "true" : "false");
	break;

    case XPATH_NUMBER:
	slaxDebugOutput("[number] %lf", xpath->floatval);
	break;

    case XPATH_STRING:
	if (xpath->stringval)
	    slaxDebugOutput("[string] %s", xpath->stringval);
	break;

    case XPATH_NODESET:
	slaxDebugOutput("[node-set]%s (%d)",
			xpath->nodesetval ? "" : " [null]",
			xpath->nodesetval ? xpath->nodesetval->nodeNr : 0);
	if (xpath->nodesetval)
	    slaxDebugPrintNodeset(xpath->nodesetval);
	break;

    case XPATH_XSLT_TREE:
	slaxDebugOutput("[rtf]%s (%d)",
			xpath->nodesetval ? "" : " [null]",
			xpath->nodesetval ? xpath->nodesetval->nodeNr : 0);
	if (xpath->nodesetval)
	    slaxDebugPrintNodeset(xpath->nodesetval);
	break;

    default:
	break;
    }
}
 
/*
 * Clear all breakpoints
 */
static void
slaxDebugClearBreakpoints (void)
{
    slaxDebugBreakpoint_t *dbp;

    for (;;) {
	dbp = TAILQ_FIRST(&slaxDebugBreakpoints);
	if (dbp == NULL)
	    break;
	TAILQ_REMOVE(&slaxDebugBreakpoints, dbp, dbp_link);
    }

    slaxDebugBreakpointNumber = 0;
}

/*
 * Clear stacktrace, basically delete the linked list
 */
static void
slaxDebugClearStacktrace (void)
{
    slaxDebugStackFrame_t *dsfp;

    for (;;) {
	dsfp = TAILQ_FIRST(&slaxDebugStack);
	if (dsfp == NULL)
	    break;
	TAILQ_REMOVE(&slaxDebugStack, dsfp, dsf_link);
    }

    slaxDebugBreakpointNumber = 0;
}

static void
slaxDebugMakeRelativePath (const char *src_f, const char *dest_f,
			   char *relative_path, int size)
{
    int slen, dlen, i, j, slash, n;
    const char *f;

    slen = strlen(src_f);
    dlen = strlen(dest_f);

    i = 0;
    j = 0;
    while (src_f[i] && dest_f[j] && src_f[i] == dest_f[j]) {
        i++;
        j++;
    }

    /*
     * Both the file are same, just return the filename
     */
    if (!src_f[i]) {
        f = strrchr(src_f, '/');
        f = f ? f + 1 : src_f;
        snprintf(relative_path, size, "%s", f);
        return;
    }

    /*
     * Find number of / in src_f
     */
    slash = 0;
    while (src_f[i]) {
        if ((src_f[i] == '/') && (src_f[i + 1] != '/'))
            slash++;
        i++;
    }

    /*
     * Both are in same dir, just return file
     */
    if (!slash) {
        snprintf(relative_path, size, "%s", (dest_f + j));
        return;
    }

    /*
     * Append number of ../
     */
    n = 0;
    while (slash) {
        n += snprintf(relative_path + n, size - n, "../");
        slash--;
    }

    snprintf(relative_path + n, size, "%s", (dest_f + j));
}

/*
 * Return the line for given linenumber from file.
 *
 * Very inefficient way, we read the file from begining till we reach the 
 * given linenumber, should be fine for now. May be we will optimize when we
 * implement 'list' command.
 */
static int
slaxDebugPrintScriptLine (slaxDebugState_t *statep, const char *filename,
			  int start, int stop)
{
    FILE *fp;
    int count = 0;
    const char *cp;
    char line[BUFSIZ];
    int len;

    if (filename == NULL || start == 0 || stop == 0)
	return TRUE;

    fp = fopen(filename, "r");
    if (fp == NULL) {
	/* 
	 * do not print error message, the line won't get displayed that is 
	 * fine
	 */
	return TRUE;
    }

    while (fgets(line, sizeof(line), fp)) {
	/*
	 * Since we are only reading line_size per iteration, if the current 
	 * line length is more than line_size then we may read same line in
	 * more than one iteration, so increment the count only when the last 
	 * character is '\n'
	 */
	if (line[strlen(line) - 1] == '\n')
	    count++;

	if (count == start)
	    break;
    }

    cp = strrchr(filename, '/');
    cp = cp ? cp + 1 : filename;

    len = strlen(line);
    if (len > 0 && line[len - 1] == '\n')
	line[len - 1] = '\0';

    while (count < stop) {
	slaxDebugOutput("%s:%d: %s", cp, count, line);

	if (fgets(line, sizeof(line), fp) == NULL)
	    break;

	len = strlen(line);
	if (len > 0 && line[len - 1] == '\n') {
	    line[len - 1] = '\0';
	    count++;
	}
    }

    if (slaxDebugDisplayMode == EMACS_MODE) {
	char rel_path[BUFSIZ];

	/*
	 * In emacs path should be relative to remote default directory,
	 * so print the relative path of the current file from main stylesheet
	 */
	cp = (const char *) statep->ds_stylesheet->doc->URL;
	slaxDebugMakeRelativePath(cp, filename, rel_path, sizeof(rel_path));
	
	slaxDebugOutput("%c%c%s:%d:0", 26, 26, rel_path, start);
    }

    fclose(fp);
    return FALSE;
}

/**
 * Split the input given by users into tokens
 * @buf buffer to split
 * @args array for args
 * @maxargs length of args
 * @return number of args
 * @bug needs to handle escapes and quotes
 */
static int
slaxDebugSplitArgs (char *buf, const char **args, int maxargs)
{
    int i;
    char *s;

    for (i = 0; (s = strsep(&buf, " \n")) && i < maxargs; i++) {
	args[i] = s;

	if (*s == '\0') {
	    args[i] = s;
	    break;
	}
    }

    args[i] = NULL;
    return i;
}

/*
 * Check if the breakpoint is available for curnode being executed
 */
static int
slaxDebugCheckBreakpoint (slaxDebugState_t *statep UNUSED,
			  xmlNodePtr node, int reached)
{
    slaxDebugBreakpoint_t *dbp;

    TAILQ_FOREACH(dbp, &slaxDebugBreakpoints, dbp_link) {
	if (dbp->dbp_inst == node) {
	    if (reached) {
		slaxDebugOutput("Reached breakpoint %d, at %s:%d", 
				dbp->dbp_num, node->doc->URL,
				xmlGetLineNo(node));
		xsltSetDebuggerStatus(XSLT_DEBUG_INIT);
	    }
	    return TRUE;
	}
    }

    return FALSE;
}

/*
 * 'break' command
 */
static void
slaxDebugCmdBreak (DC_ARGS)
{
    xmlNodePtr node = NULL;
    slaxDebugBreakpoint_t *bp;
    int lineno;

    if (argv[1] == NULL) {
	/*
	 * Break on the current line
	 */
	node = statep->ds_node;

    } else if (strchr(argv[1], ':')) {
	/*
	 * scriptname:linenumber format
	 */
	node = slaxDebugGetScriptNode(statep, argv[1]);

	/* If it wasn't foo:34, maybe it's foo:bar */
	if (node == NULL)
	    node = slaxDebugGetTemplateNodebyName(statep, argv[1]);

    } else if ((lineno = atoi(argv[1])) > 0) {
	/*
	 * simply linenumber, put breakpoint in the cur script
	 */
	node = slaxDebugGetNode(statep,
				(const char *) statep->ds_inst->doc->URL,
				lineno);
    } else {
	/*
	 * template name?
	 */
	node = slaxDebugGetTemplateNodebyName(statep, argv[1]);
    }

    if (node == NULL)
	return;

    if (slaxDebugCheckBreakpoint(statep, node, FALSE)) {
	slaxDebugOutput("Duplicate breakpoint");
	return; 
    }

    /*
     * Create a record of the breakpoint and add it to the list
     */
    bp = xmlMalloc(sizeof(*bp));
    if (bp == NULL)
	return;

    bzero(bp, sizeof(*bp));
    bp->dbp_num = ++slaxDebugBreakpointNumber;
    bp->dbp_inst = node;
    TAILQ_INSERT_TAIL(&slaxDebugBreakpoints, bp, dbp_link);

    slaxDebugOutput("Breakpoint %d at file %s, line %d",
		    bp->dbp_num, 
		    node->doc->URL, xmlGetLineNo(node));  
}

/*
 * 'continue' command
 */
static void
slaxDebugCmdContinue (DC_ARGS)
{
    xsltSetDebuggerStatus(XSLT_DEBUG_CONT);
    statep->ds_flags |= DSF_DISPLAY;
}

/*
 * 'delete' command
 */
static void
slaxDebugCmdDelete (DC_ARGS)
{
    static const char prompt[] = "Delete all breakpoints? (yes/no) ";
    uint num = 0;
    slaxDebugBreakpoint_t *dbpp;
    char *cp;

    /*
     * If no argument is provided ask for confirmation and delete all 
     * breakpoints
     */
    if (argv[1] == NULL) {
	cp = slaxDebugInput(prompt);

	if (!streq(cp, "y") && !streq(cp, "yes"))
	    return;

	slaxDebugClearBreakpoints();
	slaxDebugOutput("Deleted all breakpoints");
	xmlFree(cp);
	return;
    }

    num = atoi(argv[1]);
    if (num <= 0) {
	slaxDebugOutput("Invalid breakpoint number");
	return;
    }

    /*
     * Inefficient way of finding the patricia node for a breakpoint number,
     * but still ok, should be fine for debugger
     */
    TAILQ_FOREACH(dbpp, &slaxDebugBreakpoints, dbp_link) {
	if (dbpp->dbp_num == num) {
	    TAILQ_REMOVE(&slaxDebugBreakpoints, dbpp, dbp_link);
	    slaxDebugOutput("Deleted breakpoint '%d'", num);
	    return;
	}
    }

    slaxDebugOutput("Breakpoint '%d' not found", num);
}

/*
 * 'help' command
 */
static void
slaxDebugCmdHelp (DC_ARGS)
{
    slaxDebugCommand_t *cmdp = slaxDebugCommandTable;
    int i;

    slaxDebugOutput("Supported commands:");
    for (i = 0; cmdp->dc_command; i++, cmdp++) {
	if (cmdp->dc_help)
	    slaxDebugOutput("  %s", cmdp->dc_help);
    }
}

/*
 * 'list' command
 */
static void
slaxDebugCmdList (DC_ARGS)
{
    xsltStylesheetPtr style;
    xmlNodePtr inst = statep->ds_inst;
    int line_no;
    char *script = ALLOCADUPX(argv[1]);
    char *cp, *endp;

    if (script == NULL) {
	style = statep->ds_stylesheet;
	line_no = xmlGetLineNo(inst);

    } else {
	cp = strchr(script, ':');
	if (cp == NULL) {
	    cp = script;
	    style = statep->ds_stylesheet;
	    line_no = strtol(cp, &endp, 0);
	    if (line_no <= 0 || cp == endp) {
	    invalid_target:
		slaxDebugOutput("invalid target: %s", script);
		return;
	    }

	} else {
	    *cp++ = '\0';
	    line_no = strtol(cp, &endp, 0);
	    if (line_no <= 0 || cp == endp)
		goto invalid_target;

	    style = slaxDebugGetFile(statep->ds_stylesheet, script);
	    if (style == NULL)
		goto invalid_target;
	}
    }

    slaxDebugPrintScriptLine(statep, (const char *) style->doc->URL,
			     line_no, line_no + 10);
}

/*
 * Set the display mode
 */
static void
slaxDebugCmdMode (DC_ARGS)
{
    if (streq(argv[1], "emacs"))
	slaxDebugDisplayMode = EMACS_MODE;
    else if (streq(argv[1], "cli"))
	slaxDebugDisplayMode = CLI_MODE; 
}

/*
 * 'step' command
 */
static void
slaxDebugCmdStep (DC_ARGS)
{
    xsltSetDebuggerStatus(XSLT_DEBUG_STEP);
    statep->ds_flags |= DSF_DISPLAY;
}

/*
 * 'next' command
 */
static void
slaxDebugCmdNext (DC_ARGS)
{
    xsltSetDebuggerStatus(XSLT_DEBUG_NEXT);
    statep->ds_flags |= DSF_NEXT | DSF_DISPLAY;
}

/**
 * 
 * The caller must free the results with xmlXPathFreeObject(res);
 */
static xmlXPathObjectPtr
slaxDebugEvalXpath (slaxDebugState_t *statep, const char *expr)
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
    xmlNodePtr node = statep->ds_node;
    xmlNodePtr inst = statep->ds_inst;
    xsltTransformContextPtr ctxt;
    xmlXPathContextPtr xpctxt;

    ctxt = statep->ds_ctxt;
    if (ctxt == NULL)
	return NULL;

    xpctxt = ctxt->xpathCtxt;
    if (xpctxt == NULL)
	return NULL;

    comp = xsltXPathCompile(statep->ds_stylesheet, (const xmlChar *) expr);
    if (comp == NULL)
	return NULL;

    nsList = xmlGetNsList(node->doc, inst);
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

    xmlXPathFreeCompExpr(comp);
    xmlFree(nsList);

    return res;
}

/*
 * 'print' command
 * @bugs need to be more like "eval" functionality (e.g. "print $x/name")
 */
static void
slaxDebugCmdPrint (DC_ARGS)
{
    xmlXPathObjectPtr res;
    char *cp = ALLOCADUP(commandline);

    /* Move over command name */
    while (*cp && !isspace(*cp))
	cp += 1;
    while (*cp && isspace(*cp))
	cp += 1;

    res = slaxDebugEvalXpath (statep, cp);
    if (res) {
	slaxDebugPrintXpath(res);
	xmlXPathFreeObject(res);
    }
}

static char *
slaxDebugTemplateInfo (xsltTemplatePtr template, char *buf, int bufsiz)
{
    char *cp = buf, *ep = buf + bufsiz;

    if (template == NULL) {
	strlcpy(buf, "[global]", bufsiz);
	return buf;
    }

    if (template->name)
	SNPRINTF(cp, ep, "template %s ", template->name);

    if (template->match)
	SNPRINTF(cp, ep, "match %s ", template->match);

    return buf;
}

/*
 * 'where' command
 */
static void
slaxDebugCmdWhere (DC_ARGS)
{
    slaxDebugStackFrame_t *dsfp;
    const char *name;
    const char *filename;
    const char *tag;
    int num = 0;

    /*
     * Walk the stack linked list in reverse order and print it
     */
    TAILQ_FOREACH(dsfp, &slaxDebugStack, dsf_link) {
	name = NULL;
	tag = "";

	if (dsfp->dsf_template) {
	    if (dsfp->dsf_template->match) {
		name = (const char *) dsfp->dsf_template->match;
	    } else if (dsfp->dsf_template->name) {
		name = (const char *) dsfp->dsf_template->name;
		tag = "()";
	    }
	}

	filename = NULL;
	if (dsfp->dsf_inst) {
	    filename = strrchr((const char *) dsfp->dsf_inst->doc->URL, '/');
	    filename = filename ? filename + 1
		: (const char *) dsfp->dsf_inst->doc->URL;
	}

	if (name) {
	    if (filename) {
		slaxDebugOutput("#%d %s%s from %s:%d", num, name, tag,
				filename, xmlGetLineNo(dsfp->dsf_inst));
	    } else {
		slaxDebugOutput("#%d %s%s", num, name, tag);
	    }
	}

	num += 1;
    }
}
    
/*
 * 'quit' command
 */
static void
slaxDebugCmdQuit (DC_ARGS)
{
    slaxDebugClearBreakpoints();
    slaxDebugClearStacktrace();
    xsltSetDebuggerStatus(XSLT_DEBUG_QUIT);
    statep->ds_ctxt->debugStatus = XSLT_DEBUG_QUIT;
}

/* ---------------------------------------------------------------------- */

static slaxDebugCommand_t slaxDebugCommandTable[] = {
    { "break",	       slaxDebugCmdBreak,
      "break [where]   Add a breakpoint at [file:]line or template" },

    { "bt",	       slaxDebugCmdWhere, NULL }, /* Hidden */

    { "continue",      slaxDebugCmdContinue,
      "continue        Continue running the script" },

    { "delete",	       slaxDebugCmdDelete,
      "delete [num]    Delete all (or one) breakpoints" },
 
    { "help",	       slaxDebugCmdHelp,
      "help            Print this help message" },

    { "?",	       slaxDebugCmdHelp, NULL }, /* Hidden */

    { "list",	       slaxDebugCmdList,
      "list [num]      List contents of the current stylesheet" },

    { "mode",	       slaxDebugCmdMode, NULL }, /* Hidden */

    { "next",	       slaxDebugCmdNext,
      "next            Execute the next instruction, stepping over calls" },

    { "print",	       slaxDebugCmdPrint,
      "print <expr>    Print the value of an XPath expression" },

    { "step",	       slaxDebugCmdStep,
      "step            Execute the next instruction, stepping into calls" },

    { "where",	       slaxDebugCmdWhere,
      "where           Print the backtrace of template calls" },

    { "quit",	       slaxDebugCmdQuit,
      "quit            Quit debugger" },
};

/**
 * Find a command with the given name
 * @name name of command
 * @return command structure for that command
 */
static slaxDebugCommand_t *
slaxDebugGetCommand (const char *name)
{
    slaxDebugCommand_t *cmdp = slaxDebugCommandTable;
    int len = strlen(name);
    unsigned i;

    for (i = 0; i < NUM_ARRAY(slaxDebugCommandTable); i++, cmdp++) {
	if (strncmp(name, cmdp->dc_command, len) == 0)
	    return cmdp;
    }

    return NULL;
}

/**
 * Run the command entered by user
 * @input the command to be run
 */
static void
slaxDebugRunCommand (slaxDebugState_t *statep, char *input)
{
    const char *argv[MAXARGS];
    slaxDebugCommand_t *cmdp = NULL;
    char *input_copy = ALLOCADUP(input);

    slaxDebugSplitArgs(input, argv, MAXARGS);
    if (argv[0] == NULL)
	return;

    cmdp = slaxDebugGetCommand(argv[0]);
    if (cmdp) {
	cmdp->dc_func(statep, input_copy, argv);

    } else {
	slaxDebugOutput("Unknown command '%s'", argv[0]);
    }
}

/**
 * Show the debugger prompt and wait for input
 */
static int 
slaxDebugShell (slaxDebugState_t *statep)
{
    static char prev_input[BUFSIZ];
    char *cp, *input;
    static char prompt[] = "(sdb) ";

    if (statep->ds_flags & DSF_DISPLAY) {
	const char *filename = (const char *) statep->ds_stylesheet->doc->URL;
	int line_no = xmlGetLineNo(statep->ds_inst);
	slaxDebugPrintScriptLine(statep, filename, line_no, line_no + 1);
	statep->ds_flags &= ~DSF_DISPLAY;
    }

    input = slaxDebugInput(prompt);
    if (input == NULL)
	return -1;

    statep->ds_count += 1;

    /* Trim newlines */
    cp = input + strlen(input);
    if (cp > input && cp[-1] == '\n')
	*cp = '\0';

    cp = input;
    while (isspace(*cp))
	cp++;

    if (*cp == '\0') {
	cp = prev_input;
    } else {
	strlcpy(prev_input, cp, sizeof(prev_input));
    }

    slaxDebugRunCommand(statep, cp);

    if (input)
	xmlFree(input);

    return 0;
}

/**
 * Are we at the same spot as the last time we stopped?  This is
 * tricky question because (a) a single SLAX statement can turn into
 * multiple XSLT elements, (b) hitting the same breakpoint doesn't
 * mean you are on the same instruction, and (c) for-each loops can
 * have only one instruction.  We do our best to avoid these pits.
 *
 * @statep the slax debugger state handle
 * @inst the current instruction
 * @returns TRUE if the current instruction is that same as the last one
 */
static int
slaxDebugSameSlax (slaxDebugState_t *statep, xmlNodePtr inst)
{
    if (statep->ds_last_inst == NULL || inst == NULL)
	return FALSE;

    if (statep->ds_inst == inst)
	return TRUE;

    if (statep->ds_inst->doc == inst->doc) {
	int lineno = xmlGetLineNo(inst);
	if (lineno > 0 && lineno == xmlGetLineNo(statep->ds_inst))
	    return TRUE;
    }

    return FALSE;
}

/**
 * Called as callback function from libxslt before each statement is executed.
 * Here is where we handle all our debugger logic.
 *
 * @inst instruction being executed (never null)
 * @node context node (if null, we're just starting to evaluate a
 *       global variable) (and called from xsltEvalGlobalVariable)
 * @template  template being executed (if null, we're called while
 *        evaluating a global variable)
 * @ctxt transformation context
 */
static void 
slaxDebugHandler (xmlNodePtr inst, xmlNodePtr node,
		  xsltTemplatePtr template, xsltTransformContextPtr ctxt)
{
    slaxDebugState_t *statep = slaxDebugGetState();
    int status;
    char buf[BUFSIZ];

    slaxTrace("handleFrame: template %p/[%s], node %p/%s/%d, inst %p/%s/%d ctxt %x",
	      template, slaxDebugTemplateInfo(template, buf, sizeof(buf)),
	      node, NAME(node), node ? node->type : 0,
	      inst, NAME(inst), inst ? xmlGetLineNo(inst) : 0, ctxt);

    /*
     * We do not debug text node
     */
    if (inst && inst->type == XML_TEXT_NODE)
	return;

    /*
     * If we are on the same _line_ as we were at the previous
     * invocation of slaxDebugShell(), then we want to continue
     * on.  This is required since a single SLAX statement can
     * turn into multiple XSLT elements.  This "same as"
     * condition is very tender.  See also the code in addFrame.
     */
    if (slaxDebugSameSlax(statep, inst))
	return;

    /*
     * Fill in the current state
     */
    statep->ds_inst = inst;
    statep->ds_node = node;
    statep->ds_template = template;
    statep->ds_ctxt = ctxt;

    slaxDebugCheckBreakpoint(statep, inst, TRUE);

    if (statep->ds_flags & DSF_NEXT) {
	/*
	 * If we got here with the next flag still on, then there was
	 * no addFrame/dropFrame, so we must have "next"ed a non-call.
	 * Turn off the flag and get out of next mode.
	 */
	statep->ds_flags &= ~DSF_NEXT;
	xsltSetDebuggerStatus(XSLT_DEBUG_INIT);
    }

    for (;;) {
	status = xsltGetDebuggerStatus();

	switch (status) {
	    case XSLT_DEBUG_INIT:
		/*
		 * If slaxDebugShell() fails, we are looking at EOF,
		 * so shut it down and get out of here.
		 */
		if (slaxDebugShell(statep) < 0) {
		    xsltSetDebuggerStatus(XSLT_DEBUG_QUIT);
		    return;
		}

		/* Record the last instruction we looked at */
		statep->ds_last_inst = statep->ds_inst;
		break;

	    case XSLT_DEBUG_NEXT:
	    case XSLT_DEBUG_CONT:
	    case XSLT_DEBUG_NONE:
		return;

	    case XSLT_DEBUG_STEP:
		xsltSetDebuggerStatus(XSLT_DEBUG_INIT);
		return;

	    case XSLT_DEBUG_QUIT:
		return;
	}
    }
}

/**
 * Called from libxslt as callback function when template is executed.
 *
 * @template  template being executed (if null, we're called while
 *        evaluating a global variable)
 * @inst instruction node
 */
static int 
slaxDebugAddFrame (xsltTemplatePtr template, xmlNodePtr inst)
{
    slaxDebugState_t *statep = slaxDebugGetState();
    slaxDebugStackFrame_t *dsfp;
    char buf[BUFSIZ];

    slaxTrace("addFrame: template %p/[%s], inst %p/%s/%d (inst %p/%s)",
	      template, slaxDebugTemplateInfo(template, buf, sizeof(buf)),
	      inst, NAME(inst), inst ? xmlGetLineNo(inst) : 0,
	      statep->ds_inst, NAME(statep->ds_inst));

    /*
     * Store the template backtrace in linked list
     */
    dsfp = xmlMalloc(sizeof(*dsfp));
    if (!dsfp) {
	slaxDebugOutput("memory allocation failure");
	return 0;
    }

    bzero(dsfp, sizeof(*dsfp));
    dsfp->dsf_template = template;
    dsfp->dsf_inst = inst;

    if (inst->ns && inst->ns->href && inst->name
	&& streq((const char *) inst->ns->href, XSL_NS)) {
	if (streq((const char *) inst->name, ELT_WITH_PARAM))
	    dsfp->dsf_flags |= DSFF_PARAM;
    }

    if (!(dsfp->dsf_flags & DSFF_PARAM)) {
	/* If we're 'next'ing, mark this frame as "stop when pop" */
	if (statep->ds_flags & DSF_NEXT) {
	    statep->ds_flags &= ~DSF_NEXT;
	    dsfp->dsf_flags |= DSFF_STOPWHENPOP;
	    xsltSetDebuggerStatus(XSLT_DEBUG_CONT);
	}
    }

    TAILQ_INSERT_TAIL(&slaxDebugStack, dsfp, dsf_link);

    /*
     * return value > 0 makes libxslt to call slaxDebugDropFrame()
     */
    return 1;
}

/*
 * Called from libxslt when the template execution is over
 */
static void 
slaxDebugDropFrame (void)
{
    slaxDebugState_t *statep = slaxDebugGetState();
    slaxDebugStackFrame_t *dsfp;
    xsltTemplatePtr template;
    char buf[BUFSIZ];
    xmlNodePtr inst;

    dsfp = TAILQ_LAST(&slaxDebugStack, slaxDebugStack_s);
    if (dsfp == NULL) {
	slaxTrace("dropFrame: null");
	return;
    }

    template = dsfp->dsf_template;
    inst = dsfp->dsf_inst;

    slaxTrace("dropFrame: %s (%p), inst <%s%s%s> (%p; line %d%s)",
	      slaxDebugTemplateInfo(template, buf, sizeof(buf)),
	      template, inst,
	      (inst && inst->ns && inst->ns->prefix) ? inst->ns->prefix : null,
	      (inst && inst->ns && inst->ns->prefix) ? ":" : "",
	      NAME(dsfp->dsf_inst),
	      dsfp->dsf_inst ? xmlGetLineNo(dsfp->dsf_inst) : 0,
	      (dsfp->dsf_flags & DSFF_STOPWHENPOP) ? " stopwhenpop" : "");

    if (dsfp->dsf_flags & DSFF_STOPWHENPOP)
	xsltSetDebuggerStatus(XSLT_DEBUG_INIT);

    /*
     * If we're popping stack frames, then we're not on the same
     * instruction.  Clear the last instruction pointer.
     */
    statep->ds_last_inst = NULL;

    TAILQ_REMOVE(&slaxDebugStack, dsfp, dsf_link);
    xmlFree(dsfp);
}

/*
 * Register debugger
 */
int
slaxDebugRegister (slaxDebugInputCallback_t input_callback,
		   slaxDebugOutputCallback_t output_callback,
		   xmlOutputWriteCallback raw_write)
{
    static int done_register;
    slaxDebugState_t *statep = slaxDebugGetState();

    if (done_register)
	return FALSE;
    done_register = TRUE;

    TAILQ_INIT(&slaxDebugBreakpoints);
    TAILQ_INIT(&slaxDebugStack);

    /* Start with the current line */
    statep->ds_flags |= DSF_DISPLAY;

    xsltSetDebuggerStatus(XSLT_DEBUG_INIT);
    xsltSetDebuggerCallbacksHelper(slaxDebugHandler, slaxDebugAddFrame,
				   slaxDebugDropFrame);

    slaxDebugDisplayMode = CLI_MODE;

    slaxDebugInputCallback = input_callback;
    slaxDebugOutputCallback = output_callback;
    slaxDebugIOWrite = raw_write;

    slaxDebugOutput("sdb: The SLAX Debugger (version %s)", PACKAGE_VERSION);
    slaxDebugOutput("Type 'help' for help");

    return FALSE;
}

/*
 * Set the top most stylesheet
 */
void
slaxDebugSetStylesheet (xsltStylesheetPtr stylep)
{
    slaxDebugState_t *statep = slaxDebugGetState();

    if (statep)
	statep->ds_stylesheet = stylep;
}

void
slaxDebugSetIncludes (const char **includes)
{
    slaxDebugIncludes = includes;
}
