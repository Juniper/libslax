/*
 * $Id: debugger.c 384736 2010-06-16 04:41:59Z deo $
 *
 * Copyright (c) 2010, Juniper Networks, Inc.
 * All rights reserved.
 *
 * Debugger for scripts
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/queue.h>

#include <libxslt/xsltutils.h>
#include <libxml/xmlsave.h>
#include <libxslt/variables.h>

#include "slaxinternals.h"
#include <libslax/slax.h>

#define MAXARGS	256
#define TMPFILE_TEMPLATE    "/tmp/tmp_slax_XXXXXX"
#define NUM_ARRAY(x)    (sizeof(x)/sizeof(x[0]))

/*
 * Information about the current point of debugging
 */
typedef struct slaxDebugState_s {
    xsltStylesheetPtr ds_stylesheet; /* Current top-level stylesheet */
    xmlNodePtr ds_inst;	/* Current libxslt node being executed */
    xmlNodePtr ds_node;	/* Current context node */
    xsltTemplatePtr ds_template; /* Current template being executed */
    xsltTransformContextPtr ds_ctxt; /* transformation context */
} slaxDebugState_t;

slaxDebugState_t slaxDebugState;
const char **slaxDebugIncludes;

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
    xsltTemplatePtr dsf_template;
    xmlNodePtr dsf_inst;
} slaxDebugStackFrame_t;

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

/* Callback functions for input and output */
static slaxDebugInputCallback_t slaxDebugInputCallback;
static slaxDebugOutputCallback_t slaxDebugOutputCallback;

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

	slaxDebugOutputCallback(buf);
    }
}

/**
 * Use the input callback to get data
 * @prompt the prompt to be displayed
 */
static char *
slaxDebugInput (const char *prompt)
{
    return slaxDebugInputCallback ? slaxDebugInputCallback(prompt) : NULL;
}

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
slaxDebugGetNode (slaxDebugState_t *statep, xmlNodePtr doc UNUSED,
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
    char line[BUFSIZ];
    char tmp_file[] = TMPFILE_TEMPLATE;
    xmlSaveCtxtPtr saveptr;
    int fd, i;
    FILE *fp;

    fd = mkstemp(tmp_file);
    if (fd == -1)
	return;

    saveptr = xmlSaveToFd(fd, NULL, XML_SAVE_FORMAT);
    for (i = 0; i < nodeset->nodeNr; i++) {
	xmlSaveTree(saveptr, nodeset->nodeTab[i]);
	xmlSaveFlush(saveptr);
    }
    xmlSaveClose(saveptr);
    close(fd);

    fp = fopen(tmp_file, "r");
    if (!fp)
	return;

    while (fgets(line, sizeof(line), fp)) {
	/*
	 * Skip the line start with '<?xml'
	 */
	if (line[0] == '<' && line[1] == '?')
	    continue;

	slaxDebugOutput("%s", line);
    }

    fclose(fp);
    unlink(tmp_file);                          
}

/*
 * Print the given XPath object
 */
static int
slaxDebugPrintXpath (const char *var, const char *tag, xmlXPathObjectPtr xpath)
{
    if (!xpath)
	return TRUE;

    switch (xpath->type) {
	case XPATH_BOOLEAN:
	    slaxDebugOutput("(%s) %s => %s (Boolean)", tag, var,
		       xpath->boolval ? "True" : "False");
	    break;

	case XPATH_NUMBER:
	    slaxDebugOutput("(%s) %s => %lf (Number)", tag, var, xpath->floatval);
	    break;

	case XPATH_STRING:
	    if (xpath->stringval)
		slaxDebugOutput("(%s) %s => %s (String)",  tag, var, 
			   xpath->stringval);
	    break;

	case XPATH_NODESET:
	    if (xpath->nodesetval) {
		slaxDebugOutput("(%s) %s => (Nodeset)", tag, var);
		slaxDebugPrintNodeset(xpath->nodesetval);
	    }
	    break;
	case XPATH_XSLT_TREE:
	    if (xpath->nodesetval) {
		slaxDebugOutput("(%s) %s => (RTF)", tag, var);
		slaxDebugPrintNodeset(xpath->nodesetval);
	    }

	default:
	    break;
    }

    return FALSE;
}

#if 0
/*
 * Local variables are stored in Transform context, search for the 'var' in 
 * local variables list
 */
static xmlXPathObjectPtr
slaxDebugLocalVarScan (slaxDebugState_t *statep, const char *var)
{
    xsltTransformContextPtr tctxt = statep->ds_ctxt;
    xsltStackElemPtr elem;
    int i;

    if (tctxt->varsNr == 0)
	return NULL;

    /*
     * Search for 'var' only in  the current templ. varsBase is the base
     * for current templ so search only till there
     */
    for (i = tctxt->varsNr; i > tctxt->varsBase; i--) {
	elem = tctxt->varsTab[i - 1];

	if (streq((const char *) elem->name, var))
	    return elem->value;
    }

    return NULL;
}
#endif

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
slaxDebugPrintScriptLine (slaxDebugState_t *statep,
			  const char *filename, int line_no)
{
    FILE *fp;
    int count = 0;
    const char *cp;
    char line[BUFSIZ];

    if (filename == NULL || line_no == 0) 
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

	if (count == line_no)
	    break;
    }

    fclose(fp);

    cp = strrchr(filename, '/');
    cp = cp ? cp + 1 : filename;

    slaxDebugOutput(" \n%s:%d %s", cp, line_no, line);

    if (slaxDebugDisplayMode == EMACS_MODE) {
	char rel_path[BUFSIZ];

	/*
	 * In emacs path should be relative to remote default directory,
	 * so print the relative path of the current file from main stylesheet
	 */
	cp = (const char *) statep->ds_stylesheet->doc->URL;
	slaxDebugMakeRelativePath(cp, filename, rel_path, sizeof(rel_path));
	
	slaxDebugOutput("%c%c%s:%d:0", 26, 26, rel_path, line_no);
    }

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
	node = slaxDebugGetNode(statep, (xmlNodePtr) statep->ds_node->doc,
				(const char *) statep->ds_node->doc->URL,
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
	    slaxDebugOutput("%s", cmdp->dc_help);
    }
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
 * 'next' command
 */
static void
slaxDebugCmdNext (DC_ARGS)
{
    xsltSetDebuggerStatus(XSLT_DEBUG_NEXT);
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

    nsList = xmlGetNsList(node->doc, node);
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
    slaxDebugPrintXpath("expr", "tag", res);
    xmlXPathFreeObject(res);

#if 0
    const char *name = argv[1];
    xmlXPathObjectPtr xpath;

    if (name == NULL)
	return;

    /*
     * Just incase if the variable name is prefixed with '$' skip that
     */
    if (name[0] == '$')
	name += 1;

    /*
     * Scan local variables
     */
    xpath = slaxDebugLocalVarScan(statep, name);
    if (xpath) {
	slaxDebugPrintXpath(name, "local", xpath);
	return;
    } 

    xpath = xsltVariableLookup(statep->ds_ctxt, (const xmlChar *) name, NULL);
    if (xpath) {
	slaxDebugPrintXpath(name, "global", xpath);
	return;
    } 

    slaxDebugOutput("Variable '%s' not found", name);
#endif
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

	if (dsfp->dsf_template->match) {
	    name = (const char *) dsfp->dsf_template->match;
	} else if (dsfp->dsf_template->name) {
	    name = (const char *) dsfp->dsf_template->name;
	    tag = "()";
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
    xsltSetDebuggerStatus(XSLT_DEBUG_NONE);
    statep->ds_ctxt->debugStatus = XSLT_DEBUG_NONE;
}

/* ---------------------------------------------------------------------- */

static slaxDebugCommand_t slaxDebugCommandTable[] = {
    { "break",	    slaxDebugCmdBreak,
      "break [line] [file:line] [template] -> Add a breakpoint" },

    { "continue",   slaxDebugCmdContinue,
      "continue -> Continue running the script" },

    { "delete",	    slaxDebugCmdDelete,
      "delete [num] -> Delete breakpoints" },

    { "help",	    slaxDebugCmdHelp,
      "help -> Print this help message" },

    { "mode",	    slaxDebugCmdMode,	    NULL }, /* Hidden from user */

    { "next",	    slaxDebugCmdNext,
      "next -> Execute the next line" },

    { "print",	    slaxDebugCmdPrint,
      "print <var-name> -> Print the current value of a variable" },

    { "where",	    slaxDebugCmdWhere,
      "where -> Print the backtrace of template calls" } ,

    { "quit",	    slaxDebugCmdQuit,
      "quit -> Quit debugger" },
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
    char line[BUFSIZ];
    char *cp, *input;
    static char prompt[] = "debug> ";
    xmlNodePtr cur = statep->ds_inst;
    int line_no = xmlGetLineNo(statep->ds_inst);

    line[0] = '\0';

    slaxDebugPrintScriptLine(statep, (const char *) cur->doc->URL, line_no);

    input = slaxDebugInput(prompt);
    if (input == NULL)
	return -1;

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

/*
 * Called as callback function from libxslt before each statement is executed.
 * Here is where we handle all our debugger logic
 *
 * cur   -> current libxslt node being executed
 * node  -> current context node
 * templ -> current templ being executed
 * ctxt  -> transformation context
 */
static void 
slaxDebugHandler (xmlNodePtr inst, xmlNodePtr node,
		  xsltTemplatePtr template, xsltTransformContextPtr ctxt)
{
    slaxDebugState_t *statep = slaxDebugGetState();
    int status;

    /*
     * We do not debug text node
     */
    if (inst && inst->type == XML_TEXT_NODE)
	return;

    /*
     * Fill in the current state
     */
    statep->ds_inst = inst;
    statep->ds_node = node;
    statep->ds_template = template;
    statep->ds_ctxt = ctxt;

    slaxDebugCheckBreakpoint(statep, statep->ds_inst, TRUE);

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
		break;

	    case XSLT_DEBUG_CONT:
	    case XSLT_DEBUG_NONE:
		return;

	    case XSLT_DEBUG_NEXT:
		xsltSetDebuggerStatus(XSLT_DEBUG_INIT);
		return;
	}
    }
}

/*
 * Called from libxslt as callback function when template is executed.
 */
static int 
slaxDebugAddFrame (xsltTemplatePtr template, xmlNodePtr node)
{
    slaxDebugState_t *statep = slaxDebugGetState();
    slaxDebugStackFrame_t *dsfp;
    static xmlNodePtr last_node = NULL;

    if (!template || !node)
	return 0;

    /*
     * libxslt calls this callback function for each xsl:param in the 
     * template. So, if the template has two xsl:param then this function 
     * will be called twice, but we want to store the template name in the 
     * stack linked list only once, so we do a trick here, if the last node
     * and the cur node is same we return. return 0 so that libxslt does
     * not call slaxDebugDropFrame() for this.
     */
    if (last_node && node == last_node) {
	last_node = NULL;
	return 0;
    }
    last_node = node;		/* Save the last node */

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

    /*
     * Figure out from where this template is called.
     *
     * 'ds_inst' will have the node of the last statement being
     * executed, if that is 'call-template' or 'with-param' then that
     * is the location from where this template is called.
     */
    if (statep->ds_inst
	&& (streq((const char *) statep->ds_inst->name, "call-template")
	    || streq((const char *) statep->ds_inst->name, "with-param"))) {
	dsfp->dsf_inst = statep->ds_inst;
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
    slaxDebugStackFrame_t *dsfp;

    dsfp = TAILQ_LAST(&slaxDebugStack, slaxDebugStack_s);
    if (dsfp) {
	TAILQ_REMOVE(&slaxDebugStack, dsfp, dsf_link);
	xmlFree(dsfp);
    }
}

/*
 * Register debugger
 */
int
slaxDebugRegister (slaxDebugInputCallback_t input_callback,
	      slaxDebugOutputCallback_t output_callback)
{
    static int done_register;
    static void *callbacks[] = {
	slaxDebugHandler,
	slaxDebugAddFrame,
	slaxDebugDropFrame,
    };

    if (done_register)
	return FALSE;

    TAILQ_INIT(&slaxDebugBreakpoints);
    TAILQ_INIT(&slaxDebugStack);

    xsltSetDebuggerStatus(XSLT_DEBUG_INIT);
    xsltSetDebuggerCallbacks(NUM_ARRAY(callbacks), &callbacks);

    slaxDebugDisplayMode = CLI_MODE;
    done_register = TRUE;
    slaxDebugInputCallback = input_callback;
    slaxDebugOutputCallback = output_callback;

    slaxDebugOutput("Enterring SLAX Debugger");
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
