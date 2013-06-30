/*
 * $Id$
 *
 * Copyright (c) 2006-2011, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * slaxproc -- a command line interface to the SLAX language
 */

#include "slaxinternals.h"
#include <libslax/slax.h>
#include <sys/queue.h>
#include "config.h"

#include <libxslt/transform.h>
#include <libxml/HTMLparser.h>
#include <libxslt/xsltutils.h>
#include <libxml/globals.h>
#include <libexslt/exslt.h>
#include <libslax/slaxdyn.h>
#include <libslax/slaxdata.h>

#include <err.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>

static slax_data_list_t plist;
static int nbparams;
static const char **params;

static int options = XSLT_PARSE_OPTIONS;
static char *encoding;
static char *opt_version;
static char **opt_args;

static int opt_html;		/* Parse input as HTML */
static int opt_indent;		/* Indent the output (pretty print) */
static int opt_partial;		/* Parse partial contents */
static int opt_debugger;	/* Invoke the debugger */
static int opt_empty_input;	/* Use an empty input file */
static int opt_slax_output;	/* Make output in SLAX format */

static const char *
get_filename (const char *filename, char ***pargv, int outp)
{
    if (filename == NULL) {
	filename = **pargv;
	if (filename)
	    *pargv += 1;
	else filename = "-";
    }

    if (outp >= 0 && slaxFilenameIsStd(filename))
	filename = outp ? "/dev/stdout" : "/dev/stdin";
    return filename;
}

static int
do_format (const char *name UNUSED, const char *output,
		 const char *input, char **argv)
{
    FILE *infile, *outfile;
    xmlDocPtr docp;

    input = get_filename(input, &argv, -1);
    output = get_filename(output, &argv, -1);

    if (slaxFilenameIsStd(input))
	infile = stdin;
    else {
	infile = fopen(input, "r");
	if (infile == NULL)
	    err(1, "file open failed for '%s'", input);
    }

    docp = slaxLoadFile(input, infile, NULL, opt_partial);

    if (infile != stdin)
	fclose(infile);

    if (docp == NULL)
	errx(1, "cannot parse file: '%s'", input);

    if (output == NULL || slaxFilenameIsStd(output))
	outfile = stdout;
    else {
	outfile = fopen(output, "w");
	if (outfile == NULL)
	    err(1, "could not open output file: '%s'", output);
    }

    slaxWriteDoc((slaxWriterFunc_t) fprintf, outfile, docp,
		 opt_partial, opt_version);

    if (outfile != stdout)
	fclose(outfile);

    xmlFreeDoc(docp);

    return 0;
}

static int
do_slax_to_xslt (const char *name UNUSED, const char *output,
		 const char *input, char **argv)
{
    FILE *infile, *outfile;
    xmlDocPtr docp;

    input = get_filename(input, &argv, -1);
    output = get_filename(output, &argv, -1);

    if (slaxFilenameIsStd(input))
	infile = stdin;
    else {
	infile = fopen(input, "r");
	if (infile == NULL)
	    err(1, "file open failed for '%s'", input);
    }

    docp = slaxLoadFile(input, infile, NULL, opt_partial);

    if (infile != stdin)
	fclose(infile);

    if (docp == NULL)
	errx(1, "cannot parse file: '%s'", input);

    if (output == NULL || slaxFilenameIsStd(output))
	outfile = stdout;
    else {
	outfile = fopen(output, "w");
	if (outfile == NULL)
	    err(1, "could not open output file: '%s'", output);
    }

    slaxDumpToFd(fileno(outfile), docp, opt_partial);

    if (outfile != stdout)
	fclose(outfile);

    xmlFreeDoc(docp);

    return 0;
}

static int
do_xslt_to_slax (const char *name UNUSED, const char *output,
		 const char *input, char **argv)
{
    xmlDocPtr docp;
    FILE *outfile;

    input = get_filename(input, &argv, 0);
    output = get_filename(output, &argv, -1);

    docp = xmlReadFile(input, NULL, XSLT_PARSE_OPTIONS);
    if (docp == NULL) {
	errx(1, "cannot parse file: '%s'", input);
        return -1;
    }

    if (output == NULL || slaxFilenameIsStd(output))
	outfile = stdout;
    else {
	outfile = fopen(output, "w");
	if (outfile == NULL)
	    err(1, "could not open file: '%s'", output);
    }

    slaxWriteDoc((slaxWriterFunc_t) fprintf, outfile, docp,
		 opt_partial, opt_version);

    if (outfile != stdout)
	fclose(outfile);

    xmlFreeDoc(docp);

    return 0;
}

static xmlDocPtr
buildEmptyFile (void)
{
    xmlDocPtr docp;

    docp = xmlNewDoc((const xmlChar *) XML_DEFAULT_VERSION);
    if (docp) {
	docp->standalone = 1;
    }

    return docp;
}

static int
do_run (const char *name, const char *output, const char *input, char **argv)
{
    xmlDocPtr scriptdoc;
    const char *scriptname;
    FILE *scriptfile, *outfile;
    xmlDocPtr indoc;
    xsltStylesheetPtr script;
    xmlDocPtr res = NULL;
    char buf[BUFSIZ];

    scriptname = get_filename(name, &argv, -1);
    if (!opt_empty_input)
	input = get_filename(input, &argv, -1);
    output = get_filename(output, &argv, -1);

    if (slaxFilenameIsStd(scriptname))
	errx(1, "script file cannot be stdin");

    scriptfile = slaxFindIncludeFile(scriptname, buf, sizeof(buf));
    if (scriptfile == NULL)
	err(1, "file open failed for '%s'", scriptname);

    scriptdoc = slaxLoadFile(scriptname, scriptfile, NULL, 0);
    if (scriptdoc == NULL)
	errx(1, "cannot parse: '%s'", scriptname);
    if (scriptfile != stdin)
	fclose(scriptfile);

    script = xsltParseStylesheetDoc(scriptdoc);
    if (script == NULL || script->errors != 0)
	errx(1, "%d errors parsing script: '%s'",
	     script ? script->errors : 1, scriptname);

    if (opt_empty_input)
	indoc = buildEmptyFile();
    else if (opt_html)
	indoc = htmlReadFile(input, encoding, options);
    else
	indoc = xmlReadFile(input, encoding, options);
    if (indoc == NULL)
	errx(1, "unable to parse: '%s'", input);

    if (opt_indent)
	script->indent = 1;

    if (opt_debugger) {
	slaxDebugInit();
	slaxDebugSetStylesheet(script);
	res = slaxDebugApplyStylesheet(scriptname, script,
				 slaxFilenameIsStd(input) ? NULL : input,
				 indoc, params);
    } else {
	res = xsltApplyStylesheet(script, indoc, params);
    }

    if (res) {
	if (output == NULL || slaxFilenameIsStd(output))
	    outfile = stdout;
	else {
	    outfile = fopen(output, "w");
	    if (outfile == NULL)
		err(1, "could not open file: '%s'", output);
	}

	if (opt_slax_output)
	    slaxWriteDoc((slaxWriterFunc_t) fprintf, outfile, res,
		 TRUE, opt_version);
	else
	    xsltSaveResultToFile(outfile, res, script);

	if (outfile != stdout)
	    fclose(outfile);

	xmlFreeDoc(res);
    }

    xmlFreeDoc(indoc);
    xsltFreeStylesheet(script);

    return 0;
}

static int
do_check (const char *name, const char *output UNUSED,
	  const char *input UNUSED, char **argv)
{
    xmlDocPtr scriptdoc;
    const char *scriptname;
    FILE *scriptfile;
    xsltStylesheetPtr script;

    scriptname = get_filename(name, &argv, -1);

    if (slaxFilenameIsStd(scriptname))
	errx(1, "script file cannot be stdin");

    scriptfile = fopen(scriptname, "r");
    if (scriptfile == NULL)
	err(1, "file open failed for '%s'", scriptname);

    scriptdoc = slaxLoadFile(scriptname, scriptfile, NULL, 0);
    if (scriptdoc == NULL)
	errx(1, "cannot parse: '%s'", scriptname);
    if (scriptfile != stdin)
	fclose(scriptfile);

    script = xsltParseStylesheetDoc(scriptdoc);
    if (script == NULL || script->errors != 0)
	errx(1, "%d errors parsing script: '%s'",
	     script ? script->errors : 1, scriptname);

    fprintf(stderr, "script check succeeds\n");

    xsltFreeStylesheet(script);

    return 0;
}

static void
print_version (void)
{
    printf("libslax version %s%s\n",  LIBSLAX_VERSION, LIBSLAX_VERSION_EXTRA);
    printf("Using libxml %s, libxslt %s and libexslt %s\n",
	   xmlParserVersion, xsltEngineVersion, exsltLibraryVersion);
    printf("slaxproc was compiled against libxml %d%s, "
	   "libxslt %d%s and libexslt %d\n",
	   LIBXML_VERSION, LIBXML_VERSION_EXTRA,
	   LIBXSLT_VERSION, LIBXSLT_VERSION_EXTRA, LIBEXSLT_VERSION);
    printf("libxslt %d was compiled against libxml %d\n",
	   xsltLibxsltVersion, xsltLibxmlVersion);
    printf("libexslt %d was compiled against libxml %d\n",
	   exsltLibexsltVersion, exsltLibxmlVersion);
}

static void
print_help (void)
{
    printf("Usage: slaxproc [mode] [options] [script] [files]\n");
    printf("    Modes:\n");
    printf("\t--check OR -c: check syntax and content for a SLAX script\n");
    printf("\t--format OR -F: format (pretty print) a SLAX script\n");
    printf("\t--run OR -r: run a SLAX script (the default mode)\n");
    printf("\t--slax-to-xslt OR -x: turn SLAX into XSLT\n");
    printf("\t--xslt-to-slax OR -s: turn XSLT into SLAX\n");
    printf("\n");

    printf("    Options:\n");
    printf("\t--debug OR -d: enable the SLAX/XSLT debugger\n");
    printf("\t--empty OR -E: give an empty document for input\n");
    printf("\t--exslt OR -e: enable the EXSLT library\n");
    printf("\t--help OR -h: display this help message\n");
    printf("\t--html OR -H: Parse input data as HTML\n");
    printf("\t--ignore-arguments: Do not process any further arguments\n");
    printf("\t--include <dir> OR -I <dir>: search directory for includes/imports\n");
    printf("\t--indent OR -g: indent output ala output-method/indent\n");
    printf("\t--input <file> OR -i <file>: take input from the given file\n");
    printf("\t--lib <dir> OR -L <dir>: search directory for extension libraries\n");
    printf("\t--name <file> OR -n <file>: read the script from the given file\n");
    printf("\t--no-randomize: do not initialize the random number generator\n");
    printf("\t--output <file> OR -o <file>: make output into the given file\n");
    printf("\t--param <name> <value> OR -a <name> <value>: pass parameters\n");
    printf("\t--partial OR -p: allow partial SLAX input to --slax-to-xslt\n");
    printf("\t--slax-output OR -S: Write the result using SLAX-style XML (braces, etc)\n");
    printf("\t--trace <file> OR -t <file>: write trace data to a file\n");
    printf("\t--verbose OR -v: enable debugging output (slaxLog())\n");
    printf("\t--version OR -V: show version information (and exit)\n");
    printf("\t--write-version <version> OR -w <version>: write in version\n");
    printf("\nProject libslax home page: https://github.com/Juniper/libslax\n");
}

int
main (int argc UNUSED, char **argv)
{
    const char *cp;
    const char *input = NULL, *output = NULL, *name = NULL, *trace_file = NULL;
    int (*func)(const char *, const char *, const char *, char **) = NULL;
    int use_exslt = FALSE;
    FILE *trace_fp = NULL;
    int randomize = 1;
    int logger = FALSE;
    slax_data_node_t *dnp;
    int i;
    unsigned ioflags = 0;
    int opt_ignore_arguments = FALSE;
    char *opt_log_file = NULL;

    slaxDataListInit(&plist);

    opt_args = argv;

    for (argv++; *argv; argv++) {
	cp = *argv;

	if (*cp != '-')
	    break;

	/*
	 * We mirror the order in print_help() above:
	 * - first list the modes in alphabetically order
	 * - then list the options in alphabetically order
	 */

	if (streq(cp, "--check") || streq(cp, "-c")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_check;

	} else if (streq(cp, "--format") || streq(cp, "-F")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_format;

	} else if (streq(cp, "--run") || streq(cp, "-r")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_run;

	} else if (streq(cp, "--slax-to-xslt") || streq(cp, "-x")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_slax_to_xslt;

	} else if (streq(cp, "--xslt-to-slax") || streq(cp, "-s")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_xslt_to_slax;

	} else if (streq(cp, "--debug") || streq(cp, "-d")) {
	    opt_debugger = TRUE;

	} else if (streq(cp, "--empty") || streq(cp, "-E")) {
	    opt_empty_input = TRUE;

	} else if (streq(cp, "--exslt") || streq(cp, "-e")) {
	    use_exslt = TRUE;

	} else if (streq(cp, "--help") || streq(cp, "-h")) {
	    print_help();
	    return -1;

	} else if (streq(cp, "--html") || streq(cp, "-H")) {
	    opt_html = TRUE;

	} else if (streq(cp, "--ignore-arguments")) {
	    opt_ignore_arguments = TRUE;
	    break;

	} else if (streq(cp, "--include") || streq(cp, "-I")) {
	    slaxIncludeAdd(*++argv);

	} else if (streq(cp, "--indent") || streq(cp, "-g")) {
	    opt_indent = TRUE;

	} else if (streq(cp, "--input") || streq(cp, "-i")) {
	    input = *++argv;

	} else if (streq(cp, "--lib") || streq(cp, "-L")) {
	    slaxDynAdd(*++argv);

	} else if (streq(cp, "--log") || streq(cp, "-l")) {
	    opt_log_file = *++argv;

	} else if (streq(cp, "--name") || streq(cp, "-n")) {
	    name = *++argv;

	} else if (streq(cp, "--no-randomize")) {
	    randomize = 0;

	} else if (streq(cp, "--no-tty")) {
	    ioflags |= SIF_NO_TTY;

	} else if (streq(cp, "--output") || streq(cp, "-o")) {
	    output = *++argv;

	} else if (streq(cp, "--param") || streq(cp, "-a")) {
	    char *pname = *++argv;
	    char *pvalue = *++argv;
	    char *tvalue;
	    char quote;
	    int plen;

	    if (pname == NULL || pvalue == NULL)
		errx(1, "missing parameter value");

	    plen = strlen(pvalue);
	    tvalue = xmlMalloc(plen + 3);
	    if (tvalue == NULL)
		errx(1, "out of memory");

	    quote = strrchr(pvalue, '\"') ? '\'' : '\"';
	    tvalue[0] = quote;
	    memcpy(tvalue + 1, pvalue, plen);
	    tvalue[plen + 1] = quote;
	    tvalue[plen + 2] = '\0';

	    nbparams += 1;
	    slaxDataListAddNul(&plist, pname);
	    slaxDataListAddNul(&plist, tvalue);

	} else if (streq(cp, "--partial") || streq(cp, "-p")) {
	    opt_partial = TRUE;

	} else if (streq(cp, "--slax-output") || streq(cp, "-S")) {
	    opt_slax_output = TRUE;

	} else if (streq(cp, "--trace") || streq(cp, "-t")) {
	    trace_file = *++argv;

	} else if (streq(cp, "--verbose") || streq(cp, "-v")) {
	    logger = TRUE;

	} else if (streq(cp, "--version") || streq(cp, "-V")) {
	    print_version();
	    exit(0);

	} else if (streq(cp, "--write-version") || streq(cp, "-w")) {
	    opt_version = *++argv;

	} else if (streq(cp, "--yydebug") || streq(cp, "-y")) {
	    slaxYyDebug = TRUE;

	} else {
	    fprintf(stderr, "invalid option: %s\n", cp);
	    print_help();
	    return -1;
	}

	if (*argv == NULL) {
	    /*
	     * The only way we could have a null argv is if we said
	     * "xxx = *++argv" off the end of argv.  Bail.
	     */
	    fprintf(stderr, "missing option value: %s\n", cp);
	    print_help();
	    return 1;
	}
    }

    cp = getenv("SLAXPATH");
    if (cp)
	slaxIncludeAddPath(cp);

    params = alloca(nbparams * 2 * sizeof(*params) + 1);
    i = 0;
    SLAXDATALIST_FOREACH(dnp, &plist) {
	params[i++] = dnp->dn_data;
    }
    params[i] = NULL;

    if (func == NULL)
	func = do_run; /* the default action */

    /*
     * Seed the random number generator.  This is optional to allow
     * test jigs to take advantage of the default stream of generated
     * numbers.
     */
    if (randomize)
	slaxInitRandomizer();

    /*
     * Start the XML API
     */
    xmlInitParser();
    xsltInit();
    slaxEnable(SLAX_ENABLE);
    slaxIoUseStdio(ioflags);

    if (opt_log_file) {
	FILE *fp = fopen(opt_log_file, "w");
	if (fp == NULL)
	    err(1, "could not open log file: '%s'", opt_log_file);

	slaxLogEnable(TRUE);
	slaxLogToFile(fp);

    } else if (logger) {
	slaxLogEnable(TRUE);
    }

    if (use_exslt)
	exsltRegisterAll();

    if (trace_file) {
	if (slaxFilenameIsStd(trace_file))
	    trace_fp = stderr;
	else {
	    trace_fp = fopen(trace_file, "w");
	    if (trace_fp == NULL)
		err(1, "could not open trace file: '%s'", trace_file);
	}
	slaxTraceToFile(trace_fp);
    }

    if (opt_ignore_arguments) {
	static char *null_argv[] = { NULL };
	argv = null_argv;
    }

    func(name, output, input, argv);

    if (trace_fp && trace_fp != stderr)
	fclose(trace_fp);

    slaxDynClean();
    xsltCleanupGlobals();
    xmlCleanupParser();

    return slaxGetExitCode();
}
