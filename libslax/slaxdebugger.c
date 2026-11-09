/*
 * Copyright (c) 2010-2025, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
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
 * instruction.  The instruction should never be NULL.  Sadly, addFrame
 * doesn't get the context pointer, so we have to grab it the next time
 * the handler is called (see DSF_FRESHADD).
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
 * There is a fourth callback (errorHandler), added for SLAX, which is
 * called when a fatal runtime error (e.g. a variable whose expression
 * fails to evaluate) is about to abort the transform.  Unlike Handler,
 * it is called unconditionally, regardless of the current debugger
 * status or any breakpoint, so the user gets a chance to inspect the
 * still-live context (e.g. "print" or "info locals") before the
 * transform unwinds and that context is torn down.
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
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/queue.h>

#include <libxml/xmlsave.h>
#include <libxml/xmlIO.h>
#include <libxml/hash.h>
#include <libxslt/variables.h>
#include <libxslt/transform.h>

#include "slaxinternals.h"
#include "slaxparser.h"
#include <libslax/slax.h>
#include <libpsu/psustring.h>
#include <libpsu/psutime.h>

#define MAXARGS	256
#define DEBUG_LIST_COUNT	12 /* Number of lines "list" shows */

/* Add some values to the xsltDebugStatusCodes enum */
#define XSLT_DEBUG_LOCAL (XSLT_DEBUG_QUIT + 1)
#define XSLT_DEBUG_OVER (XSLT_DEBUG_LOCAL + 1) /* The "over" operation */
#define XSLT_DEBUG_DONE (XSLT_DEBUG_LOCAL + 2) /* The script is done */

#ifdef WITH_SLAX_DEBUGGER

/*
 * Information about the current point of debugging
 */
typedef struct slaxDebugState_s {
    xsltStylesheetPtr ds_script; /* Current top-level script/stylesheet */
    xmlNodePtr ds_inst;	/* Current libxslt node being executed */
    xmlNodePtr ds_node;	/* Current context node */
    xsltTemplatePtr ds_template; /* Current template being executed */
    xsltTransformContextPtr ds_ctxt; /* Transformation context */
    xmlNodePtr ds_last_inst;	/* Last libxslt node being executed */
    xmlNodePtr ds_stop_at;	/* Stopping point (from "cont xxx") */
    int ds_count;		/* Command count */
    int ds_flags;		/* Global state flags */
    int ds_stackdepth;		/* Current depth of call stack */
    int ds_frame_selected;	/* "up"/"down" selected frame, as an offset
				   from the innermost (0 == live/current) */
    xmlNodePtr ds_list_node;	/* Last "list" target */
    int ds_list_line;		/* Last "list" line number */
    char *ds_script_buffer;	/* In-memory copy of the script text */
    char *ds_callgraph_buffer;	/* Captured xsltSaveProfiling() output */
    xmlHashTablePtr ds_flamegraph_hash; /* Per-call-path self time (usecs) */
    char *ds_profile_file;	/* Target file for callgraph/flamegraph report */
} slaxDebugState_t;

/* Flags for ds_flags */
#define DSF_OVER 	(1<<0)	/* Step over the current instruction */
#define DSF_DISPLAY	(1<<1)	/* Show instruction before next command */
#define DSF_CALLFLOW	(1<<2)	/* Report call flow */
#define DSF_INSHELL	(1<<3)	/* Inside the shell, so don't recurse */

#define DSF_RESTART	(1<<4)	/* Restart the debugger/script */
#define DSF_PROFILER	(1<<5)	/* Profiler is on */
#define DSF_FRESHADD	(1<<6)	/* Just did an "addFrame" */
#define DSF_CONTINUE	(1<<7)	/* Continue (or run) when restarted */

#define DSF_RELOAD	(1<<8)	/* Reload the script */
#define DSF_PROFILE_CALLGRAPH (1<<9) /* Callgraph profiling armed */
#define DSF_PROFILE_FLAMEGRAPH (1<<10) /* Flame-graph profiling armed */
#define DSF_ERRORSTOP	(1<<11)	/* Already stopped for the current
				   fatal runtime error; see
				   slaxDebugErrorHandler() */
#define DSF_INEVAL	(1<<12)	/* Inside slaxDebugEvalXpath(), evaluating
				   an expression on the debugger's own
				   behalf (print/set/breakpoint condition);
				   a failure here is not a script error */

slaxDebugState_t slaxDebugState;
const char **slaxDebugIncludes;

/* Flags from slaxDebugInitFlags() */
slaxDebugFlags_t slaxDebugFlags;

/*
 * Echo a line of input just read via slaxInput(), if SDBF_ECHO_INPUT
 * is set.  slaxInput() already writes the prompt (sans newline), so
 * we only need to echo the input text to complete that line, making
 * piped/non-tty transcripts (e.g. test output) show both halves of
 * the exchange.
 */
static void
slaxDebugEchoInput (const char *input)
{
    if ((slaxDebugFlags & SDBF_ECHO_INPUT) && input != NULL)
	slaxOutput("%s", input);
}

/* Arguments to our command functions */
#define DC_ARGS \
        slaxDebugState_t *statep UNUSED, \
	const char *commandline UNUSED, \
	const char **argv UNUSED
#define DH_ARGS \
        slaxDebugState_t *statep UNUSED

/*
 * Commands supported in debugger
 */
typedef void (*slaxDebugCommandFunc_t)(DC_ARGS);
typedef void (*slaxDebugHelpFunc_t)(DH_ARGS);

typedef struct slaxDebugCommand_s {
    const char *dc_command;	/* Command name */
    int dc_min;			/* Minimum length */
    slaxDebugCommandFunc_t dc_func; /* Function pointer */
    const char *dc_help;	/* Help text */
    slaxDebugHelpFunc_t dc_helpfunc; /* Function to generate more help */
} slaxDebugCommand_t;

static slaxDebugCommand_t slaxDebugCmdTable[];

static slaxDebugCommand_t *
slaxDebugGetCommand (const char *name);

/*
 * Doubly linked list to store the templates call sequence
 */
typedef struct slaxDebugStackFrame_s {
    TAILQ_ENTRY(slaxDebugStackFrame_s) st_link;
    unsigned st_depth;           /* Stack depth */
    xsltTemplatePtr st_template; /* Template (parent) */
    xmlNodePtr st_inst;	  /* Instruction of the template (code) */
    xmlNodePtr st_caller;        /* Instruction of the caller */
    xsltTransformContextPtr st_ctxt; /* Transform context pointer */
    int st_locals_start;     /* Our first entry in ctxt->varsTab[] */
    int st_locals_stop;      /* Our last entry in ctxt->varsTab[] */
    unsigned st_flags; /* STF_* flags for this stack frame */
    psu_time_usecs_t st_enter_time; /* Entry time, for flame-graph mode */
    psu_time_usecs_t st_child_time; /* Time already charged to children */
} slaxDebugStackFrame_t;

/* Flags for st_flags */
#define STF_STOPWHENPOP	(1<<0) /* Stop when this frame is popped */
#define STF_PARAM		(1<<1) /* Frame is a with-param insn */

TAILQ_HEAD(slaxDebugStack_s, slaxDebugStackFrame_s) slaxDebugStack;

typedef struct slaxDebugRestartItem_s {
    TAILQ_ENTRY(slaxDebugRestartItem_s) sdr_link;
    slaxRestartFunc sdr_func; /* Func to call at restart time */
} slaxDebugRestartItem_t;

TAILQ_HEAD(slaxDebugRestartList_s, slaxDebugRestartItem_s) slaxDebugRestartList;

/*
 * Double linked list to hold the breakpoints
 */
typedef struct slaxDebugBreakpoint_s {
    TAILQ_ENTRY(slaxDebugBreakpoint_s) dbp_link;
    char *dbp_where;		/* Text name as given by user */
    xmlNodePtr dbp_inst;	/* Node we are breaking on */
    uint dbp_num;		/* Breakpoint number */
    char *dbp_condition;	/* Conditional breakpoint expression (XPath) */
    char *dbp_condition_user;	/* Same, in user's original syntax (slax?) */
    uint32_t dbp_flags;		/* DBPF_* flags for this breakpoint */
} slaxDebugBreakpoint_t;

/* Flags for dbp_flags */
#define DBPF_COND_WARNED (1<<0) /* Already warned that condition fails to eval */
#define DBPF_TEMPORARY	 (1<<1) /* Delete automatically after it's hit */

TAILQ_HEAD(slaxDebugBpList_s, slaxDebugBreakpoint_s) slaxDebugBreakpoints;

static uint slaxDebugBreakpointNumber;

/*
 * Double linked list to hold the watchpoints.  A watchpoint fires
 * whenever the watched variable is assigned a new value, whether by
 * normal script execution (slaxMvarElement()) or by an interactive
 * "set"/"append" (slaxDebugCmdSetCommon()); see
 * slaxDebugCheckWatchpoints().  Watchpoint numbers are independent of
 * breakpoint numbers, so adding or removing one never shifts the
 * other's numbering.
 */
typedef struct slaxDebugWatch_s {
    TAILQ_ENTRY(slaxDebugWatch_s) dwp_link;
    char *dwp_where;		/* Text as given by the user ("$foo") */
    char *dwp_name;		/* Bare variable name ("foo"), for matching */
    char *dwp_condition;	/* "$foo" translated to XPath */
    uint dwp_num;		/* Watchpoint number */
    int dwp_have_value;	/* TRUE once dwp_value holds a real snapshot */
    char *dwp_value;		/* Last known value, stringified, for display */
    xmlDocPtr dwp_nodes;	/* Deep copy of the last node-set value's
				   top-level nodes, for structural
				   comparison; NULL for a scalar value */
} slaxDebugWatch_t;

TAILQ_HEAD(slaxDebugWpList_s, slaxDebugWatch_s) slaxDebugWatches;

static uint slaxDebugWatchNumber;

/*
 * Various display mode
 */
int slaxDebugDisplayMode;
#define DEBUG_MODE_CLI		1 /* Normal CLI operations */
#define DEBUG_MODE_EMACS	2 /* gdb/emacs mode */
#define DEBUG_MODE_PROFILER	3 /* Profiler only */

#define NAME(_x) (((_x) && xmlNodeGetName(_x)) ? xmlNodeGetName(_x) : slaxNull)

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

void
slaxRestartListAdd (slaxRestartFunc func)
{
    slaxDebugRestartItem_t *itemp;

    itemp = malloc(sizeof(*itemp));
    if (itemp) {
	bzero(itemp, sizeof(*itemp));
	itemp->sdr_func = func;
	TAILQ_INSERT_TAIL(&slaxDebugRestartList, itemp, sdr_link);
    }
}

void
slaxRestartListCall (void)
{
    slaxDebugRestartItem_t *itemp;

    TAILQ_FOREACH(itemp, &slaxDebugRestartList, sdr_link) {
	itemp->sdr_func();
    }
}

static xsltStylesheetPtr
slaxDebugGetFile (xsltStylesheetPtr style, const char *filename)
{
    char buf[BUFSIZ];
    char *slash;
    const char **inc;

    for ( ; style; style = xsltStylesheetGetNext(style)) {
	if (streq((const char *) xmlDocGetURL(xsltStylesheetGetDoc(style)),
		  filename))
	    return style;

	for (inc = slaxDebugIncludes; inc && *inc; inc++) {
	    snprintf(buf, sizeof(buf), "%s%s", *inc, filename);
	    if (streq((const char *) xmlDocGetURL(xsltStylesheetGetDoc(style)),
		      buf))
		return style;
	}

	/*
	 * Just the filname match
	 */
	slash = strrchr((const char *)
			 xmlDocGetURL(xsltStylesheetGetDoc(style)), '/');
	if (slash && streq(slash + 1, filename))
	    return style;

	if (xsltStylesheetGetImports(style)) {
	    xsltStylesheetPtr answer;
	    answer = slaxDebugGetFile(xsltStylesheetGetImports(style), filename);
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

    for (tmp = xsltStylesheetGetTemplates(statep->ds_script); tmp;
	 tmp = xsltTemplateGetNext(tmp)) {
	if ((xsltTemplateGetMatch(tmp)
	     && streq((const char *) xsltTemplateGetMatch(tmp), name))
	    || (xsltTemplateGetName(tmp)
		&& streq((const char *) xsltTemplateGetName(tmp), name)))
	    return xsltTemplateGetElem(tmp);
    }

    return NULL;
}

static xsltTemplatePtr
slaxDebugGetTemplate (slaxDebugState_t *statep, xmlNodePtr inst)
{
    xsltTemplatePtr tmp;

    for ( ; inst; inst = xmlNodeGetParent(inst)) {
	if (slaxNodeIsXsl(inst, ELT_TEMPLATE)) {

	    for (tmp = xsltStylesheetGetTemplates(statep->ds_script); tmp;
		 tmp = xsltTemplateGetNext(tmp)) {
		if (xsltTemplateGetElem(tmp) == inst)
		    return tmp;
	    }

	    return NULL;
	}
    }

    return NULL;
}

static xmlNodePtr
slaxDebugGetNodeByLine (xmlNodePtr node, int lineno)
{
    for ( ; node; node = xmlNodeGetNext(node)) {
	if (lineno == xmlGetLineNo(node))
	    return node;

	if (xmlNodeGetChildren(node)) {
	    xmlNodePtr answer;
	    answer = slaxDebugGetNodeByLine(xmlNodeGetChildren(node), lineno);
	    if (answer)
		return answer;
	}
    }

    return NULL;
}

static void
slaxDebugListNodeByLine (xmlNodePtr node)
{
    slaxDumpTree(node, "", 0);
}

/*
 * Return xmlnode for the given line number
 */
static xmlNodePtr
slaxDebugGetNodeByFilename (slaxDebugState_t *statep,
		  const char *filename, int lineno)
{
    xsltStylesheetPtr style;
    xmlNodePtr node;

    style = slaxDebugGetFile(statep->ds_script, filename);
    if (style == NULL)
	return NULL;

    node = slaxDebugGetNodeByLine(xmlDocGetChildren(xsltStylesheetGetDoc(style)), lineno);

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

    style = slaxDebugGetFile(statep->ds_script, script);
    if (!style)
	return NULL;

    /*
     * Get the node for the given linenumber from the stylesheet
     */
    node = slaxDebugGetNodeByLine((xmlNodePtr) xsltStylesheetGetDoc(style),
				  lineno);

    return node;
}

/*
 * Print the given XPath object
 */
static void
slaxDebugOutputXpath (xmlXPathObjectPtr xpath, const char *tag, int full)
{
    if (xpath == NULL)
	return;

    if (tag == NULL)
	tag = "";

    switch (xmlXPathObjectGetType(xpath)) {
    case XPATH_BOOLEAN:
	slaxOutput("%s[boolean] %s", tag,
		   xmlXPathObjectGetBoolval(xpath) ? "true" : "false");
	break;

    case XPATH_NUMBER:
	slaxOutput("%s[number] %lf", tag, xmlXPathObjectGetFloatval(xpath));
	break;

    case XPATH_STRING:
	if (xmlXPathObjectGetStringval(xpath))
	    slaxOutput("%s[string] \"%s\"", tag,
		       xmlXPathObjectGetStringval(xpath));
	break;

    case XPATH_NODESET:
	if (full) {
	    xmlNodeSetPtr ns = xmlXPathObjectGetNodesetval(xpath);
	    const char *frag = "";

	    if (ns && xmlNodeSetGetNodeNr(ns) == 1
		    && xsltIsResultTreeFragment(xmlNodeSetGetNodeEntry(ns, 0)))
		frag = " rtf-doc";

	    slaxOutput("%s[node-set]%s (%d)%s", tag,
		       ns ? "" : " [null]",
		       ns ? xmlNodeSetGetNodeNr(ns) : 0,
		       frag);

	    if (ns)
		slaxOutputNodeset(ns);
	} else {
	    xmlNodeSetPtr ns = xmlXPathObjectGetNodesetval(xpath);
	    if (ns && xmlNodeSetGetNodeNr(ns) == 0)
		ns = NULL;

	    slaxOutput("%s[node-set]%s (%d)%s%s%s", tag,
		       xmlXPathObjectGetNodesetval(xpath) ? "" : " [null]",
		       ns ? xmlNodeSetGetNodeNr(ns) : 0,
		       ns ? " <" : "",
		       ns ? xmlNodeGetName(xmlNodeSetGetNodeEntry(ns, 0)) : slaxNull,
		       ns ? "> ...." : "");
	}
	break;

    case XPATH_XSLT_TREE: {
	xmlNodeSetPtr ns = xmlXPathObjectGetNodesetval(xpath);

	slaxOutput("%s[rtf]%s (%d)", tag,
			ns ? "" : " [null]",
			ns ? xmlNodeSetGetNodeNr(ns) : 0);
	if (ns)
	    slaxOutputNodeset(ns);
	break;
    }

    default:
	break;
    }
}
 
/*
 * Remove a breakpoint from the list and free its contents
 */
static void
slaxDebugFreeBreakpoint (slaxDebugBreakpoint_t *dbp)
{
    xmlFreeAndEasy(dbp->dbp_where);
    xmlFreeAndEasy(dbp->dbp_condition);
    xmlFreeAndEasy(dbp->dbp_condition_user);
    TAILQ_REMOVE(&slaxDebugBreakpoints, dbp, dbp_link);
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
	slaxDebugFreeBreakpoint(dbp);
    }

    slaxDebugBreakpointNumber = 0;
}

/*
 * Remove a watchpoint from the list and free its contents
 */
static void
slaxDebugFreeWatch (slaxDebugWatch_t *dwp)
{
    xmlFreeAndEasy(dwp->dwp_where);
    xmlFreeAndEasy(dwp->dwp_name);
    xmlFreeAndEasy(dwp->dwp_condition);
    xmlFreeAndEasy(dwp->dwp_value);
    if (dwp->dwp_nodes != NULL)
	xmlFreeDoc(dwp->dwp_nodes);
    TAILQ_REMOVE(&slaxDebugWatches, dwp, dwp_link);
}

/*
 * Clear all watchpoints
 */
static void
slaxDebugClearWatches (void)
{
    slaxDebugWatch_t *dwp;

    for (;;) {
	dwp = TAILQ_FIRST(&slaxDebugWatches);
	if (dwp == NULL)
	    break;
	slaxDebugFreeWatch(dwp);
    }

    slaxDebugWatchNumber = 0;
}

/*
 * Clear stacktrace, basically delete the linked list
 */
static void
slaxDebugClearStacktrace (void)
{
    slaxDebugStackFrame_t *stp;

    for (;;) {
	stp = TAILQ_FIRST(&slaxDebugStack);
	if (stp == NULL)
	    break;
	TAILQ_REMOVE(&slaxDebugStack, stp, st_link);
    }
}

static void
slaxDebugMakeRelativePath (const char *src_f, const char *dest_f,
			   char *relative_path, int size)
{
    int i, j, slash, n;
    const char *f;

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

static void
slaxDebugOutputExpanded (const char *filename, int line,
			 const char *rawp, int len)
{
    char buf[8 * len + 1];
    char *cp = buf;
    int i;

    for (i = 0; i < len; i++, rawp++) {
	if (*rawp == '\r')
	    continue;

	if (*rawp == '\t') {
	    do {
		*cp++ = ' ';
	    } while ((cp - buf) % 8 != 0);

	} else {
	    /* Normal character */
	    *cp++ = *rawp;
	}
    }
    *cp = '\0';

    slaxOutput("%s%s%d: %s", filename ?: "", filename ? ": " : "",
	       line, buf);
}

/*
 * Return the line for given linenumber from file.
 *
 * Very inefficient way, we read the file from begining till we reach the
 * given linenumber, should be fine for now. May be we will optimize when we
 * implement 'list' command.
 */
static int
slaxDebugOutputScriptLines (slaxDebugState_t *statep, const char *filename,
			    int start, int stop)
{
    FILE *fp;
    int count = 0;
    const char *cp;
    char line[BUFSIZ];
    int len;

    if (filename == NULL || start == 0 || stop == 0)
	return TRUE;

    if (statep->ds_script_buffer) {
	char *xp, *last;

	last = statep->ds_script_buffer;
	for (xp = strchr(last, '\n'); xp; xp = strchr(last, '\n')) {
	    if (++count >= start)
		break;
	    last = xp + 1;
	}
	if (xp == NULL)
	    return TRUE;

	/* Ensure we get at least one line of output */
	if (count > stop)
	    stop = count + 1;

	while (count < stop) {
	    len = xp ? xp - last : (int) strlen(last);
	    slaxDebugOutputExpanded(NULL, count++, last, len);

	    if (xp == NULL)
		break;

	    last = xp + 1;
	    xp = strchr(last, '\n');
	}
	return TRUE;
    }

    fp = fopen(filename, "r");
    if (fp == NULL) {
	/* 
	 * do not print error message, the line won't get displayed that is 
	 * fine
	 */
	return TRUE;
    }

    for (;;) {
	if (fgets(line, sizeof(line), fp) == NULL) {
	    break;
	}
	/*
	 * Since we are only reading line_size per iteration, if the current 
	 * line length is more than line_size then we may read same line in
	 * more than one iteration, so increment the count only when the last 
	 * character is '\n'
	 */
	if (line[strlen(line) - 1] == '\n')
	    count += 1;

	if (count >= start)
	    break;
    }

    cp = strrchr(filename, '/');
    cp = cp ? cp + 1 : filename;

    len = strlen(line);
    if (len > 0 && line[len - 1] == '\n')
	line[len - 1] = '\0';

    /* Ensure we get at least one line of output */
    if (count > stop)
	stop = count + 1;

    while (count < stop) {
	slaxDebugOutputExpanded(cp, count, line, strlen(line));

	if (fgets(line, sizeof(line), fp) == NULL)
	    break;

	len = strlen(line);
	if (len > 0 && line[len - 1] == '\n') {
	    line[len - 1] = '\0';
	    count += 1;
	}
    }

    if (slaxDebugDisplayMode == DEBUG_MODE_EMACS) {
	char rel_path[BUFSIZ];

	/*
	 * In emacs path should be relative to remote default directory,
	 * so print the relative path of the current file from main stylesheet
	 */
	cp = (const char *) xmlDocGetURL(xsltStylesheetGetDoc(statep->ds_script));
	slaxDebugMakeRelativePath(cp, filename, rel_path, sizeof(rel_path));
	
	slaxOutput("%c%c%s:%d:0", 26, 26, rel_path, start);
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
    static const char wsp[] = " \t\n\r";
    int i;
    char *s;

    for (i = 0; (s = strsep(&buf, wsp)) && i < maxargs - 1; i++) {
	args[i] = s;

	if (buf) {
	    buf += strspn(buf, wsp);
	    if (*buf == '\0')
		buf = NULL;
	}
    }

    args[i] = NULL;

    return i;
}

/*
 * Check if the breakpoint is available for curnode being executed
 */
static int
slaxDebugCheckBreakpoint (slaxDebugState_t *statep, xmlNodePtr node)
{
    slaxDebugBreakpoint_t *dbp;

    if (statep->ds_stop_at && statep->ds_stop_at == node) {
	statep->ds_stop_at = NULL; /* One time only */
	return TRUE;
    }

    TAILQ_FOREACH(dbp, &slaxDebugBreakpoints, dbp_link) {
	if (dbp->dbp_inst && dbp->dbp_inst == node)
	    return TRUE;
    }

    return FALSE;
}

/*
 * Find the breakpoint (and return it)
 */
static slaxDebugBreakpoint_t *
slaxDebugFindBreakpoint (slaxDebugState_t *statep, xmlNodePtr node)
{
    slaxDebugBreakpoint_t *dbp;

    if (statep->ds_stop_at && statep->ds_stop_at == node)
	return NULL;

    TAILQ_FOREACH(dbp, &slaxDebugBreakpoints, dbp_link) {
	if (dbp->dbp_inst && dbp->dbp_inst == node)
	    return dbp;
    }

    return NULL;
}

/*
 * Announce the breakpoint found (and return it)
 */
static void
slaxDebugAnnounceBreakpoint (slaxDebugState_t *statep, xmlNodePtr node)
{
    slaxDebugBreakpoint_t *dbp;

    if (statep->ds_stop_at && statep->ds_stop_at == node) {
	slaxOutput("Reached stop at %s:%ld",
		   xmlDocGetURL(xmlNodeGetDoc(node)), xmlGetLineNo(node));

	xsltSetDebuggerStatus(XSLT_DEBUG_INIT);
	statep->ds_stop_at = NULL; /* One time only */
	return;
    }

    TAILQ_FOREACH(dbp, &slaxDebugBreakpoints, dbp_link) {
	if (dbp->dbp_inst && dbp->dbp_inst == node) {
	    slaxOutput("Reached breakpoint %d, at %s:%ld",
		       dbp->dbp_num, xmlDocGetURL(xmlNodeGetDoc(node)),
		       xmlGetLineNo(node));

	    if (dbp->dbp_condition_user)
		slaxOutput("  Condition: '%s'", dbp->dbp_condition_user);

	    xsltSetDebuggerStatus(XSLT_DEBUG_INIT);
	    return;
	}
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

    if (xsltTemplateGetName(template))
	SNPRINTF(cp, ep, "template %s ", xsltTemplateGetName(template));

    if (xsltTemplateGetMatch(template))
	SNPRINTF(cp, ep, "match %s", xsltTemplateGetMatch(template));

    /* Trim trailing space */
    if (cp > buf && cp[-1] == ' ')
	cp[-1] = '\0';

    return buf;
}

static void
slaxDebugCallFlow (slaxDebugState_t *statep, xsltTemplatePtr template,
		   xmlNodePtr inst, const char *tag)
{
    char buf[BUFSIZ];

    slaxOutput("callflow: %u: %s <%s%s%s>%s%s at %s%s%ld",
	statep->ds_stackdepth, tag,
	(inst && xmlNodeGetNs(inst) && xmlNsGetPrefix(xmlNodeGetNs(inst)))
		? xmlNsGetPrefix(xmlNodeGetNs(inst)) : slaxNull,
	(inst && xmlNodeGetNs(inst) && xmlNsGetPrefix(xmlNodeGetNs(inst))) ? ":" : "",
	NAME(inst), template ? " in " : "",
	template ? slaxDebugTemplateInfo(template, buf, sizeof(buf)) : "",
	(inst && xmlNodeGetDoc(inst) && xmlDocGetURL(xmlNodeGetDoc(inst)))
		? xmlDocGetURL(xmlNodeGetDoc(inst)) : slaxNull,
	(inst && xmlNodeGetDoc(inst) && xmlDocGetURL(xmlNodeGetDoc(inst))) ? ":" : "",
	inst ? xmlGetLineNo(inst) : 0);
}

static xmlNodePtr
slaxDebugGetNode (slaxDebugState_t *statep, const char *spec)
{
    xmlNodePtr node;
    int lineno;

    /*
     * Break on the current line
     */
    if (spec == NULL)
	return statep->ds_inst;

    /*
     * scriptname:linenumber format
     */
    if (strchr(spec, ':')) {
	node = slaxDebugGetScriptNode(statep, spec);

	/* If it wasn't foo:34, maybe it's foo:bar */
	if (node == NULL)
	    node = slaxDebugGetTemplateNodebyName(statep, spec);
	return node;
    }

    /*
     * simply linenumber, put breakpoint in the cur script
     */
    if ((lineno = atoi(spec)) > 0) {
	xmlDocPtr docp = statep->ds_inst
	    ? xmlNodeGetDoc(statep->ds_inst) : xsltStylesheetGetDoc(statep->ds_script);
	const char *fname =  (const char *) xmlDocGetURL(docp);
	return slaxDebugGetNodeByFilename(statep, fname, lineno);
    }

    /*
     * template name?
     */
    return slaxDebugGetTemplateNodebyName(statep, spec);
}

/*
 * Reload all breakpoints
 */
static void
slaxDebugReloadBreakpoints (slaxDebugState_t *statep)
{
    slaxDebugBreakpoint_t *dbp;
    xmlNodePtr node;

    TAILQ_FOREACH(dbp, &slaxDebugBreakpoints, dbp_link) {
	dbp->dbp_inst = NULL;	/* No dangling references */

	node = slaxDebugGetNode(statep, dbp->dbp_where);
	if (node)
	    dbp->dbp_inst = node;
	else
	    slaxOutput("Breakpoint target \"%s\" was not defined",
		       dbp->dbp_where);
    }
}

static int
slaxDebugCheckAbbrev (const char *name, int min, const char *value, int len)
{
    return (len >= min && strncmp(name, value, len) == 0);
}

static inline int
slaxDebugIsAbbrev (const char *name, const char *value)
{
    return value ? slaxDebugCheckAbbrev(name, 1, value, strlen(value)) : 0;
}

static inline void
slaxDebugClearListInfo (slaxDebugState_t *statep)
{
    statep->ds_list_node = NULL;
    statep->ds_list_line = 0;
}

/*
 * Move along the commandline to find the expression.  Allow
 * the user to say "if" or "when".
 */
static char *
slaxDebugFindCondition (char *input)
{
    /* Move over command name */
    while (*input && !isspace((int) *input))
	input += 1;
    while (*input && isspace((int) *input))
	input += 1;

    /* Move over breakpoint number */
    while (*input && !isspace((int) *input))
	input += 1;
    while (*input && isspace((int) *input))
	input += 1;

    if (strncmp(input, "if", 2) == 0
	    && (input[2] == '\0' || isspace((int) input[2]))) {
	input += 2;
	while (*input && isspace((int) *input))
	    input += 1;
    } else if (strncmp(input, "when", 4) == 0
	    && (input[4] == '\0' || isspace((int) input[4]))) {
	input += 4;
	while (*input && isspace((int) *input))
	    input += 1;
    }

    return input;
}

/*
 * Shared logic behind the 'break' and 'tbreak' commands: resolve the
 * location, parse and validate an optional condition, and add the
 * resulting breakpoint to the list.  "temporary" marks it to be
 * auto-deleted (by slaxDebugHandler()) the first time it's hit.
 */
static void
slaxDebugAddBreakpoint (DC_ARGS, int temporary)
{
    xmlNodePtr node = NULL;
    slaxDebugBreakpoint_t *bp;
    char *condition = NULL;
    char *condition_user = NULL;

    node = slaxDebugGetNode(statep, argv[1]);
    if (node == NULL) {
	slaxOutput("Target \"%s\" is not defined", argv[1]);
	return;
    }

    if (slaxDebugCheckBreakpoint(statep, node)) {
	slaxOutput("Duplicate breakpoint");
	return;
    }

    /* If we have a breakpoint, make sure the expression parses */
    if (argv[2]) {
	condition = ALLOCADUP(commandline);
	condition = slaxDebugFindCondition(condition);
	if (*condition == '\0') {
	    slaxOutput("Missing expression");
	    return;
	}

	/* Keep the user's original SLAX spelling around for display */
	condition_user = xmlStrdup2(condition);

	condition = slaxSlaxToXpath("sdb", 1, condition, NULL);
	if (condition == NULL) {
	    slaxOutput("Invalid expression");
	    xmlFreeAndEasy(condition_user);
	    return;
	}

	xmlXPathCompExprPtr comp;
	comp = xsltXPathCompile(statep->ds_script, (const xmlChar *) condition,
				 NULL);
	if (comp == NULL) {
	    slaxOutput("Invalid expression");
	    xmlFreeAndEasy(condition);
	    xmlFreeAndEasy(condition_user);
	    return;
	}

	xmlXPathFreeCompExpr(comp);
    }

    /*
     * Create a record of the breakpoint and add it to the list
     */
    bp = xmlMalloc(sizeof(*bp));
    if (bp == NULL)
	return;

    bzero(bp, sizeof(*bp));
    bp->dbp_where = xmlStrdup2(argv[1]);
    bp->dbp_num = ++slaxDebugBreakpointNumber;
    bp->dbp_inst = node;
    bp->dbp_condition = condition;
    bp->dbp_condition_user = condition_user;
    if (temporary)
	bp->dbp_flags |= DBPF_TEMPORARY;

    TAILQ_INSERT_TAIL(&slaxDebugBreakpoints, bp, dbp_link);

    slaxOutput("%s %d at file %s, line %ld",
		    temporary ? "Temporary breakpoint" : "Breakpoint",
		    bp->dbp_num,
		    xmlDocGetURL(xmlNodeGetDoc(node)), xmlGetLineNo(node));
}

/*
 * 'break' command
 */
static void
slaxDebugCmdBreak (DC_ARGS)
{
    slaxDebugAddBreakpoint(statep, commandline, argv, FALSE);
}

/*
 * 'tbreak' command -- like 'break', but the breakpoint is automatically
 * deleted the first time it's hit (see slaxDebugHandler())
 */
static void
slaxDebugCmdTbreak (DC_ARGS)
{
    slaxDebugAddBreakpoint(statep, commandline, argv, TRUE);
}

/*
 * 'watch variable $foo' subcommand: parse and validate "$foo" the
 * same way slaxDebugAddBreakpoint() validates a breakpoint condition,
 * then add a new watchpoint for it.
 */
static void
slaxDebugCmdWatchVariable (DC_ARGS)
{
    slaxDebugWatch_t *dwp;
    const char *where = argv[2];

    if (where == NULL || where[0] != '$' || where[1] == '\0') {
	slaxOutput("usage: watch variable $name");
	return;
    }

    TAILQ_FOREACH(dwp, &slaxDebugWatches, dwp_link) {
	if (streq(dwp->dwp_name, where + 1)) {
	    slaxOutput("Duplicate watchpoint");
	    return;
	}
    }

    char *condition = slaxSlaxToXpath("sdb", 1, where, NULL);
    if (condition == NULL) {
	slaxOutput("Invalid expression");
	return;
    }

    xmlXPathCompExprPtr comp;
    comp = xsltXPathCompile(statep->ds_script, (const xmlChar *) condition,
			     NULL);
    if (comp == NULL) {
	slaxOutput("Invalid expression");
	xmlFreeAndEasy(condition);
	return;
    }

    xmlXPathFreeCompExpr(comp);

    dwp = xmlMalloc(sizeof(*dwp));
    if (dwp == NULL)
	return;

    bzero(dwp, sizeof(*dwp));
    dwp->dwp_where = xmlStrdup2(where);
    dwp->dwp_name = xmlStrdup2(where + 1);
    dwp->dwp_condition = condition;
    dwp->dwp_num = ++slaxDebugWatchNumber;

    TAILQ_INSERT_TAIL(&slaxDebugWatches, dwp, dwp_link);

    slaxOutput("Watchpoint %d: %s", dwp->dwp_num, dwp->dwp_where);
}

/*
 * 'watch' command
 */
static void
slaxDebugCmdWatch (DC_ARGS)
{
    if (argv[1] && slaxDebugIsAbbrev("variable", argv[1])) {
	slaxDebugCmdWatchVariable(statep, commandline, argv);
	return;
    }

    slaxOutput("usage: watch variable $name");
}

/*
 * 'clear watch variable [$var | num]' subcommand
 */
static void
slaxDebugCmdClearWatch (DC_ARGS)
{
    slaxDebugWatch_t *dwp;
    uint num;
    const char *target = argv[3];

    if (argv[2] == NULL || !slaxDebugIsAbbrev("variable", argv[2])
	    || target == NULL) {
	slaxOutput("usage: clear watch variable [$name | num]");
	return;
    }

    if (target[0] == '$') {
	TAILQ_FOREACH(dwp, &slaxDebugWatches, dwp_link) {
	    if (streq(dwp->dwp_name, target + 1)) {
		num = dwp->dwp_num;
		slaxDebugFreeWatch(dwp);
		slaxOutput("Deleted watchpoint %d (%s)", num, target);
		return;
	    }
	}

	slaxOutput("No watchpoint on variable \"%s\"", target);
	return;
    }

    num = atoi(target);
    if (num <= 0) {
	slaxOutput("Invalid watchpoint number");
	return;
    }

    TAILQ_FOREACH(dwp, &slaxDebugWatches, dwp_link) {
	if (dwp->dwp_num == num) {
	    slaxDebugFreeWatch(dwp);
	    slaxOutput("Deleted watchpoint %d", num);
	    return;
	}
    }

    slaxOutput("Watchpoint %d not found", num);
}

/*
 * 'clear' command
 */
static void
slaxDebugCmdClear (DC_ARGS)
{
    if (argv[1] && slaxDebugIsAbbrev("watch", argv[1])) {
	slaxDebugCmdClearWatch(statep, commandline, argv);
	return;
    }

    slaxOutput("usage: clear watch variable [$name | num]");
}

static int
slaxDebugCheckDone (slaxDebugState_t *statep UNUSED)
{
    int status = xsltGetDebuggerStatus();
    if (status == 0 || status == XSLT_DEBUG_DONE) {
	slaxOutput("The script is not being run");
	return TRUE;
    }

    return FALSE;
}

static int
slaxDebugCheckStart (slaxDebugState_t *statep UNUSED)
{
    int status = xsltGetDebuggerStatus();

    if (status == 0) {
	statep->ds_flags |= DSF_RESTART | DSF_DISPLAY;
	return FALSE;

    } else if (status == XSLT_DEBUG_DONE) {
	slaxOutput("The script is not being run");
	return TRUE;
    }

    return FALSE;
}

/*
 * 'continue' command
 */
static void
slaxDebugCmdContinue (DC_ARGS)
{
    xmlNodePtr node;

    if (slaxDebugCheckStart(statep))
	return;

    slaxDebugClearListInfo(statep);

    if (argv && argv[1]) {
	node = slaxDebugGetNode(statep, argv[1]);
	if (node == NULL) {
	    slaxOutput("Unknown location: %s", argv[1]);
	    return;
	}

	statep->ds_stop_at = node;
    }

    xsltSetDebuggerStatus(XSLT_DEBUG_CONT);
    statep->ds_flags |= DSF_DISPLAY | DSF_CONTINUE;
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
	cp = slaxInput(prompt, 0);
	slaxDebugEchoInput(cp);

	if (!streq(cp, "y") && !streq(cp, "yes"))
	    return;

	slaxDebugClearBreakpoints();
	slaxOutput("Deleted all breakpoints");
	xmlFree(cp);
	return;
    }

    num = atoi(argv[1]);
    if (num <= 0) {
	slaxOutput("Invalid breakpoint number");
	return;
    }

    /*
     * Inefficient way of finding the patricia node for a breakpoint number,
     * but still ok, should be fine for debugger
     */
    TAILQ_FOREACH(dbpp, &slaxDebugBreakpoints, dbp_link) {
	if (dbpp->dbp_num == num) {
	    slaxDebugFreeBreakpoint(dbpp);
	    slaxOutput("Deleted breakpoint '%d'", num);
	    return;
	}
    }

    slaxOutput("Breakpoint '%d' not found", num);
}

/*
 * 'condition' command -- add, change, or clear the condition on an
 * existing breakpoint, without deleting and recreating it.
 */
static void
slaxDebugCmdCondition (DC_ARGS)
{
    slaxDebugBreakpoint_t *dbp;
    uint num;
    char *condition = NULL;
    char *condition_user = NULL;
    int found = FALSE;

    if (argv[1] == NULL) {
	slaxOutput("Missing breakpoint number");
	return;
    }

    num = atoi(argv[1]);
    if (num <= 0) {
	slaxOutput("Invalid breakpoint number");
	return;
    }

    TAILQ_FOREACH(dbp, &slaxDebugBreakpoints, dbp_link) {
	if (dbp->dbp_num == num) {
	    found = TRUE;
	    break;
	}
    }

    if (!found) {
	slaxOutput("Breakpoint '%d' not found", num);
	return;
    }

    /* If a new condition was given, make sure it parses */
    if (argv[2]) {
	condition = ALLOCADUP(commandline);
	condition = slaxDebugFindCondition(condition);
	if (*condition == '\0') {
	    slaxOutput("Missing expression");
	    return;
	}

	/* Keep the user's original SLAX spelling around for display */
	condition_user = xmlStrdup2(condition);

	condition = slaxSlaxToXpath("sdb", 1, condition, NULL);
	if (condition == NULL) {
	    slaxOutput("Invalid expression");
	    xmlFreeAndEasy(condition_user);
	    return;
	}

	xmlXPathCompExprPtr comp;
	comp = xsltXPathCompile(statep->ds_script, (const xmlChar *) condition,
				 NULL);
	if (comp == NULL) {
	    slaxOutput("Invalid expression");
	    xmlFreeAndEasy(condition);
	    xmlFreeAndEasy(condition_user);
	    return;
	}

	xmlXPathFreeCompExpr(comp);
    }

    xmlFreeAndEasy(dbp->dbp_condition);
    xmlFreeAndEasy(dbp->dbp_condition_user);
    dbp->dbp_condition = condition;
    dbp->dbp_condition_user = condition_user;
    dbp->dbp_flags &= ~DBPF_COND_WARNED;

    if (condition)
	slaxOutput("Breakpoint %d now conditional on '%s'", num, condition_user);
    else
	slaxOutput("Breakpoint %d is now unconditional", num);
}

/*
 * 'help' command
 */
static void
slaxDebugCmdHelp (DC_ARGS)
{
    slaxDebugCommand_t *cmdp;

    if (argv[1]) {
	cmdp = slaxDebugGetCommand(argv[1]);
	if (cmdp == NULL)
	    slaxOutput("Unknown command \"%s\".  Try \"help\"", argv[1]);
	else if (cmdp->dc_helpfunc)
	    cmdp->dc_helpfunc(statep);
	else if (cmdp->dc_help)
	    slaxOutput("  %s", cmdp->dc_help);
	else 
	    slaxOutput("No help is available");
	
    } else {
	slaxOutput("List of commands:");
	for (cmdp = slaxDebugCmdTable; cmdp->dc_command; cmdp++) {
	    if (cmdp->dc_help)
		slaxOutput("  %s", cmdp->dc_help);
	}
	slaxOutput("%s", "");	/* Avoid compiler warning */
	slaxOutput("Command name abbreviations are allowed");
    }
}

static void
slaxDebugHelpInfo (DH_ARGS)
{
    slaxOutput("List of commands:");
    slaxOutput("  info breakpoints  Display current breakpoints");
    slaxOutput("  info insert       Display current insertion point");
    slaxOutput("  info locals       Display local variables");
    slaxOutput("  info output       Display output document");
    slaxOutput("  info profile [brief | wall]  Report profiling information");
    slaxOutput("  info watch        Display current watchpoints");
}

static void
slaxDebugInfoBreakpoints (slaxDebugState_t *statep)
{
    xsltTemplatePtr template;
    slaxDebugBreakpoint_t *dbp;
    const char *tag;
    char buf[BUFSIZ];
    int hit = 0;

    TAILQ_FOREACH(dbp, &slaxDebugBreakpoints, dbp_link) {
	if (++hit == 1)
	    slaxOutput("List of breakpoints:");

	tag = (dbp->dbp_inst == statep->ds_node) ? "*" : " ";
	template = slaxDebugGetTemplate(statep, dbp->dbp_inst);

	if (dbp->dbp_inst) {
	    char *cond = dbp->dbp_condition_user;
	    slaxOutput("    %s#%d %s at %s:%ld%s%s%s",
		       tag, dbp->dbp_num,
		       slaxDebugTemplateInfo(template, buf, sizeof(buf)),
		       xmlNodeGetDoc(dbp->dbp_inst) ? xmlDocGetURL(xmlNodeGetDoc(dbp->dbp_inst)) : slaxNull,
		       xmlGetLineNo(dbp->dbp_inst),
		       cond ? " condition: '" : "", cond ?: "",
		       cond ? "'" : "");
	} else
	    slaxOutput("    #%d %s (orphaned)", dbp->dbp_num, dbp->dbp_where);
    }

    if (hit == 0)
	slaxOutput("No breakpoints");
}

static void
slaxDebugInfoWatch (void)
{
    slaxDebugWatch_t *dwp;
    int hit = 0;

    TAILQ_FOREACH(dwp, &slaxDebugWatches, dwp_link) {
	if (++hit == 1)
	    slaxOutput("List of watchpoints:");

	slaxOutput("    #%d %s%s%s", dwp->dwp_num, dwp->dwp_where,
		   dwp->dwp_have_value ? ", current value: " : "",
		   dwp->dwp_have_value ? dwp->dwp_value : "");
    }

    if (hit == 0)
	slaxOutput("No watchpoints");
}

/*
 * Return the template/match name for a stack frame, or NULL if the
 * frame has no name, such as a pseudo-frame pushed for an
 * <xsl:call-template> instruction itself rather than for the
 * template it calls.  This is the same test 'where' uses to decide
 * which frames to show by default; 'up'/'down' use it too, so that
 * frame offsets/numbers agree with what 'where' displays.  If
 * "tagp" is non-NULL, it's filled in with the "()" suffix used for
 * named (as opposed to matched) templates.
 */
static const char *
slaxDebugFrameName (slaxDebugStackFrame_t *stp, const char **tagp)
{
    const char *name = NULL;
    const char *tag = "";

    if (stp->st_template) {
	if (xsltTemplateGetMatch(stp->st_template)) {
	    name = (const char *) xsltTemplateGetMatch(stp->st_template);
	} else if (xsltTemplateGetName(stp->st_template)) {
	    name = (const char *) xsltTemplateGetName(stp->st_template);
	    tag = "()";
	}
    }

    if (tagp)
	*tagp = tag;

    return name;
}

/*
 * Return the stack frame "offset" (named) frames out from the
 * innermost (live) frame.  Offset 0 is the innermost named frame,
 * which is where execution is actually sitting right now; larger
 * offsets walk toward the outermost (head) frame.  Unnamed
 * pseudo-frames (see slaxDebugFrameName) are skipped entirely, so
 * offsets here line up with the frame numbers 'where' displays.
 * Returns NULL if the stack doesn't have that many named frames,
 * including when the stack is empty.
 */
static slaxDebugStackFrame_t *
slaxDebugFrameAt (int offset)
{
    slaxDebugStackFrame_t *stp;

    if (offset < 0)
	return NULL;

    stp = TAILQ_LAST(&slaxDebugStack, slaxDebugStack_s);
    while (stp != NULL && slaxDebugFrameName(stp, NULL) == NULL)
	stp = TAILQ_PREV(stp, slaxDebugStack_s, st_link);

    while (stp != NULL && offset-- > 0) {
	do {
	    stp = TAILQ_PREV(stp, slaxDebugStack_s, st_link);
	} while (stp != NULL && slaxDebugFrameName(stp, NULL) == NULL);
    }

    return stp;
}

/*
 * Resolve the local-variable range (ctxt, start, stop) for the
 * currently selected frame (see "up"/"down").  The live/innermost
 * frame (ds_frame_selected == 0) uses the live context's own
 * current vars range, since its stack frame's st_locals_stop isn't
 * finalized until a deeper frame is pushed; any other, outer frame
 * uses its own recorded st_locals_start/st_locals_stop.  "templatep",
 * if non-NULL, is filled in with the selected frame's template, so
 * callers can report params that are declared but not yet bound
 * (see slaxDebugUninitializedParams).  Returns FALSE (and prints a
 * message) if there's no live context to fall back on.
 */
static int
slaxDebugLocalsRange (slaxDebugState_t *statep, xsltTransformContextPtr *ctxtp,
		      int *startp, int *stopp, xsltTemplatePtr *templatep)
{
    slaxDebugStackFrame_t *stp = slaxDebugFrameAt(statep->ds_frame_selected);

    if (statep->ds_frame_selected == 0 || stp == NULL) {
	xsltTransformContextPtr ctxt = statep->ds_ctxt;

	if (ctxt == NULL) {
	    slaxOutput("The script is not being run");
	    return FALSE;
	}

	*ctxtp = ctxt;
	*startp = xsltTransformContextGetVarsBase(ctxt);
	*stopp = xsltTransformContextGetVarsNr(ctxt);
	if (templatep)
	    *templatep = statep->ds_template;

    } else {
	*ctxtp = stp->st_ctxt;
	*startp = stp->st_locals_start;
	*stopp = stp->st_locals_stop;
	if (templatep)
	    *templatep = stp->st_template;
    }

    return TRUE;
}

/*
 * Report a template's own declared <xsl:param>s as "uninitialized".
 * libxslt binds an xsl:param's value lazily, on first reference,
 * rather than when the template is entered; if nothing in the
 * template's own body (before the point we stopped at) has
 * referenced a param yet, it has no slot in ctxt->varsTab[] at all,
 * even though it's plainly declared.  This walks the template's own
 * source element for direct <xsl:param> children and reports each
 * by name, so "no local variables" doesn't hide the fact that the
 * template does have declared parameters -- they just haven't been
 * bound yet.
 */
static int
slaxDebugUninitializedParams (xsltTemplatePtr template)
{
    xmlNodePtr elem, child;
    int count = 0;

    if (template == NULL)
	return 0;

    elem = xsltTemplateGetElem(template);
    if (elem == NULL)
	return 0;

    for (child = xmlNodeGetChildren(elem); child;
	 child = xmlNodeGetNext(child)) {
	if (!slaxNodeIsXsl(child, ELT_PARAM))
	    continue;

	xmlChar *pname = xmlGetProp(child, (const xmlChar *) ATT_NAME);
	if (pname) {
	    if (count++ == 0)
		slaxOutput("Local variables:");
	    slaxOutput("param $%s = [uninitialized]", pname);
	    xmlFree(pname);
	}
    }

    return count;
}

/*
 * Dump the variables in a context, over the range [start, stop)
 * of ctxt->varsTab[].  "template", if non-NULL, is the frame's own
 * template, used to report declared-but-not-yet-bound params (see
 * slaxDebugUninitializedParams) when the range comes up empty.
 */
static void
slaxDebugContextVariables (xsltTransformContextPtr ctxt, int start, int stop,
			   xsltTemplatePtr template)
{
    int i;
    const char *type, *name;
    char buf[BUFSIZ];

    if (ctxt == NULL) {
	slaxOutput("The script is not being run");
	return;
    }

    if (stop <= start) {
	if (slaxDebugUninitializedParams(template) == 0)
	    slaxOutput("no local variables");
	return;
    }

    slaxOutput("Local variables:");
    for (i = stop; i > start; i--) {
        xsltStackElemPtr cur = xsltTransformContextGetVarsEntry(ctxt, i - 1);
	xmlXPathObjectPtr value;
	xsltStylePreCompPtr comp;
	const xmlChar *curName;

	if (cur == NULL)
	    continue;

	/*
	 * Each slot in the vars stack already holds exactly one
	 * variable (xsltAddStackElem() pushes a chain by storing
	 * each member in its own slot), but that member's own
	 * "next" pointer still dangles into the slots that follow
	 * it (the rest of the chain it was pushed with).  Those
	 * slots are visited separately by this same loop, so we
	 * must not follow "next" here -- doing so re-walks and
	 * re-prints variables that belong to later (lower i) slots,
	 * once for every slot that precedes them in the chain.
	 */
	value = xsltStackElemGetValue(cur);
	comp = xsltStackElemGetComp(cur);
	curName = xsltStackElemGetName(cur);

	type = "local";
	name = "unknown";

	if (curName)
	    name = (const char *) curName;

	if (comp == NULL) {
	    type = "invalid";

	} else if (xsltStylePreCompGetType(comp) == XSLT_FUNC_PARAM
		   || xsltStylePreCompGetType(comp) == XSLT_FUNC_WITHPARAM) {
	    /*
	     * A param whose value comes from the caller's
	     * <xsl:with-param> (rather than from this template's own
	     * default) is pushed with the with-param's own precomp,
	     * not the <xsl:param>'s -- but it's still a parameter of
	     * this template, so it gets the same "param" label.
	     */
	    type = "param";

	} else if (xsltStylePreCompGetType(comp) == XSLT_FUNC_VARIABLE) {
	    type = "var";
	}

	snprintf(buf, sizeof(buf), "%s $%s%s", type, name,
		   value ? " = " : " -- null value");
	if (value)
	    slaxDebugOutputXpath(value, buf, TRUE);
	else
	    slaxOutput("%s", buf);
    }
}

/*
 * Dump the variables in a context
 */
static void
slaxDebugCmdLocals (DC_ARGS)
{
    xsltTransformContextPtr ctxt;
    int start, stop;
    xsltTemplatePtr template;

    if (!slaxDebugLocalsRange(statep, &ctxt, &start, &stop, &template))
	return;

    slaxDebugContextVariables(ctxt, start, stop, template);
}

static int
slaxDebugCheckContext (xsltTransformContextPtr ctxt)
{
    if (ctxt == NULL) {
	slaxOutput("The script is not being run");
	return TRUE;
    }

    return FALSE;
}

/*
 * 'info' command
 */
static void
slaxDebugCmdInfo (DC_ARGS)
{
    xsltTransformContextPtr ctxt = statep->ds_ctxt;

    if (argv[1] == NULL || argv[1][0] == '\0'
		|| slaxDebugIsAbbrev("breakpoints", argv[1])) {
	slaxDebugInfoBreakpoints(statep);

    } else if (slaxDebugIsAbbrev("insert", argv[1])) {
	if (slaxDebugCheckContext(ctxt))
	    return;

	if (xsltTransformContextGetInsert(ctxt) == NULL) {
	    slaxOutput("context insertion point is NULL");
	} else {
	    slaxOutput("[context insertion point]");
	    slaxOutputNode(xsltTransformContextGetInsert(ctxt));
	}

    } else if (slaxDebugIsAbbrev("nodes", argv[1])) {
	if (statep->ds_script && xsltStylesheetGetDoc(statep->ds_script))
	    slaxDebugListNodeByLine(
		xmlDocGetChildren(xsltStylesheetGetDoc(statep->ds_script)));

    } else if (slaxDebugIsAbbrev("output", argv[1])) {
	if (slaxDebugCheckContext(ctxt))
	    return;

	if (xsltTransformContextGetOutput(ctxt) == NULL) {
	    slaxOutput("context output document is NULL");
	} else {
	    slaxOutput("[context output document]");
	    slaxDumpToFd(1, xsltTransformContextGetOutput(ctxt), FALSE);
	}

    } else if (slaxDebugIsAbbrev("locals", argv[1])) {
	xsltTransformContextPtr lctxt;
	int start, stop;
	xsltTemplatePtr ltemplate;

	if (slaxDebugCheckContext(ctxt))
	    return;

	if (!slaxDebugLocalsRange(statep, &lctxt, &start, &stop, &ltemplate))
	    return;

	slaxDebugContextVariables(lctxt, start, stop, ltemplate);

    } else if (slaxDebugIsAbbrev("profile", argv[1])) {
	slaxDebugFlags_t flags = 0;

	if (argv[2] && slaxDebugIsAbbrev("brief", argv[2]))
	    flags |= SDBF_PROFILE_BRIEF;

	if (argv[2] && slaxDebugIsAbbrev("wall", argv[2]))
	    flags |= SDBF_PROFILE_WALL;

	slaxProfReport(flags, statep->ds_script_buffer);

    } else if (slaxDebugIsAbbrev("watch", argv[1])) {
	slaxDebugInfoWatch();

    } else if (slaxDebugIsAbbrev("help", argv[1])) {
	slaxDebugHelpInfo(statep);

    } else
	slaxOutput("Undefined command: \"%s\".  Try \"help\"", argv[1]);
}

/*
 * 'list' command
 */
static void
slaxDebugCmdList (DC_ARGS)
{
    xmlNodePtr node;
    int line_no;

    if (argv[1]) {
	node = slaxDebugGetNode(statep, argv[1]);
	if (node) {
	    line_no = xmlGetLineNo(node);
	} else {
	    line_no = atoi(argv[1]);
	    if (line_no <= 0) {
		/* Not a line number either; maybe it's just a filename */
		xsltStylesheetPtr fstyle
		    = slaxDebugGetFile(statep->ds_script, argv[1]);

		if (fstyle == NULL) {
		    slaxOutput("invalid target: %s", argv[1]);
		    return;
		}

		node = xmlDocGetChildren(xsltStylesheetGetDoc(fstyle));
		line_no = xmlGetLineNo(node) ?: 1;

	    } else {
		/* We have a line number but no node */
		if (statep->ds_list_node)
		    node = statep->ds_list_node;

		else if (statep->ds_inst)
		    node = statep->ds_inst;

		else if (xsltStylesheetGetDoc(statep->ds_script))
		    node = xmlDocGetRootElement(xsltStylesheetGetDoc(statep->ds_script));

		else {
		    slaxOutput("unknown location");
		    return;
		}
	    }
	}

    } else if (statep->ds_list_node) {
	node = statep->ds_list_node;
	line_no = statep->ds_list_line;

    } else if (statep->ds_inst) {
	node = statep->ds_inst;
	line_no = xmlGetLineNo(node);

    } else if (xsltStylesheetGetDoc(statep->ds_script)) {
	node = xmlDocGetChildren(xsltStylesheetGetDoc(statep->ds_script));
	line_no = xmlGetLineNo(node) ?: 1;
    } else {
	slaxOutput("no target");
	return;
    }

    if (node && xmlNodeGetDoc(node)) {
	slaxDebugOutputScriptLines(statep, (const char *) xmlDocGetURL(xmlNodeGetDoc(node)),
				   line_no, line_no + DEBUG_LIST_COUNT);
	statep->ds_list_node = node;
	statep->ds_list_line = line_no + DEBUG_LIST_COUNT;
    } else {
	slaxOutput("no target");
    }
}

/*
 * Set the display mode
 */
static void
slaxDebugCmdMode (DC_ARGS)
{
    if (streq(argv[1], "emacs"))
	slaxDebugDisplayMode = DEBUG_MODE_EMACS;
    else if (streq(argv[1], "cli"))
	slaxDebugDisplayMode = DEBUG_MODE_CLI;
}

/*
 * 'finish' command
 */
static void
slaxDebugCmdFinish (DC_ARGS)
{
    slaxDebugStackFrame_t *stp, *last = NULL;

    if (slaxDebugCheckDone(statep))
	return;

    slaxDebugClearListInfo(statep);

    /*
     * Walk the stack linked list in reverse order, looking for the
     * lowest template.  When we find it, turn on the "stop-when-pop"
     * flag so we'll stop when it's popped by dropFrame.  Then mark
     * the debugger for "continue".
     */
    TAILQ_FOREACH_REVERSE(stp, &slaxDebugStack, slaxDebugStack_s, st_link) {
	if (slaxNodeIsXsl(stp->st_inst, ELT_TEMPLATE)) {
	    stp->st_flags |= STF_STOPWHENPOP;
	    statep->ds_flags |= DSF_DISPLAY;
	    xsltSetDebuggerStatus(XSLT_DEBUG_CONT);
	    return;
	}
	last = stp;
    }

    if (last) {
	last->st_flags |= STF_STOPWHENPOP;
	statep->ds_flags |= DSF_DISPLAY;
	xsltSetDebuggerStatus(XSLT_DEBUG_CONT);
	return;
    }

    slaxOutput("template not found");
}

/*
 * 'step' command
 */
static void
slaxDebugCmdStep (DC_ARGS)
{
    if (slaxDebugCheckStart(statep))
	return;

    slaxDebugClearListInfo(statep);

    xsltSetDebuggerStatus(XSLT_DEBUG_STEP);
    statep->ds_flags |= DSF_DISPLAY;
}

/*
 * 'next' command.  Is we are on a "call", then act like "over".
 * Otherwise, act like "step".
 */
static void
slaxDebugCmdNext (DC_ARGS)
{
    if (slaxDebugCheckStart(statep))
	return;

    slaxDebugClearListInfo(statep);

    if (slaxNodeIsXsl(statep->ds_inst, ELT_CALL_TEMPLATE)) {
	xsltSetDebuggerStatus(XSLT_DEBUG_OVER);
	statep->ds_flags |= DSF_OVER | DSF_DISPLAY;
    } else {
	xsltSetDebuggerStatus(XSLT_DEBUG_STEP);
	statep->ds_flags |= DSF_DISPLAY;
    }
}

/*
 * 'over' command
 */
static void
slaxDebugCmdOver (DC_ARGS)
{
    if (slaxDebugCheckDone(statep))
	return;

    slaxDebugClearListInfo(statep);

    if (slaxNodeIsXsl(statep->ds_inst, ELT_CALL_TEMPLATE)) {
	xsltSetDebuggerStatus(XSLT_DEBUG_OVER);
	statep->ds_flags |= DSF_OVER | DSF_DISPLAY;
    } else {
	xsltSetDebuggerStatus(XSLT_DEBUG_STEP);
	statep->ds_flags |= DSF_DISPLAY;
    }
}

/**
 * If a non-live frame is selected (see "up"/"down"), temporarily narrow
 * @ctxt's varsNr/varsBase to that frame's own local-variable range, so
 * that variable lookups and evaluations against @ctxt see the selected
 * frame's variables rather than always the innermost/live ones. Returns
 * TRUE if the scope was narrowed (in which case the caller must restore
 * it with slaxDebugUnscopeFrame(), passing back *old_nrp and *old_basep),
 * FALSE otherwise (nothing to restore).
 */
static int
slaxDebugScopeToFrame (slaxDebugState_t *statep, xsltTransformContextPtr ctxt,
			int *old_nrp, int *old_basep)
{
    if (ctxt != NULL && statep->ds_frame_selected != 0) {
	xsltTransformContextPtr fctxt;
	int start, stop;

	if (slaxDebugLocalsRange(statep, &fctxt, &start, &stop, NULL)
	    && fctxt == ctxt) {
	    *old_nrp = xsltTransformContextGetVarsNr(ctxt);
	    *old_basep = xsltTransformContextGetVarsBase(ctxt);

	    xsltTransformContextSetVarsNr(ctxt, stop);
	    xsltTransformContextSetVarsBase(ctxt, start);
	    return TRUE;
	}
    }

    return FALSE;
}

/*
 * Undo slaxDebugScopeToFrame(), restoring the vars range it saved.
 */
static void
slaxDebugUnscopeFrame (xsltTransformContextPtr ctxt, int old_nr, int old_base)
{
    xsltTransformContextSetVarsNr(ctxt, old_nr);
    xsltTransformContextSetVarsBase(ctxt, old_base);
}

/**
 *
 * The caller must free the results with xmlXPathFreeObject(res);
 *
 * If a non-live frame is selected (see "up"/"down"), the live
 * context's varsNr/varsBase are temporarily narrowed to that
 * frame's own local-variable range for the duration of the
 * evaluation, so "print" (and anything else built on this
 * function) sees the selected frame's variables rather than always
 * the innermost/live ones.
 */
static xmlXPathObjectPtr
slaxDebugEvalXpath (slaxDebugState_t *statep, const char *expr)
{
    if (slaxDebugCheckDone(statep))
	return NULL;

    xsltTransformContextPtr ctxt = statep->ds_ctxt;
    xmlXPathContextPtr xpctxt;
    xmlXPathObjectPtr res;
    int old_nr = 0, old_base = 0;
    int scoped;

    if (ctxt == NULL)
	return NULL;

    xpctxt = xsltTransformContextGetXpathCtxt(ctxt);
    if (xpctxt == NULL)
	return NULL;

    scoped = slaxDebugScopeToFrame(statep, ctxt, &old_nr, &old_base);

    /*
     * A failure evaluating this expression (e.g. an undefined
     * variable in a breakpoint condition, or in something the user
     * typed to "print"/"set") is not a script error, so tell
     * slaxDebugErrorHandler() not to treat it as one.
     */
    statep->ds_flags |= DSF_INEVAL;

    res = slaxXpathEval(statep->ds_node, statep->ds_inst, xpctxt,
			 statep->ds_script, expr);

    statep->ds_flags &= ~DSF_INEVAL;

    if (scoped)
	slaxDebugUnscopeFrame(ctxt, old_nr, old_base);

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
    while (*cp && !isspace((int) *cp))
	cp += 1;
    while (*cp && isspace((int) *cp))
	cp += 1;

    res = slaxDebugEvalXpath(statep, cp);
    if (res) {
	slaxDebugOutputXpath(res, NULL, TRUE);
	xmlXPathFreeObject(res);
	slaxOutput("%s", ""); /* Avoid compiler warning */
    }
}

static void
slaxDebugHelpDump (DH_ARGS)
{
    slaxOutput("List of commands:");
    slaxOutput("  dump var $var   Dump information on a variable");
    slaxOutput("  dump expr $expr Dump an expression (nodeset)");
}

/*
 * 'print' command
 * @bugs need to be more like "eval" functionality (e.g. "print $x/name")
 */
static void
slaxDebugCmdDump (DC_ARGS)
{
    const char *arg = argv[1];

    if (arg) {
	if (slaxDebugIsAbbrev("var", arg)) {
	    if (argv[2] == NULL) {
		slaxOutput("missing variable name");
		return;
	    }

	    for (argv += 2; *argv; argv++) {
		const char *name = *argv;
		if (name[0] == '$')
		    name += 1;	/* Skip over leading $var */

		xsltStackElemPtr var
		    = slaxMvarLookupQname(statep->ds_ctxt,
					  (const xmlChar *) name, NULL);
		if (var == NULL) {
		    slaxOutput("variable not found: $%s", name);
		    return;
		}

		slaxOutput("variable: $%s/%p", name, var);
		slaxDumpVar(var);
	    }

	} else if (slaxDebugIsAbbrev("expr", arg)) {
	    /*
	     * We need to reconstruct the command line by taking the
	     * raw command, and moving over command name ("dump expr").
	     */
	    char *cp = ALLOCADUP(commandline);

	    while (*cp && !isspace((int) *cp))
		cp += 1;
	    while (*cp && isspace((int) *cp)) /* "dump" */
		cp += 1;
	    while (*cp && !isspace((int) *cp))
		cp += 1;
	    while (*cp && isspace((int) *cp)) /* "expr" */
		cp += 1;

	    xmlXPathObjectPtr res = slaxDebugEvalXpath(statep, cp);
	    if (res) {
		slaxDebugOutputXpath(res, NULL, TRUE);
		slaxDumpObject(res);
		xmlXPathFreeObject(res);
	    }

	} else {
	    slaxOutput("invalid command: %s", arg);
	    return;
	}
    }
}

static inline char *
slaxDebugSkipToken (char *str)
{
    while (*str && !isspace((int) *str))
	str += 1;

    return str;
}

static inline char *
slaxDebugSkipSpace (char *str)
{
    while (*str && isspace((int) *str))
	str += 1;

    return str;
}

/*
 * Shared implementation of the 'set' and 'append' commands: parse
 * "$name = value" / "$name += value" ourselves (the grammar's
 * M_PARSE_INITIAL_VALUE entry point only ever sees "value"), evaluate
 * "value" against the live transform context, and store it into
 * $name via slaxMvarSet()/slaxMvarAppend().
 */
static void
slaxDebugCmdSetCommon (slaxDebugState_t *statep, const char *commandline,
		       int is_append)
{
    char *cp = ALLOCADUP(commandline);
    char name[BUFSIZ];
    int i;

    /* Move over the command word ("set" / "append"), same as print/dump */
    cp = slaxDebugSkipToken(cp);
    cp = slaxDebugSkipSpace(cp);

    if (*cp++ != '$') {
	slaxOutput("expected a variable name ($name)");
	return;
    }

    for (i = 0; i < (int) sizeof(name) - 1 && slaxIsVarChar(*cp); i++, cp++)
	name[i] = *cp;
    name[i] = '\0';

    if (i == 0) {
	slaxOutput("expected a variable name ($name)");
	return;
    }

    cp = slaxDebugSkipSpace(cp);

    if (is_append) {
	if (cp[0] != '+' || cp[1] != '=') {
	    slaxOutput("usage: append $%s += value", name);
	    return;
	}
	cp += 2;

    } else {
	if (cp[0] != '=') {
	    slaxOutput("usage: set $%s = value", name);
	    return;
	}
	cp += 1;
    }

    cp = slaxDebugSkipSpace(cp);
    if (*cp == '\0') {
	slaxOutput("missing value for variable '$%s'", name);
	return;
    }

    /* Copy the remainder so we can safely extend it by one char */
    size_t len = strlen(cp);
    char *buf = alloca(len + 2);
    memcpy(buf, cp, len + 1);

    /*
     * Trim trailing whitespace, then require a trailing ';' unless the
     * value is a block/element-constructor/braced-call ending in '}' --
     * that's the only thing initial_value's four alternatives need:
     * xpath_value requires L_EOS; call_stmt's parens-only form needs
     * it too (its braces form doesn't); block/element_stmt end in '}'
     * and must NOT get an extra ';' appended.  The user may have
     * already typed a trailing ';' themselves, so don't double it up.
     */
    char *end = buf + len;
    while (end > buf && isspace((int) end[-1]))
	end -= 1;
    *end = '\0';

    if (end == buf) {
	slaxOutput("missing value for variable '$%s'", name);
	return;
    }

    /* Add terminating semi-colon if needed */
    if (end[-1] != '}' && end[-1] != ';') {
	*end++ = ';';
	*end = '\0';
    }

    if (statep->ds_inst == NULL) {
	slaxOutput("no current context");
	return;
    }

    xmlNodePtr context_node = xmlNodeGetParent(statep->ds_inst);
    xmlNodePtr inst = slaxLoadBufferAt("sdb-set", buf, context_node,
					M_PARSE_INITIAL_VALUE);
    if (inst == NULL)
	return;	/* slaxLoadBufferAt() already reported the parse error */

    xmlXPathObjectPtr value = NULL;
    xmlChar *sel = xmlGetNsProp(inst, (const xmlChar *) ATT_SELECT, NULL);

    if (sel) {
	value = slaxDebugEvalXpath(statep, (const char *) sel);
	xmlFree(sel);

    } else if (xmlNodeGetChildren(inst)) {
	/*
	 * inst's content was never run through the stylesheet's own
	 * compile pass, so none of its xsl: instructions (value-of,
	 * element, etc) have the precomputed "comp" info that
	 * xsltApplyOneTemplate() (inside slaxMvarEvalBlock()) requires.
	 * xsltParseTemplateContent() is the same call libxslt itself
	 * uses to lazily compile content added after the initial parse
	 * (e.g. EXSLT's func:function body). It also validates that
	 * each xsl: instruction sits within a template/variable/param,
	 * walking up inst's real ancestors to find one -- which requires
	 * inst to be (temporarily) back in the live tree, since
	 * slaxLoadBufferAt() already unlinked it.
	 */
	xmlAddChild(context_node, inst);
	xsltParseTemplateContent(statep->ds_script, inst);
	xmlUnlinkNode(inst);

	xmlDocPtr tree = slaxMvarEvalBlock(statep->ds_ctxt, statep->ds_node,
					   inst);
	if (tree)
	    value = xmlXPathNewValueTree((xmlNodePtr) tree);

    } else {
	slaxOutput("missing value for variable '$%s'", name);
    }

    xmlFreeNode(inst);

    if (value == NULL)
	return;

    /*
     * Look up the target variable/param within the selected frame's own
     * scope (see slaxDebugScopeToFrame()) -- not always the live/innermost
     * frame's -- so that "up"/"down" followed by "set $foo = ..." affects
     * the $foo the user is actually looking at, rather than whichever
     * (possibly unrelated) $foo happens to be in scope at the real,
     * currently-running point of the script.
     */
    xsltTransformContextPtr ctxt = statep->ds_ctxt;
    int old_nr = 0, old_base = 0;
    int scoped = slaxDebugScopeToFrame(statep, ctxt, &old_nr, &old_base);

    int local;
    xsltStackElemPtr var = slaxMvarLookupQname(ctxt, (const xmlChar *) name,
						&local);

    if (scoped)
	slaxDebugUnscopeFrame(ctxt, old_nr, old_base);

    if (var == NULL) {
	slaxOutput("variable not found: $%s", name);
	xmlXPathFreeObject(value);
	return;
    }

    if (is_append)
	slaxMvarAppend(ctxt, (const xmlChar *) name, var, value);
    else
	slaxMvarSet((const xmlChar *) name, var, value);

    slaxDebugCheckWatchpoints(name);

    char tagbuf[BUFSIZ];
    snprintf(tagbuf, sizeof(tagbuf), "$%s = ", name);
    slaxDebugOutputXpath(xsltStackElemGetValue(var), tagbuf, TRUE);
}

/*
 * 'set' command
 */
static void
slaxDebugCmdSet (DC_ARGS)
{
    slaxDebugCmdSetCommon(statep, commandline, FALSE);
}

/*
 * 'append' command
 */
static void
slaxDebugCmdAppend (DC_ARGS)
{
    slaxDebugCmdSetCommon(statep, commandline, TRUE);
}

static void
slaxDebugHelpProfile (DH_ARGS)
{
    slaxOutput("List of commands:");
    slaxOutput("  profile callgraph <file>  Enable callgraph profiling, "
	       "writing output to <file>");
    slaxOutput("  profile flamegraph <file>  Enable flame-graph profiling, "
	       "writing output to <file>");
    slaxOutput("  profile clear   Clear  profiling information");
    slaxOutput("  profile off     Disable profiling");
    slaxOutput("  profile on      Enable profiling");
    slaxOutput("  profile report [brief | wall | callgraph | flamegraph] "
	       "[<file>]  Report profiling information");
}

/*
 * 'profiler [on|off]' command
 */
static void
slaxDebugCmdProfiler (DC_ARGS)
{
    const char *arg = argv[1];
    int enable = !(statep->ds_flags & DSF_PROFILER);

    if (arg) {
	if (streq("on", arg) || slaxDebugIsAbbrev("yes", arg)
	    || slaxDebugIsAbbrev("enable", arg))
	    enable = TRUE;

	else if (streq("off", arg) || slaxDebugIsAbbrev("no", arg)
		 || slaxDebugIsAbbrev("disable", arg))
	    enable = FALSE;

	else if (slaxDebugIsAbbrev("clear", arg)) {
	    slaxOutput("Clearing profile information");
	    slaxProfClear();
	    return;

	} else if (slaxDebugIsAbbrev("callgraph", arg)) {
	    const char *file = argv[2];

	    if (file == NULL) {
		slaxOutput("profile callgraph: missing <file> argument");
		return;
	    }

	    slaxOutput("Enabling callgraph profiling, output to %s", file);

	    statep->ds_flags &= ~(DSF_PROFILER | DSF_PROFILE_FLAMEGRAPH);
	    statep->ds_flags |= DSF_PROFILE_CALLGRAPH;
	    slaxDebugSetProfileFile(file);
	    return;

	} else if (slaxDebugIsAbbrev("flamegraph", arg)) {
	    const char *file = argv[2];

	    if (file == NULL) {
		slaxOutput("profile flamegraph: missing <file> argument");
		return;
	    }

	    slaxOutput("Enabling flame-graph profiling, output to %s", file);

	    statep->ds_flags &= ~(DSF_PROFILER | DSF_PROFILE_CALLGRAPH);
	    statep->ds_flags |= DSF_PROFILE_FLAMEGRAPH;
	    slaxDebugSetProfileFile(file);
	    return;

	} else if (slaxDebugIsAbbrev("report", arg)) {
	    slaxDebugFlags_t flags = 0;

	    if (argv[2] && slaxDebugIsAbbrev("callgraph", argv[2])) {
		const char *file = argv[3] ? argv[3] : statep->ds_profile_file;
		slaxProfReportCallgraph(statep->ds_callgraph_buffer, file);
		return;
	    }

	    if (argv[2] && slaxDebugIsAbbrev("flamegraph", argv[2])) {
		const char *file = argv[3] ? argv[3] : statep->ds_profile_file;
		slaxProfReportFlamegraph(statep->ds_flamegraph_hash, file);
		return;
	    }

	    if (argv[2] && slaxDebugIsAbbrev("brief", argv[2]))
		flags |= SDBF_PROFILE_BRIEF;

	    if (argv[2] && slaxDebugIsAbbrev("wall", argv[2]))
		flags |= SDBF_PROFILE_WALL;

	    slaxProfReport(flags, statep->ds_script_buffer);
	    return;

	} else if (slaxDebugIsAbbrev("help", arg)) {
	    slaxDebugHelpProfile(statep);
	    return;

	} else {
	    slaxOutput("invalid setting: %s", arg);
	    return;
	}
    }

    if (enable) {
	statep->ds_flags |= DSF_PROFILER;
	statep->ds_flags &= ~(DSF_PROFILE_CALLGRAPH | DSF_PROFILE_FLAMEGRAPH);
	slaxOutput("Enabling profiler");
    } else {
	statep->ds_flags &= ~DSF_PROFILER;
	slaxOutput("Disabling profiler");
    }

}

/*
 * Print the single summary line for one stack frame, in the same
 * "#<num> <template> at <file>:<line>" format that 'where' uses.
 * Shared with 'up'/'down' so the newly-selected frame is rendered
 * identically to how 'where' would show it.
 */
static void
slaxDebugPrintFrame (slaxDebugStackFrame_t *stp, int num)
{
    const char *tag;
    const char *name = slaxDebugFrameName(stp, &tag);
    const char *filename;
    char template_info[BUFSIZ];
    char from_info[BUFSIZ];
    xmlNodePtr caller;

    if (name)
	slaxDebugTemplateInfo(stp->st_template,
			      template_info, sizeof(template_info));
    else snprintf(template_info, sizeof(template_info),
		  "<%s%s%s>",
		  (stp->st_inst && xmlNodeGetNs(stp->st_inst)
		   && xmlNsGetPrefix(xmlNodeGetNs(stp->st_inst)))
		  ? xmlNsGetPrefix(xmlNodeGetNs(stp->st_inst)) : slaxNull,
		  (stp->st_inst && xmlNodeGetNs(stp->st_inst)
		   && xmlNsGetPrefix(xmlNodeGetNs(stp->st_inst))) ? ":" : "",
		  NAME(stp->st_inst));

    caller = stp->st_caller ?: stp->st_inst;

    const char *url = (const char *) xmlDocGetURL(xmlNodeGetDoc(caller));
    filename = strrchr(url, '/');
    filename = filename ? filename + 1
	: (const char *) xmlDocGetURL(xmlNodeGetDoc(caller));

    if (caller)
	snprintf(from_info, sizeof(from_info),
		 " at %s:%ld", filename ?: "", xmlGetLineNo(caller));
    else from_info[0] = '\0';

    slaxOutput("#%d %s%s%s", num, template_info, tag, from_info);
}

/*
 * 'where' command
 *  syntax: where [full]
 */
static void
slaxDebugCmdWhere (DC_ARGS)
{
    slaxDebugStackFrame_t *stp;
    const char *name;
    int num = 0;
    int full;

    if (slaxDebugCheckDone(statep))
	return;

    full = slaxDebugIsAbbrev("full", argv[1]);

    /*
     * Walk the stack linked list in reverse order and print it
     */
    TAILQ_FOREACH(stp, &slaxDebugStack, st_link) {
	name = slaxDebugFrameName(stp, NULL);

	if (name == NULL && !full)
	    continue;

	slaxDebugPrintFrame(stp, num);
	slaxLog("  locals %d .. %d",
		   stp->st_locals_start, stp->st_locals_stop);

	if (stp->st_inst && stp->st_ctxt) {
	    xsltStackElemPtr cur;
	    int start, stop, i;
	    char tbuf[BUFSIZ];
	    xsltTransformContextPtr ctxt = stp->st_ctxt;

	    /*
	     * We display the parameter list for the template
	     */
	    start = (stp->st_locals_start > 0) ? stp->st_locals_start : 0;
	    stop = stp->st_locals_stop ?:
		stp->st_locals_start ? xsltTransformContextGetVarsNr(ctxt) : 0;
	    for (i = start; i < stop; i++) {
		const xmlChar *curName;
		xmlXPathObjectPtr curValue;
		int curLevel;

		cur = xsltTransformContextGetVarsEntry(ctxt, i);
		if (cur == NULL)
		    continue;

		curLevel = xsltStackElemGetLevel(cur);
		if (!full && curLevel >= 0)
		    continue;

		curName = xsltStackElemGetName(cur);
		curValue = xsltStackElemGetValue(cur);

		slaxLog("    $%s (%d)", curName, curLevel);
		snprintf(tbuf, sizeof(tbuf), "    $%s = ", curName);

		if (curValue) {
		    slaxDebugOutputXpath(curValue, tbuf, FALSE);
		} else {
		    slaxOutput("%sNULL", tbuf);
		}
	    }
	}

	num += 1;
    }

    if (num == 0)
	slaxOutput("call stack is empty");
}

/*
 * Return the total number of named frames currently on the call
 * stack, i.e. the same set that 'where' numbers by default (see
 * slaxDebugFrameName).
 */
static int
slaxDebugFrameCount (void)
{
    slaxDebugStackFrame_t *stp;
    int count = 0;

    TAILQ_FOREACH(stp, &slaxDebugStack, st_link)
	if (slaxDebugFrameName(stp, NULL) != NULL)
	    count += 1;

    return count;
}

/*
 * Shared implementation of 'up' and 'down': move the selected frame
 * (statep->ds_frame_selected) toward the outermost frame (direction
 * == 1, for "up") or toward the innermost/live frame (direction ==
 * -1, for "down") by "n" frames (default 1), printing the newly
 * selected frame the same way 'where' would.
 *
 * Note that ds_frame_selected is an offset from the innermost
 * (tail) frame (0 == live/current), but 'where' numbers frames the
 * other way, from the outermost (head) frame.  We print the
 * 'where'-style number here, not the raw offset, so a frame is
 * labeled the same regardless of which command showed it.
 */
static void
slaxDebugChangeFrame (DC_ARGS, int direction)
{
    int n = 1;
    int new_offset;
    slaxDebugStackFrame_t *stp;

    if (slaxDebugCheckDone(statep))
	return;

    if (argv[1]) {
	n = atoi(argv[1]);
	if (n <= 0) {
	    slaxOutput("Invalid frame count");
	    return;
	}
    }

    new_offset = statep->ds_frame_selected + (direction * n);

    const char msg[] = "Current frame depth is #%d of %d; "
                   "cannot '%s' more than %d";
    int fnum = slaxDebugFrameCount() - statep->ds_frame_selected - 1;

    if (new_offset < 0) {
	slaxOutput(msg, fnum, slaxDebugFrameCount() - 1,
                   "down", statep->ds_frame_selected);
	return;
    }

    stp = slaxDebugFrameAt(new_offset);
    if (stp == NULL) {
	slaxOutput(msg, fnum, slaxDebugFrameCount() - 1,
                   "up", fnum);
	return;
    }

    statep->ds_frame_selected = new_offset;
    slaxDebugPrintFrame(stp, slaxDebugFrameCount() - 1 - new_offset);
}

/*
 * 'up' command
 *  syntax: up [n]
 */
static void
slaxDebugCmdUp (DC_ARGS)
{
    slaxDebugChangeFrame(statep, commandline, argv, 1);
}

/*
 * 'down' command
 *  syntax: down [n]
 */
static void
slaxDebugCmdDown (DC_ARGS)
{
    slaxDebugChangeFrame(statep, commandline, argv, -1);
}

static void
slaxDebugHelpCallFlow (DH_ARGS)
{
    slaxOutput("List of commands:");
    slaxOutput("  callflow off    Disable callflow tracing");
    slaxOutput("  callflow on     Enable callflow tracing");
}

/**
 * 'callflow' command
 */
static void
slaxDebugCmdCallFlow (DC_ARGS)
{
    const char *arg = argv[1];
    int enable = !(statep->ds_flags & DSF_CALLFLOW);

    if (arg) {
	if (streq("on", arg) || slaxDebugIsAbbrev("yes", arg)
		|| slaxDebugIsAbbrev("enable", arg))
	    enable = TRUE;

	else if (streq("off", arg) || slaxDebugIsAbbrev("no", arg)
		 || slaxDebugIsAbbrev("disable", arg))
	    enable = FALSE;

	else if (slaxDebugIsAbbrev("help", arg)) {
	    slaxDebugHelpCallFlow(statep);
	    return;

	} else {
	    slaxOutput("invalid setting: %s", arg);
	    return;
	}
    }

    if (enable) {
	statep->ds_flags |= DSF_CALLFLOW;
	slaxOutput("Enabling callflow");
    } else {
	statep->ds_flags &= ~DSF_CALLFLOW;
	slaxOutput("Disabling callflow");
    }
}
    
/*
 * 'reload' command
 */
static void
slaxDebugCmdReload (DC_ARGS)
{
    int status = xsltGetDebuggerStatus();
    int restart;

    if (status != 0 && status != XSLT_DEBUG_DONE
		&& status != XSLT_DEBUG_QUIT) {
	const char warning[] =
	    "The script being debugged has been started already";
	const char prompt[]
	    = "Reload and restart it from the beginning? (y or n) ";
	char *input;

	slaxOutput(warning);
	input = slaxInput(prompt, 0);
	slaxDebugEchoInput(input);
	if (input == NULL)
	    return;

	restart = slaxDebugCheckAbbrev("yes", 1, input, strlen(input));
	xmlFree(input);

	if (!restart)
	    return;
    }

    slaxDebugClearListInfo(statep);

    /* Tell the xslt engine to stop */
    xsltStopEngine(statep->ds_ctxt);

    /*
     * Use XSLT_DEBUG_QUIT, not XSLT_DEBUG_CONT, so that the main
     * loop's "status == XSLT_DEBUG_QUIT" check (right after the
     * in-progress xsltApplyStylesheet() call returns) sees this as
     * a stop needing attention and acts on DSF_RELOAD immediately --
     * matching how 'run' (see slaxDebugCmdRun(), just below) uses
     * XSLT_DEBUG_QUIT with DSF_RESTART.  With XSLT_DEBUG_CONT, that
     * check never matches, so DSF_RELOAD then sits unconsumed until
     * some later unrelated command's loop iteration happens to find
     * it still set.
     */
    xsltSetDebuggerStatus(XSLT_DEBUG_QUIT);
    statep->ds_flags |= DSF_RELOAD;
}
    
/*
 * 'run' command
 */
static void
slaxDebugCmdRun (DC_ARGS)
{
    int status = xsltGetDebuggerStatus();
    int restart;

    if (status != 0 && status != XSLT_DEBUG_DONE
		&& status != XSLT_DEBUG_QUIT) {
	const char warning[] =
	    "The script being debugged has been started already";
	const char prompt[] = "Start it from the beginning? (y or n) ";
	char *input;

	slaxOutput(warning);
	input = slaxInput(prompt, 0);
	slaxDebugEchoInput(input);
	if (input == NULL)
	    return;

	restart = slaxDebugCheckAbbrev("yes", 1, input, strlen(input));
	xmlFree(input);

	if (!restart)
	    return;
    }

    slaxDebugClearListInfo(statep);

    /* Tell the xslt engine to stop */
    xsltStopEngine(statep->ds_ctxt);

    xsltSetDebuggerStatus(XSLT_DEBUG_QUIT);
    statep->ds_flags |= DSF_RESTART | DSF_DISPLAY | DSF_CONTINUE;
}

static void
slaxDebugHelpVerbose (DH_ARGS)
{
    slaxOutput("List of commands:");
    slaxOutput("  verbose off     Disable verbose logging");
    slaxOutput("  verbose on      Enable verbose logging");
}

/*
 * 'verbose' command
 */
static void
slaxDebugCmdVerbose (DC_ARGS)
{
    const char *arg = argv[1];
    int enable = !slaxLogIsEnabled;

    if (arg) {
	if (streq("on", arg) || slaxDebugIsAbbrev("yes", arg)
		|| slaxDebugIsAbbrev("enable", arg))
	    enable = TRUE;

	else if (streq("off", arg) || slaxDebugIsAbbrev("no", arg)
		 || slaxDebugIsAbbrev("disable", arg))
	    enable = FALSE;

	else if (slaxDebugIsAbbrev("help", arg)) {
	    slaxDebugHelpVerbose(statep);
	    return;

	} else {
	    slaxOutput("invalid setting: %s", arg);
	    return;
	}
    }

    slaxLogEnable(enable);
    slaxOutput("%s verbose logging", enable ? "Enabling" : "Disabling");
}
    
/*
 * 'quit' command
 */
static void
slaxDebugCmdQuit (DC_ARGS)
{
    static const char prompt[] =
	"The script is running.  Exit anyway? (y or n) ";
    int status = xsltGetDebuggerStatus();

    if (status != 0 && status != XSLT_DEBUG_DONE) {
	char *input = slaxInput(prompt, 0);
	slaxDebugEchoInput(input);

	if (input == NULL
	    	|| !slaxDebugCheckAbbrev("yes", 1, input, strlen(input)))
	    return;
    }

    xsltSetDebuggerStatus(XSLT_DEBUG_QUIT);

    slaxDebugClearListInfo(statep);

    /* Tell the xslt engine to stop */
    xsltStopEngine(statep->ds_ctxt);
}

/* ---------------------------------------------------------------------- */

static slaxDebugCommand_t slaxDebugCmdTable[] = {
    { "append",        2, slaxDebugCmdAppend,
      "append $var += value  Append a value to a variable",
      NULL,
    },

    { "backtrace",     2, slaxDebugCmdWhere,
      "backtrace [full] Show the backtrace of template calls (alias: bt)",
      NULL,
    },

    { "break",	       1, slaxDebugCmdBreak,
      "break [loc] [if expr]  Add a breakpoint, optionally conditional "
      "(also: when expr)",
      NULL,
    },

    { "bt",	       1, slaxDebugCmdWhere, NULL, NULL }, /* Hidden */

    { "callflow",      2, slaxDebugCmdCallFlow,
      "callflow [val]  Enable call flow tracing",
      slaxDebugHelpCallFlow,
    },

    { "clear",	       2, slaxDebugCmdClear,
      "clear watch variable [$var | num]  Clear a watchpoint",
      NULL,
    },

    { "condition",     4, slaxDebugCmdCondition,
      "condition <num> [expr]  Set, change, or clear a breakpoint's "
      "condition",
      NULL,
    },

    { "continue",      1, slaxDebugCmdContinue,
      "continue [loc]  Continue running the script",
      NULL,
    },

    { "delete",	       1, slaxDebugCmdDelete,
      "delete [num]    Delete all (or one) breakpoints",
      NULL,
    },

    { "down",	       1, slaxDebugCmdDown,
      "down [n]        Move the selected frame toward the innermost frame",
      NULL,
    },

    { "dump",	       1, slaxDebugCmdDump,
      "dump            Dump internal information about libxml2 objects",
      slaxDebugHelpDump,
    },
 
    { "finish",	       1, slaxDebugCmdFinish,
      "finish          Finish the current template",
      NULL,
    },

    { "help",	       1, slaxDebugCmdHelp,
      "help            Show this help message",
      NULL,
    },

    { "info",	       1, slaxDebugCmdInfo,
      "info            Showing info about the script being debugged",
      slaxDebugHelpInfo,
    },

    { "?",	       1, slaxDebugCmdHelp, NULL, NULL }, /* Hidden */

    { "list",	       1, slaxDebugCmdList,
      "list [loc]      List contents of the current script",
      NULL,
    },

    { "locals",	       2, slaxDebugCmdLocals,
      "locals          List contents of local variables",
      NULL,
    },

    { "mode",	       1, slaxDebugCmdMode, NULL, NULL }, /* Hidden */

    { "next",	       1, slaxDebugCmdNext,
      "next            Execute the over instruction, stepping over calls",
      NULL,
    },

    { "over",	       1, slaxDebugCmdOver,
      "over            Execute the current instruction hierarchy",
      NULL,
    },

    { "print",	       1, slaxDebugCmdPrint,
      "print <xpath>   Print the value of an XPath expression",
      NULL,
    },

    { "profile",       2, slaxDebugCmdProfiler,
      "profile [val]   Turn profiler on or off",
      slaxDebugHelpProfile,
    },

    { "reload",	       3, slaxDebugCmdReload,
      "reload          Reload the script contents",
      NULL,
    },

    { "run",	       3, slaxDebugCmdRun,
      "run             Restart the script",
      NULL,
    },

    { "set",	       2, slaxDebugCmdSet,
      "set $var = value  Set a variable to a new value",
      NULL,
    },

    { "step",	       1, slaxDebugCmdStep,
      "step            Execute the next instruction, stepping into calls",
      NULL,
    },

    { "tbreak",	       2, slaxDebugCmdTbreak,
      "tbreak [loc] [if expr]  Add a temporary breakpoint, deleted when hit "
      "(also: when expr)",
      NULL,
    },

    { "up",	       1, slaxDebugCmdUp,
      "up [n]          Move the selected frame toward the outermost frame",
      NULL,
    },

    { "verbose",       1, slaxDebugCmdVerbose,
      "verbose         Turn on verbose (-v) output logging",
      slaxDebugHelpVerbose,
    },

    { "watch",	       2, slaxDebugCmdWatch,
      "watch variable $var  Set a watchpoint on a variable",
      NULL,
    },

    { "where",	       1, slaxDebugCmdWhere,
      "where           Show the backtrace of template calls (alias: bt)",
      NULL,
    },

    { "quit",	       1, slaxDebugCmdQuit,
      "quit            Quit debugger",
      NULL,
    },

    { NULL, 0, NULL, NULL, NULL }
};

/**
 * Find a command with the given name
 * @name name of command
 * @return command structure for that command
 */
static slaxDebugCommand_t *
slaxDebugGetCommand (const char *name)
{
    slaxDebugCommand_t *cmdp;
    int len = strlen(name);

    for (cmdp = slaxDebugCmdTable; cmdp->dc_command; cmdp++) {
	if (slaxDebugCheckAbbrev(cmdp->dc_command, cmdp->dc_min, name, len))
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
	slaxOutput("Unknown command \"%s\".  Try \"help\"", argv[0]);
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

    /* If we're in profiler mode, we don't prompt for commands */
    if (slaxDebugDisplayMode == DEBUG_MODE_PROFILER) {
	xsltSetDebuggerStatus(XSLT_DEBUG_CONT);
	statep->ds_flags |= DSF_DISPLAY | DSF_CONTINUE;
	return 0;
    }

    if ((statep->ds_flags & DSF_DISPLAY) && statep->ds_inst != NULL) {
	const char *filename = (const char *) xmlDocGetURL(xmlNodeGetDoc(statep->ds_inst));
	int line_no = xmlGetLineNo(statep->ds_inst);
	slaxDebugOutputScriptLines(statep, filename, line_no, line_no + 1);
	statep->ds_flags &= ~DSF_DISPLAY;
    }

    input = slaxInput(prompt, SIF_HISTORY);
    if (input == NULL)
	return -1;

    statep->ds_count += 1;

    /* Trim newlines */
    cp = input + strlen(input);
    if (cp > input && cp[-1] == '\n')
	*cp = '\0';

    cp = input;
    while (isspace((int) *cp))
	cp++;

    if (*cp == '\0') {
	cp = prev_input;
    } else {
	strlcpy(prev_input, cp, sizeof(prev_input));
    }

    slaxDebugEchoInput(cp);

    statep->ds_flags |= DSF_INSHELL;
    slaxDebugRunCommand(statep, cp);
    statep->ds_flags &= ~DSF_INSHELL;

    if (input)
	xmlFree(input);

    return 0;
}

/*
 * We've got a conditional breakpoint, which means we need to
 * (a) compile the expression, (b) evaluate the expression,
 * (c) turn it into a boolean, and (d) decide whether to skip.
 * We return FALSE to skip, TRUE to stop.
 */
static int
slaxDebugEvalCondition (slaxDebugState_t *statep,
			slaxDebugBreakpoint_t *dbp, int print)
{
    xmlXPathObjectPtr xpobj;
    int res = FALSE;

    xpobj = slaxDebugEvalXpath(statep, dbp->dbp_condition);
    if (xpobj == NULL) {
	/*
	 * The condition failed to evaluate (e.g. a variable that's out
	 * of scope at this call site) rather than evaluating to false.
	 * Without this, that's indistinguishable from a condition that
	 * legitimately evaluated to false, and the only other signal is
	 * libxml2/libxslt's generic error handler printing to stderr --
	 * on every single pass through this breakpoint.  Warn once per
	 * breakpoint (reset if the condition is changed via 'condition')
	 * instead of letting a broken condition on a hot code path spam
	 * the same message every iteration.
	 */
	if (!(dbp->dbp_flags & DBPF_COND_WARNED)) {
	    slaxOutput("warning: breakpoint %d condition failed to "
		       "evaluate: '%s'", dbp->dbp_num, dbp->dbp_condition_user);
	    dbp->dbp_flags |= DBPF_COND_WARNED;
	}
	return FALSE;		/* Failure means don't stop */
    }

    if (print)
	slaxDebugOutputXpath(xpobj, NULL, TRUE); /* Debug output */

    if (xmlXPathObjectGetType(xpobj) != XPATH_BOOLEAN)
	xpobj = xmlXPathConvertBoolean(xpobj);

    if (xmlXPathObjectGetType(xpobj) == XPATH_BOOLEAN) /* Otherwise, it can't be converted */
	res = xmlXPathObjectGetBoolval(xpobj) ? TRUE : FALSE;

    xmlXPathFreeObject(xpobj);

    return res;
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

    if (xmlNodeGetDoc(statep->ds_inst) && xmlNodeGetDoc(statep->ds_inst) == xmlNodeGetDoc(inst)) {
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
 * @inst instruction being executed (never NULL)
 * @node context node (if NULL, we're just starting to evaluate a
 *       global variable) (and called from xsltEvalGlobalVariable)
 * @template  template being executed (if NULL, we're called while
 *        evaluating a global variable)
 * @ctxt transformation context
 */
static void
slaxDebugHandler (xmlNodePtr inst, xmlNodePtr node,
		  xsltTemplatePtr template, xsltTransformContextPtr ctxt)
{
    slaxDebugState_t *statep = slaxDebugGetState();
    slaxDebugStackFrame_t *stp;
    int status;
    char buf[BUFSIZ];

    /* We don't want to be recursive (via the 'print' command) */
    if (statep->ds_flags & DSF_INSHELL)
	return;

    slaxLog("handleFrame: template %p/[%s], node %p/%s/%d, "
	      "inst %p/%s/%ld ctxt %p",
	      template, slaxDebugTemplateInfo(template, buf, sizeof(buf)),
	      node, NAME(node), node ? xmlNodeGetType(node) : 0,
	      inst, NAME(inst), inst ? xmlGetLineNo(inst) : 0, ctxt);

    /*
     * We do not debug text nodes
     */
    if (inst && xmlNodeGetType(inst) == XML_TEXT_NODE)
	return;

    /*
     * When we ask to quit, libxslt might keep going for a while
     */
    status = xsltGetDebuggerStatus();
    if (status == XSLT_DEBUG_DONE || status == XSLT_DEBUG_QUIT)
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

    if (statep->ds_flags & DSF_PROFILER)
	slaxProfExit();

    /*
     * Fill in the current state
     */
    statep->ds_inst = inst;
    statep->ds_node = node;
    statep->ds_template = template;
    statep->ds_ctxt = ctxt;
    statep->ds_frame_selected = 0;	/* Snap back to the live frame */

    /*
     * The addFrame callback doesn't get passed the context pointer
     * and we need it to properly record the "varsBase" field, which
     * tells us where local variables start.  Without that, we have
     * to record it on the next handler call.
     */
    if (ctxt && (statep->ds_flags & DSF_FRESHADD)) {
	statep->ds_flags &= ~DSF_FRESHADD;
	stp = TAILQ_LAST(&slaxDebugStack, slaxDebugStack_s);
	if (stp) {
	    stp->st_ctxt = ctxt;
	    stp->st_locals_start = xsltTransformContextGetVarsNr(ctxt);

	    stp = TAILQ_PREV(stp, slaxDebugStack_s, st_link);
	    if (stp) {
		stp->st_ctxt = ctxt;
		stp->st_locals_start = xsltTransformContextGetVarsBase(ctxt);
		stp->st_locals_stop = xsltTransformContextGetVarsNr(ctxt);

		/*
		 * Record the last local index for the previous stack frame.
		 */
		stp = TAILQ_PREV(stp, slaxDebugStack_s, st_link);
		if (stp && stp->st_ctxt == ctxt)
		    stp->st_locals_stop
			= xsltTransformContextGetVarsBase(ctxt);
	    }
	}
    }

    slaxDebugBreakpoint_t *dbp = slaxDebugFindBreakpoint(statep, inst);
    if (dbp && dbp->dbp_condition) {
	if (slaxDebugEvalCondition(statep, dbp, FALSE) == FALSE) {
	    /* Fake a "continue" command */
	    slaxDebugCmdContinue(statep, NULL, NULL);
	    return;
	}
    }

    slaxDebugAnnounceBreakpoint(statep, inst);

    if (dbp && (dbp->dbp_flags & DBPF_TEMPORARY)) {
	slaxOutput("Deleted temporary breakpoint %d", dbp->dbp_num);
	slaxDebugFreeBreakpoint(dbp);
    }

#if 0
    if (statep->ds_flags & DSF_OVER) {
	/*
	 * If we got here with the over flag still on, then there was
	 * no addFrame/dropFrame, so we must have "over"ed a non-call.
	 * Turn off the flag and get out of over mode.
	 */
	statep->ds_flags &= ~DSF_OVER;
	xsltSetDebuggerStatus(XSLT_DEBUG_INIT);
    }
#endif

    statep->ds_flags &= ~DSF_CONTINUE;

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

	case XSLT_DEBUG_QUIT:
	    return;

	case XSLT_DEBUG_STEP:
	    xsltSetDebuggerStatus(XSLT_DEBUG_INIT);
	    /* Fallthru */

	case XSLT_DEBUG_OVER:
	case XSLT_DEBUG_NEXT:
	case XSLT_DEBUG_CONT:
	case XSLT_DEBUG_NONE:
	    if (statep->ds_flags & DSF_PROFILER)
		slaxProfEnter(inst);

	    return;
	}
    }
}

/**
 * Called as callback function from libxslt when a fatal runtime error
 * is about to abort the transform (e.g. a variable's expression fails
 * to evaluate, or an expression anywhere references an undeclared
 * variable).  Unlike slaxDebugHandler(), this is called unconditionally
 * -- regardless of the current debugger status or any breakpoint --
 * so we always stop here and let the user inspect the still-live
 * frame (e.g. "print" or "info locals") before the transform unwinds
 * and that context is torn down.
 *
 * A single logical failure can end up reporting itself more than once
 * as it propagates (e.g. an undefined-variable lookup failure is
 * reported both at the point of the lookup and then again by the
 * instruction whose expression evaluation came back empty); since
 * ctxt->state never resets back to XSLT_STATE_OK once a fatal error
 * has set it, and the transform unwinds quickly once that happens, we
 * only need to stop for the *first* such report per run -- so we
 * latch DSF_ERRORSTOP here and skip re-entering the shell on any
 * later call until the next "run" clears it.
 *
 * @cur instruction where the error occurred
 * @ctxt transformation context
 * @message a short, specific description of the failure
 * @note an optional, more specific detail about the failure (e.g.
 *       the underlying XPath error), or NULL if none is available
 *
 * Returns TRUE if we actually reported the error (via
 * xsltReportFatalError()), FALSE if we declined (e.g. we're
 * mid-eval on our own behalf, or already shutting down) -- our
 * caller (xslHandleDebuggerError(), and transitively the libxslt
 * code that calls it) uses this to decide whether its own, less
 * informative error message is still needed as the only report this
 * failure will get.
 */
static int
slaxDebugErrorHandler (xmlNodePtr cur, xsltTransformContextPtr ctxt,
			const char *message, const char *note)
{
    slaxDebugState_t *statep = slaxDebugGetState();
    int status;

    /*
     * Only the first report of a given failure should stop us; every
     * later call for that same failure (including one arriving after
     * the user has already quit, e.g. from a caller we don't put in
     * DSF_INEVAL) is just noise we've already reported once, so say
     * so before any of the checks below.
     */
    if (statep->ds_flags & DSF_ERRORSTOP)
	return TRUE;

    /* We don't want to be recursive (via the 'print' command) */
    if (statep->ds_flags & DSF_INSHELL)
	return FALSE;

    /*
     * A failure evaluating an expression on the debugger's own
     * behalf (print/set/breakpoint condition) is not a script
     * error -- slaxDebugCheckCondition() and friends already report
     * and recover from these themselves.
     */
    if (statep->ds_flags & DSF_INEVAL)
	return FALSE;

    status = xsltGetDebuggerStatus();

    /* If we're already shutting down, there's nothing useful to do */
    if (status == XSLT_DEBUG_DONE || status == XSLT_DEBUG_QUIT)
	return FALSE;

    statep->ds_flags |= DSF_ERRORSTOP;

    if (statep->ds_flags & DSF_PROFILER)
	slaxProfExit();

    statep->ds_inst = cur;
    statep->ds_ctxt = ctxt;

    xsltReportFatalError(cur, message, note, statep->ds_script_buffer);

    /*
     * slaxDebugShell() only processes a single command per call; a
     * command like "print" or "info locals" leaves the status at
     * XSLT_DEBUG_INIT so we loop back for another command, while a
     * stepping/continue/quit command changes the status, telling us
     * it's time to stop looping and return (letting the fatal error
     * unwind the transform, or honoring an explicit "quit").
     */
    for (;;) {
	if (slaxDebugShell(statep) < 0) {
	    xsltSetDebuggerStatus(XSLT_DEBUG_QUIT);
	    return TRUE;
	}

	if (xsltGetDebuggerStatus() != XSLT_DEBUG_INIT)
	    return TRUE;
    }
}

/*
 * Make a standalone, deep copy of the sibling chain starting at
 * "first", owned by a throwaway document, so a watchpoint can keep
 * comparing against it after the live value (an RTF that may be
 * replaced or freed by the next assignment) has moved on
 */
static xmlDocPtr
slaxDebugSnapshotNodes (xmlNodePtr first)
{
    xmlDocPtr docp = xmlNewDoc(NULL);
    xmlNodePtr np, copy;

    if (docp == NULL)
	return NULL;

    for (np = first; np != NULL; np = xmlNodeGetNext(np)) {
	copy = xmlCopyNode(np, 1);
	if (copy != NULL)
	    xmlAddChild((xmlNodePtr) docp, copy);
    }

    return docp;
}

/**
 * Called whenever a variable is assigned a new value, from both
 * slaxMvarElement() (an <xsl:variable>/<xsl:param> executing
 * normally) and slaxDebugCmdSetCommon() (an interactive
 * "set"/"append").  If "name" matches a watched variable and its
 * value actually changed, force an interactive stop, the same way a
 * breakpoint hit does.
 *
 * Unlike slaxDebugHandler() and slaxDebugErrorHandler(), this can be
 * called while DSF_INSHELL is set (an interactive "set" runs with it
 * set the entire time), so we can't use that flag to guard against
 * recursion the way they do.  That's fine here: a nested stop (e.g.
 * typing "set $y = 2" while already stopped at a watchpoint on $y)
 * is just another level of C-stack recursion, and each level's own
 * loop below independently exits once the status leaves
 * XSLT_DEBUG_INIT, so a "continue" at the innermost stop unwinds
 * cleanly back out through all of them.
 *
 * @name bare name (no leading "$") of the variable just assigned
 */
void
slaxDebugCheckWatchpoints (const char *name)
{
    slaxDebugState_t *statep = slaxDebugGetState();
    slaxDebugWatch_t *dwp;
    int status;

    if (TAILQ_EMPTY(&slaxDebugWatches))
	return;

    status = xsltGetDebuggerStatus();
    if (status == XSLT_DEBUG_DONE || status == XSLT_DEBUG_QUIT)
	return;

    TAILQ_FOREACH(dwp, &slaxDebugWatches, dwp_link) {
	if (!streq(dwp->dwp_name, name))
	    continue;

	xmlXPathObjectPtr xpath = slaxDebugEvalXpath(statep, dwp->dwp_condition);
	if (xpath == NULL)
	    continue;	/* Out of scope or otherwise unevaluable; skip quietly */

	int is_nodeset = (xmlXPathObjectGetType(xpath) == XPATH_NODESET);
	xmlNodeSetPtr nset = is_nodeset
	    ? xmlXPathObjectGetNodesetval(xpath) : NULL;
	xmlNodePtr new_nodes = (nset != NULL && xmlNodeSetGetNodeNr(nset) > 0)
	    ? xmlNodeSetGetNodeEntry(nset, 0) : NULL;

	char *value = (char *) xmlXPathCastToString(xpath);
	xmlXPathFreeObject(xpath);

	/*
	 * xpath is gone, but new_nodes still points into the live
	 * document -- freeing the xmlXPathObjectPtr only frees the
	 * node-set wrapper, not the nodes it referenced -- so reading
	 * or deep-copying it below is still safe.
	 */

	if (!dwp->dwp_have_value) {
	    /* First evaluation just records a baseline; no stop */
	    dwp->dwp_value = value;
	    dwp->dwp_nodes = is_nodeset
		? slaxDebugSnapshotNodes(new_nodes) : NULL;
	    dwp->dwp_have_value = TRUE;
	    continue;
	}

	int changed = is_nodeset
	    ? !slaxNodeListEqual(dwp->dwp_nodes
				 ? xmlDocGetChildren(dwp->dwp_nodes) : NULL,
				 new_nodes)
	    : !streq(dwp->dwp_value, value);

	if (!changed) {
	    xmlFreeAndEasy(value);
	    continue;
	}

	slaxOutput("Watchpoint %d: %s", dwp->dwp_num, dwp->dwp_where);
	slaxOutput("Old value: %s", dwp->dwp_value);
	slaxOutput("New value: %s", value);

	xmlFreeAndEasy(dwp->dwp_value);
	dwp->dwp_value = value;

	if (dwp->dwp_nodes != NULL)
	    xmlFreeDoc(dwp->dwp_nodes);
	dwp->dwp_nodes = is_nodeset ? slaxDebugSnapshotNodes(new_nodes) : NULL;

	if (statep->ds_flags & DSF_PROFILER)
	    slaxProfExit();

	for (;;) {
	    if (slaxDebugShell(statep) < 0) {
		xsltSetDebuggerStatus(XSLT_DEBUG_QUIT);
		break;
	    }

	    if (xsltGetDebuggerStatus() != XSLT_DEBUG_INIT)
		break;
	}

	if (statep->ds_flags & DSF_PROFILER)
	    slaxProfEnter(statep->ds_inst);
    }
}

/*
 * Build a ';'-joined folded-stack path string for the live call
 * stack, from the outermost frame down through (and including)
 * "stp" -- the format flamegraph.pl and speedscope.app both expect
 * as input.  Caller must xmlFree() the result.
 */
static char *
slaxDebugFlamegraphPath (slaxDebugStackFrame_t *stp)
{
    slaxDebugStackFrame_t *fp;
    char *path = NULL;
    size_t len = 0;
    char buf[BUFSIZ];

    for (fp = TAILQ_FIRST(&slaxDebugStack); fp; fp = TAILQ_NEXT(fp, st_link)) {
	const char *name = slaxDebugTemplateInfo(fp->st_template, buf,
						  sizeof(buf));
	size_t namelen = strlen(name);
	char *newpath = xmlMalloc(len + namelen + 2); /* ';' and NUL */

	if (newpath == NULL) {
	    if (path)
		xmlFree(path);
	    return NULL;
	}

	if (path) {
	    memcpy(newpath, path, len);
	    newpath[len] = ';';
	    memcpy(newpath + len + 1, name, namelen);
	    newpath[len + 1 + namelen] = '\0';
	    xmlFree(path);
	    len = len + 1 + namelen;
	} else {
	    memcpy(newpath, name, namelen);
	    newpath[namelen] = '\0';
	    len = namelen;
	}

	path = newpath;

	if (fp == stp)
	    break;
    }

    return path;
}

/*
 * xmlHashFree() deallocator for the malloc'd psu_time_usecs_t
 * accumulators we store as values in ds_flamegraph_hash.
 */
static void
slaxDebugFlamegraphFreeEntry (void *payload, const xmlChar *name UNUSED)
{
    xmlFree(payload);
}

/*
 * Add "self" microseconds to the accumulated self time for the full
 * call path ending at "stp", creating the hash table and/or this
 * path's entry on first use.
 */
static void
slaxDebugFlamegraphAccum (slaxDebugState_t *statep, slaxDebugStackFrame_t *stp,
			   psu_time_usecs_t self)
{
    char *path = slaxDebugFlamegraphPath(stp);
    psu_time_usecs_t *valp;

    if (path == NULL)
	return;

    if (statep->ds_flamegraph_hash == NULL)
	statep->ds_flamegraph_hash = xmlHashCreate(0);

    valp = xmlHashLookup(statep->ds_flamegraph_hash, (const xmlChar *) path);
    if (valp == NULL) {
	valp = xmlMalloc(sizeof(*valp));
	*valp = self;
	xmlHashAddEntry(statep->ds_flamegraph_hash, (const xmlChar *) path,
			valp);
    } else {
	*valp += self;
    }

    xmlFree(path);
}

/**
 * Called from libxslt as callback function when template is executed.
 *
 * @template  template being executed (if NULL, we're called while
 *        evaluating a global variable)
 * @inst instruction node
 */
static int
slaxDebugAddFrame (xsltTemplatePtr template, xmlNodePtr inst)
{
    slaxDebugState_t *statep = slaxDebugGetState();
    slaxDebugStackFrame_t *stp;
    char buf[BUFSIZ];

    /* We don't want to be recursive (via the 'print' command) */
    if (statep->ds_flags & DSF_INSHELL)
	return 0;

    if (statep->ds_flags & DSF_PROFILER)
	slaxProfExit();

    slaxLog("addFrame: template %p/[%s], inst %p/%s/%ld (inst %p/%s)",
	      template, slaxDebugTemplateInfo(template, buf, sizeof(buf)),
	      inst, NAME(inst), inst ? xmlGetLineNo(inst) : 0,
	      statep->ds_inst, NAME(statep->ds_inst));

    /*
     * This should never happen, except when it does.
     * Seems to be when the engine can't find the instruction, like
     * an unknown function or template.  Ignore it instead of making
     * a core file.
     */
    if (inst == NULL)
	return 0;

    /*
     * They are two distinct calls for addFrame when a template is
     * invoked.  The sequence goes like this:
     *
     * - handler(call-template)
     * - addFrame(template)
     * - handler(template)
     * - addFrame(template)
     *
     * Looks like both xsltApplyXSLTTemplate() and
     * xsltApplySequenceConstructor() have calls to the debugger.  We
     * pick the outer addFrame to record so we want to skip the inner
     * set.  Our clue is that for the inner set, the template
     * instruction is the same one we recorded in Handler.
     */
    if (inst == statep->ds_inst
	&& (streq((const char *) xmlNodeGetName(inst), ELT_CALL_TEMPLATE)
	    || streq((const char *) xmlNodeGetName(inst), ELT_TEMPLATE)))
	return 0;

    if (statep->ds_flags & DSF_CALLFLOW)
	slaxDebugCallFlow(statep, template, inst, "enter");

    /*
     * Store the template backtrace in linked list
     */
    stp = xmlMalloc(sizeof(*stp));
    if (!stp) {
	slaxOutput("memory allocation failure");
	return 0;
    }

    bzero(stp, sizeof(*stp));
    stp->st_depth = statep->ds_stackdepth++;
    stp->st_template = template;
    stp->st_inst = inst;
    stp->st_caller = statep->ds_inst;

    if (statep->ds_flags & DSF_PROFILE_FLAMEGRAPH)
	stp->st_enter_time = psu_get_time();

    if (xmlNodeGetNs(inst) && xmlNsGetHref(xmlNodeGetNs(inst)) && xmlNodeGetName(inst)
	&& streq((const char *) xmlNsGetHref(xmlNodeGetNs(inst)), XSL_URI)) {
	if (streq((const char *) xmlNodeGetName(inst), ELT_WITH_PARAM))
	    stp->st_flags |= STF_PARAM;
    }

    if (!(stp->st_flags & STF_PARAM)) {
	/* If we're 'over'ing, mark this frame as "stop when pop" */
	if (statep->ds_flags & DSF_OVER) {
	    statep->ds_flags &= ~DSF_OVER;
	    stp->st_flags |= STF_STOPWHENPOP;
	    xsltSetDebuggerStatus(XSLT_DEBUG_CONT);
	}
    }

    TAILQ_INSERT_TAIL(&slaxDebugStack, stp, st_link);

    statep->ds_flags |= DSF_FRESHADD;

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
    slaxDebugStackFrame_t *stp;
    xsltTemplatePtr template;
    char buf[BUFSIZ];
    xmlNodePtr inst;

    /* We don't want to be recursive (via the 'print' command) */
    if (statep->ds_flags & DSF_INSHELL)
	return;

    if (statep->ds_flags & DSF_PROFILER)
	slaxProfExit();

    stp = TAILQ_LAST(&slaxDebugStack, slaxDebugStack_s);
    if (stp == NULL) {
	slaxLog("dropFrame: null");
	return;
    }

    template = stp->st_template;
    inst = stp->st_inst;

    slaxLog("dropFrame: %s (%p), inst <%s%s%s> (%p; line %ld%s)",
	      slaxDebugTemplateInfo(template, buf, sizeof(buf)),
	      template, 
	      (inst && xmlNodeGetNs(inst) && xmlNsGetPrefix(xmlNodeGetNs(inst)))
	    		? xmlNsGetPrefix(xmlNodeGetNs(inst)) : slaxNull,
	      (inst && xmlNodeGetNs(inst) && xmlNsGetPrefix(xmlNodeGetNs(inst))) ? ":" : "",
	      NAME(stp->st_inst), inst,
	      stp->st_inst ? xmlGetLineNo(stp->st_inst) : 0,
	      (stp->st_flags & STF_STOPWHENPOP) ? " stopwhenpop" : "");

    if (stp->st_flags & STF_STOPWHENPOP)
	xsltSetDebuggerStatus(XSLT_DEBUG_INIT);

    if (statep->ds_flags & DSF_PROFILE_FLAMEGRAPH) {
	psu_time_usecs_t now = psu_get_time();
	psu_time_usecs_t elapsed = now - stp->st_enter_time;
	psu_time_usecs_t self = (elapsed > stp->st_child_time)
	    ? elapsed - stp->st_child_time : 0;
	slaxDebugStackFrame_t *parent;

	slaxDebugFlamegraphAccum(statep, stp, self);

	parent = TAILQ_PREV(stp, slaxDebugStack_s, st_link);
	if (parent)
	    parent->st_child_time += elapsed;
    }

    /* 'Pop' the stack frame */
    TAILQ_REMOVE(&slaxDebugStack, stp, st_link);
    statep->ds_stackdepth -= 1;	/* Reduce depth of stack */

    if (statep->ds_flags & DSF_CALLFLOW)
	slaxDebugCallFlow(statep, template, inst, "exit");

    /*
     * If we're popping stack frames, then we're not on the same
     * instruction.  Clear the last instruction pointer.
     */
    statep->ds_last_inst = NULL;

    xmlFree(stp);
}

/*
 * Register debugger
 */
int
slaxDebugInitFlags (slaxDebugFlags_t flags)
{
    static int done_register;
    slaxDebugState_t *statep = slaxDebugGetState();

    slaxDebugFlags = flags;

    if (done_register)
	return FALSE;
    done_register = TRUE;

    TAILQ_INIT(&slaxDebugRestartList);
    TAILQ_INIT(&slaxDebugBreakpoints);
    TAILQ_INIT(&slaxDebugWatches);
    TAILQ_INIT(&slaxDebugStack);

    /* Start with the current line */
    statep->ds_flags |= DSF_DISPLAY;

    xsltSetDebuggerStatus(XSLT_DEBUG_INIT);
    xsltSetDebuggerCallbacksHelper(slaxDebugHandler, slaxDebugAddFrame,
				   slaxDebugDropFrame, slaxDebugErrorHandler);

    if (flags & SDBF_PROFILE_ONLY) {
	slaxDebugDisplayMode = DEBUG_MODE_PROFILER;

    } else {
	slaxDebugDisplayMode = DEBUG_MODE_CLI;
	if (!(slaxDebugFlags & SDBF_ECHO_INPUT))
	    slaxOutput("sdb: The SLAX Debugger (version %s)", LIBSLAX_VERSION);
	slaxOutput("Type 'help' for help");
    }

    return FALSE;
}

int
slaxDebugInit (void)
{
    return slaxDebugInitFlags(0);
}

/**
 * Set the top-most stylesheet
 *
 * @stylep the stylesheet aka script
 */
void
slaxDebugSetStylesheet (xsltStylesheetPtr script)
{
    slaxDebugState_t *statep = slaxDebugGetState();

    if (statep) {
	statep->ds_script = script;
	statep->ds_inst = NULL;
	statep->ds_template = NULL;
	statep->ds_node = NULL;
	statep->ds_last_inst = NULL;
	statep->ds_stop_at = NULL;
    }
}

void
slaxDebugSetScriptBuffer (const char *buffer)
{
    slaxDebugState_t *statep = slaxDebugGetState();

    if (statep)
	statep->ds_script_buffer = strdup(buffer);
}

void
slaxDebugSetProfileFile (const char *filename)
{
    slaxDebugState_t *statep = slaxDebugGetState();

    if (statep == NULL)
	return;

    if (statep->ds_profile_file) {
	free(statep->ds_profile_file);
	statep->ds_profile_file = NULL;
    }

    if (filename)
	statep->ds_profile_file = strdup(filename);
}

/**
 * Set a search path for included and imported files
 *
 * @includes array of search paths
 */
void
slaxDebugSetIncludes (const char **includes)
{
    slaxDebugIncludes = includes;
}

static xsltStylesheetPtr
slaxDebugReload (const char *scriptname)
{
    FILE *fp;
    xmlDocPtr docp;
    xsltStylesheetPtr newp = NULL;

    fp = fopen(scriptname, "r");
    if (fp == NULL) {
	slaxOutput("could not open file '%s': %s",
		   scriptname, strerror(errno));
    } else {
	docp = slaxLoadFile(scriptname, fp, NULL, 0);
	fclose(fp);
	if (docp == NULL) {
	    slaxOutput("could not parse file '%s'", scriptname);
	} else {
	    newp = xsltParseStylesheetDoc(docp);
	    if (newp && xsltStylesheetGetErrors(newp) == 0)
		return newp;

	    slaxOutput("%d errors parsing script: '%s'",
		       newp ? xsltStylesheetGetErrors(newp) : 1, scriptname);
	    if (newp) {
		xsltFreeStylesheet(newp);
		newp = NULL;
	    } else if (docp) {
		xmlFreeDoc(docp);
	    }
	}
    }

    slaxOutput("Reload failed");
    return NULL;
}

/**
 * Apply a stylesheet to an input document, returning the results.
 *
 * @style stylesheet aka script
 * @doc input document
 * @params set of parameters
 * @returns output document
 */
xmlDocPtr
slaxDebugApplyStylesheet (const char *scriptname, xsltStylesheetPtr style,
			  const char *docname UNUSED, xmlDocPtr doc,
			  const char **params)
{
    slaxDebugState_t *statep = slaxDebugGetState();
    xmlDocPtr res = NULL;
    int status;
    xsltStylesheetPtr new_style, save_style = NULL;
    int indent = xsltStylesheetGetIndent(style); /* Save indent value */

    slaxDebugSetStylesheet(style);
    slaxProfOpen(xsltStylesheetGetDoc(style));
    statep->ds_flags |= DSF_PROFILER;

    /* A "--profile-mode callgraph" CLI run arms this before any "run" */
    if (slaxDebugFlags & SDBF_PROFILE_CALLGRAPH) {
	statep->ds_flags &= ~DSF_PROFILER;
	statep->ds_flags |= DSF_PROFILE_CALLGRAPH;
    }

    /* A "--profile-mode flamegraph" CLI run arms this before any "run" */
    if (slaxDebugFlags & SDBF_PROFILE_FLAMEGRAPH) {
	statep->ds_flags &= ~DSF_PROFILER;
	statep->ds_flags |= DSF_PROFILE_FLAMEGRAPH;
    }

    xsltSetDebuggerStatus(0);

    /* Start up in profiler-only mode */
    if (slaxDebugDisplayMode == DEBUG_MODE_PROFILER) {
	slaxDebugClearListInfo(statep);

	/* Tell the xslt engine to stop */
	xsltStopEngine(statep->ds_ctxt);

	xsltSetDebuggerStatus(XSLT_DEBUG_QUIT);
	statep->ds_flags |= DSF_RESTART | DSF_DISPLAY | DSF_CONTINUE;
	if (!(statep->ds_flags & (DSF_PROFILE_CALLGRAPH | DSF_PROFILE_FLAMEGRAPH)))
	    statep->ds_flags |= DSF_PROFILER;
    }

    int working = TRUE; 

    while (working) {
	if (slaxDebugShell(statep) < 0)
	    break;

	/*
	 * Lots of flag interactions here, based on how we got
	 * to this spot.
	 */
	if (statep->ds_flags & DSF_RESTART) {
	restart:
	    statep->ds_flags &= ~DSF_RESTART;
	    statep->ds_flags |= DSF_DISPLAY;

	    if (statep->ds_flags & DSF_CONTINUE) {
		xsltSetDebuggerStatus(XSLT_DEBUG_CONT);
		statep->ds_flags &= ~DSF_CONTINUE;
	    } else 
		xsltSetDebuggerStatus(XSLT_DEBUG_INIT);

	    slaxProfClear();
	    slaxRestartListCall();

	    if (statep->ds_callgraph_buffer) {
		free(statep->ds_callgraph_buffer);
		statep->ds_callgraph_buffer = NULL;
	    }

	    if (statep->ds_flamegraph_hash) {
		xmlHashFree(statep->ds_flamegraph_hash,
			    slaxDebugFlamegraphFreeEntry);
		statep->ds_flamegraph_hash = NULL;
	    }

	} else if (statep->ds_flags & DSF_RELOAD) {
	reload:
	    statep->ds_flags &= ~DSF_RELOAD;
	    xsltSetDebuggerStatus(0);

	    if (statep->ds_script_buffer) {
		new_style = style;
		goto reload_skip;
	    }

	    if (save_style) {
		xsltFreeStylesheet(save_style);
		save_style = NULL;
	    }

	    slaxRestartListCall();

	    new_style = slaxDebugReload(scriptname);
	    if (new_style) {
		save_style = new_style;

	    reload_skip:
		/* Out with the old */
		slaxDebugClearStacktrace();
		slaxProfClear();
		slaxProfClose();

		/* In with the new */
		style = new_style;
		xsltStylesheetSetIndent(style, indent); /* Restore indent value */
		slaxDebugSetStylesheet(style);
		slaxDebugReloadBreakpoints(statep);

		slaxProfOpen(xsltStylesheetGetDoc(style));

		statep->ds_flags &= ~(DSF_RESTART & DSF_DISPLAY);
		xsltSetDebuggerStatus(0);

		slaxOutput("Reloading complete");

		if (res) {
		    xmlFreeDoc(res);
		    res = NULL;
		}
	    }

	    continue;

	} else {
	    status = xsltGetDebuggerStatus();
	    if (status == XSLT_DEBUG_QUIT)
		break;
	    
	    continue;	/* Until the user says "run", we go nothing */
	}

	if (res)		/* Free doc from last run */
	    xmlFreeDoc(res);

	/* Each run gets its own chance to stop on a fatal error */
	statep->ds_flags &= ~DSF_ERRORSTOP;

	if (statep->ds_flags & DSF_PROFILE_CALLGRAPH) {
	    char *buf = NULL;
	    size_t buflen = 0;
	    FILE *memfp = open_memstream(&buf, &buflen);

	    res = xsltApplyStylesheetUser(style, doc, params, NULL, memfp,
					  NULL);
	    fclose(memfp);

	    if (statep->ds_callgraph_buffer)
		free(statep->ds_callgraph_buffer);
	    statep->ds_callgraph_buffer = buf;

	} else {
	    res = xsltApplyStylesheet(style, doc, params);
	}

	status = xsltGetDebuggerStatus();
	if (status == XSLT_DEBUG_QUIT) {
	    if (res)
		xmlFreeDoc(res);
	    res = NULL;

	    if (statep->ds_flags & DSF_RELOAD) {
		slaxOutput("Reloading script...");
		goto reload;

	    } else if (statep->ds_flags & DSF_RESTART) {
		slaxOutput("Restarting script");
		slaxProfClear();
		slaxDebugClearStacktrace();
		goto restart;

	    } else {
		/* Quit without restart == exit */
		break;
	    }
	}

	/*
	 * We fell out the bottom of the script.  Show the output,
	 * cleanup, and loop in the shell until something
	 * interesting happens.
	 */
	if (res && slaxDebugDisplayMode != DEBUG_MODE_PROFILER)
	    xsltSaveResultToFile(stdout, res, style);

	/*
	 * If we're in profile-only mode, then we can clean up, make
	 * the report and then stop the loop.
	 */
	if (slaxDebugDisplayMode == DEBUG_MODE_PROFILER) {
	    if (statep->ds_flags & DSF_PROFILE_CALLGRAPH)
		slaxProfReportCallgraph(statep->ds_callgraph_buffer,
					statep->ds_profile_file);
	    else if (statep->ds_flags & DSF_PROFILE_FLAMEGRAPH)
		slaxProfReportFlamegraph(statep->ds_flamegraph_hash,
					 statep->ds_profile_file);
	    else
		slaxProfReport(slaxDebugFlags, statep->ds_script_buffer);
	    working = FALSE;
	}

	/* Clean up state pointers (all free'd by now) */
	statep->ds_ctxt = NULL;
	statep->ds_inst = NULL;
	statep->ds_node = NULL;
	statep->ds_template = NULL;
	statep->ds_last_inst = NULL;
	statep->ds_stop_at = NULL;

	slaxOutput("Script exited normally");

	statep->ds_flags &= ~(DSF_RESTART & DSF_DISPLAY);
	xsltSetDebuggerStatus(XSLT_DEBUG_DONE);
    }

    /* Free our resources */
    slaxDebugClearBreakpoints();
    slaxDebugClearWatches();
    slaxDebugClearStacktrace();
    slaxProfClose();

    if (statep->ds_callgraph_buffer) {
	free(statep->ds_callgraph_buffer);
	statep->ds_callgraph_buffer = NULL;
    }

    if (statep->ds_flamegraph_hash) {
	xmlHashFree(statep->ds_flamegraph_hash, slaxDebugFlamegraphFreeEntry);
	statep->ds_flamegraph_hash = NULL;
    }

    if (statep->ds_profile_file) {
	free(statep->ds_profile_file);
	statep->ds_profile_file = NULL;
    }

    if (save_style)
	xsltFreeStylesheet(save_style);

    return res;
}

#else /* WITH_SLAX_DEBUGGER */

/*
 * This build was configured without the slax/sdb source-level debugger
 * (--without-slax-debugger), so none of the real code above was built.
 * Without these stubs, sdb would either fail to link or crash the first
 * time it tried to call into a debugger that was never built.  Fail
 * cleanly instead.
 */

int
slaxDebugInitFlags (slaxDebugFlags_t flags UNUSED)
{
    slaxError("sdb: debugger support was not compiled into this build "
	      "(rebuild with --with-bxslt-components=\"default +slax-debugger\")");
    return TRUE;
}

int
slaxDebugInit (void)
{
    return slaxDebugInitFlags(0);
}

void
slaxDebugSetStylesheet (xsltStylesheetPtr script UNUSED)
{
    /* nothing to do; debugger support isn't compiled in */
}

void
slaxDebugSetScriptBuffer (const char *buffer UNUSED)
{
    /* nothing to do; debugger support isn't compiled in */
}

void
slaxDebugSetProfileFile (const char *filename UNUSED)
{
    /* nothing to do; debugger support isn't compiled in */
}

void
slaxDebugSetIncludes (const char **includes UNUSED)
{
    /* nothing to do; debugger support isn't compiled in */
}

xmlDocPtr
slaxDebugApplyStylesheet (const char *scriptname UNUSED,
			  xsltStylesheetPtr style UNUSED,
			  const char *docname UNUSED, xmlDocPtr doc UNUSED,
			  const char **params UNUSED)
{
    return NULL;
}

#endif /* WITH_SLAX_DEBUGGER */
