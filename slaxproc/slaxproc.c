/*
 * $Id$
 *
 * Copyright (c) 2006-2010, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 *
 * slaxproc -- a command line interface to the SLAX language
 */

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "config.h"

#include <libxslt/transform.h>
#include <libxml/HTMLparser.h>
#include <libxslt/xsltutils.h>
#include <libxml/globals.h>
#include <libexslt/exslt.h>

#include <err.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>

#define MAX_PARAMETERS 64
#define MAX_PATHS 64

static const char *params[MAX_PARAMETERS + 1];
static int nbparams;

#if 0
static xmlChar *strparams[MAX_PARAMETERS + 1];
static int nbstrparams;
static xmlChar *paths[MAX_PATHS + 1];
static int nbpaths;
#endif

static int options = XSLT_PARSE_OPTIONS;
static int html;
static char *encoding;
static char *version;
static int indent;

static int partial;
static int use_debugger;

static inline int
is_filename_std (const char *filename)
{
    return (filename == NULL || (filename[0] == '-' && filename[1] == '\0'));
}

static const char *
get_filename (const char *filename, char ***pargv, int outp)
{
    if (filename == NULL) {
	filename = **pargv;
	if (filename)
	    *pargv += 1;
	else filename = "-";
    }

    if (outp >= 0 && is_filename_std(filename))
	filename = outp ? "/dev/stdout" : "/dev/stdin";
    return filename;
}

static int
do_slax_to_xslt (const char *name UNUSED, const char *output,
		 const char *input, char **argv)
{
    FILE *infile, *outfile;
    xmlDocPtr docp;

    input = get_filename(input, &argv, -1);
    output = get_filename(output, &argv, -1);

    if (is_filename_std(input))
	infile = stdin;
    else {
	infile = fopen(input, "r");
	if (infile == NULL)
	    err(1, "file open failed for '%s'", input);
    }

    docp = slaxLoadFile(input, infile, NULL, partial);

    if (infile != stdin)
	fclose(infile);

    if (docp == NULL)
	errx(1, "cannot parse file: '%s'", input);

    if (output == NULL || is_filename_std(output))
	outfile = stdout;
    else {
	outfile = fopen(output, "w");
	if (outfile == NULL)
	    err(1, "could not open output file: '%s'", output);
    }

    slaxDumpToFd(fileno(outfile), docp, partial);

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

    if (output == NULL || is_filename_std(output))
	outfile = stdout;
    else {
	outfile = fopen(output, "w");
	if (outfile == NULL)
	    err(1, "could not open file: '%s'", output);
    }

    slaxWriteDoc((slaxWriterFunc_t) fprintf, outfile, docp, partial, version);

    if (outfile != stdout)
	fclose(outfile);

    xmlFreeDoc(docp);

    return 0;
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

    scriptname = get_filename(name, &argv, -1);
    input = get_filename(input, &argv, -1);
    output = get_filename(output, &argv, -1);

    if (is_filename_std(scriptname))
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

    if (html)
	indoc = htmlReadFile(input, encoding, options);
    else
	indoc = xmlReadFile(input, encoding, options);
    if (indoc == NULL)
	errx(1, "unable to parse: '%s'", input);

    if (indent)
	script->indent = 1;

    if (use_debugger) {
	slaxDebugInit();
	slaxDebugSetStylesheet(script);
	slaxDebugApplyStylesheet(scriptname, script,
				 is_filename_std(input) ? NULL : input,
				 indoc, params);
    } else {
	res = xsltApplyStylesheet(script, indoc, params);

	if (output == NULL || is_filename_std(output))
	    outfile = stdout;
	else {
	    outfile = fopen(output, "w");
	    if (outfile == NULL)
		err(1, "could not open file: '%s'", output);
	}

	xsltSaveResultToFile(outfile, res, script);

	if (outfile != stdout)
	    fclose(outfile);

	if (res)
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

    if (is_filename_std(scriptname))
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

/*
 * An ugly attempt to seed the random number generator with the best
 * value possible.  Ugly, but localized ugliness.
 */
static void
init_randomizer (void)
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
    fprintf(stderr, "could not initialize random\n");
#endif /* HAVE_SRAND */
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
    printf("\t--run OR -r: run a SLAX script (the default mode)\n");
    printf("\t--slax-to-xslt OR -x: turn SLAX into XSLT\n");
    printf("\t--xslt-to-slax OR -s: turn XSLT into SLAX\n");
    printf("\t--check OR -c: check syntax and content for a SLAX script\n");
    printf("\n");

    printf("    Options:\n");
    printf("\t--debug OR -d: enable the SLAX/XSLT debugger\n");
    printf("\t--exslt OR -e: enable the EXSLT library\n");
    printf("\t--indent OR -g: indent output ala output-method/indent\n");
    printf("\t--help OR -h: display this help message\n");
    printf("\t--input <file> OR -i <file>: take input from the given file\n");
    printf("\t--name <file> OR -n <file>: read the script from the given file\n");
    printf("\t--no-randomize: do not initialize the random number generator\n");
    printf("\t--output <file> OR -o <file>: make output into the given file\n");
    printf("\t--param name value OR -a name value: pass parameters\n");
    printf("\t--partial OR -p: allow partial SLAX input to --slax-to-xslt\n");
    printf("\t--trace <file> OR -t <file>: write trace data to a file\n");
    printf("\t--verbose OR -v: enable debugging output (slaxLog())\n");
    printf("\t--version OR -V: show version information (and exit)\n");
    printf("\t--write-version <version> OR -w <version>: write in version\n");
    printf("\nProject libslax home page: http://code.google.com/p/libslax\n");
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

    for (argv++; *argv; argv++) {
	cp = *argv;

	if (*cp != '-')
	    break;

	if (streq(cp, "--version") || streq(cp, "-V")) {
	    print_version();
	    exit(0);

	} else if (streq(cp, "--slax-to-xslt") || streq(cp, "-x")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_slax_to_xslt;

	} else if (streq(cp, "--xslt-to-slax") || streq(cp, "-s")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_xslt_to_slax;

	} else if (streq(cp, "--run") || streq(cp, "-r")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_run;

	} else if (streq(cp, "--check") || streq(cp, "-c")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_check;

	} else if (streq(cp, "--exslt") || streq(cp, "-e")) {
	    use_exslt = TRUE;

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

	    int pnum = nbparams++;
	    params[pnum++] = pname;
	    params[pnum] = tvalue;

	} else if (streq(cp, "--input") || streq(cp, "-i")) {
	    input = *++argv;

	} else if (streq(cp, "--output") || streq(cp, "-o")) {
	    output = *++argv;

	} else if (streq(cp, "--verbose") || streq(cp, "-v")) {
	    slaxDebug = TRUE;

	} else if (streq(cp, "--debug") || streq(cp, "-d")) {
	    use_debugger = TRUE;

	} else if (streq(cp, "--partial") || streq(cp, "-p")) {
	    partial = TRUE;

	} else if (streq(cp, "--indent") || streq(cp, "-g")) {
	    indent = TRUE;

	} else if (streq(cp, "--name") || streq(cp, "-n")) {
	    name = *++argv;

	} else if (streq(cp, "--trace") || streq(cp, "-t")) {
	    trace_file = *++argv;

	} else if (streq(cp, "--write-version") || streq(cp, "-w")) {
	    version = *++argv;

	} else if (streq(cp, "--help") || streq(cp, "-n")) {
	    print_help();
	    return -1;

	} else if (streq(cp, "--no-randomize")) {
	    randomize = 0;

	} else {
	    fprintf(stderr, "invalid option: %s\n", cp);
	    print_help();
	    return -1;
	}
    }

    if (func == NULL)
	func = do_run; /* the default action */

    /*
     * Seed the random number generator.  This is optional to allow
     * test jigs to take advantage of the default stream of generated
     * numbers.
     */
    if (randomize)
	init_randomizer();

    /*
     * Start the XML API
     */
    xmlInitParser();
    xsltInit();
    slaxEnable(SLAX_ENABLE);
    slaxIoUseStdio();

    if (use_exslt)
	exsltRegisterAll();

    if (trace_file) {
	trace_fp = is_filename_std(trace_file)
	    ? stderr : fopen(trace_file, "w");
	slaxTraceToFile(trace_fp);
    }

    func(name, output, input, argv);

    if (trace_fp && trace_fp != stderr)
	fclose(trace_fp);

    xsltCleanupGlobals();
    xmlCleanupParser();

    return 0;
}
