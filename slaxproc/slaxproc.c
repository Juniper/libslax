/*
 * $Id$
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

#define MAX_PARAMETERS 64
#define MAX_PATHS 64

#if 0
static const char *params[MAX_PARAMETERS + 1];
static int nbparams = 0;
static xmlChar *strparams[MAX_PARAMETERS + 1];
static int nbstrparams = 0;
static xmlChar *paths[MAX_PATHS + 1];
static int nbpaths = 0;
#endif
static int options = XSLT_PARSE_OPTIONS;
static int html = 0;
static char *encoding = NULL;

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

    docp = slaxLoadFile(input, infile, NULL);

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

    slaxDumpToFd(fileno(outfile), docp);

    fclose(outfile);

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

    slaxWriteDoc((slaxWriterFunc_t) fprintf, outfile, docp);
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
    FILE *scriptfile;
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

    scriptdoc = slaxLoadFile(scriptname, scriptfile, NULL);
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

    res = xsltApplyStylesheet(script, indoc, NULL);

    xsltSaveResultToFile(stdout, res, script);

    xmlFreeDoc(res);
    xmlFreeDoc(indoc);
    xsltFreeStylesheet(script);

    return 0;
}

static void
print_version (void)
{
    printf("libslax version %s\n",  PACKAGE_VERSION);
    printf("Using libxml %s, libxslt %s and libexslt %s\n",
	   xmlParserVersion, xsltEngineVersion, exsltLibraryVersion);
    printf("slaxproc was compiled against libxml %d, "
	   "libxslt %d and libexslt %d\n",
	   LIBXML_VERSION, LIBXSLT_VERSION, LIBEXSLT_VERSION);
    printf("libxslt %d was compiled against libxml %d\n",
	   xsltLibxsltVersion, xsltLibxmlVersion);
    printf("libexslt %d was compiled against libxml %d\n",
	   exsltLibexsltVersion, exsltLibxmlVersion);
}

int
main (int argc UNUSED, char **argv)
{
    const char *cp;
    const char *input = NULL, *output = NULL, *name = NULL;
    int (*func)(const char *name, const char *output,
		const char *input, char **argv) = NULL;
    int use_exslt = FALSE;

    for (argv++; *argv; argv++) {
	cp = *argv;

	if (*cp != '-')
	    break;

	if (streq(cp, "--version") || streq(cp, "-v")) {
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

	} else if (streq(cp, "--exslt") || streq(cp, "-e")) {
	    use_exslt = TRUE;

	} else if (streq(cp, "--input") || streq(cp, "-i")) {
	    input = *++argv;

	} else if (streq(cp, "--output") || streq(cp, "-o")) {
	    output = *++argv;

	} else if (streq(cp, "--debug") || streq(cp, "-d")) {
	    slaxDebug = TRUE;

	} else if (streq(cp, "--name") || streq(cp, "-n")) {
	    name = *++argv;

	} else {
	    fprintf(stderr, "invalid option\n");
	    return -1;
	}
    }

    if (func == NULL)
	func = do_run; /* the default action */

    /*
     * Start the XML API
     */
    xmlInitParser();
    xsltInit();
    slaxEnable(SLAX_ENABLE);
    if (use_exslt)
	exsltRegisterAll();

    func(name, output, input, argv);

    xsltCleanupGlobals();
    xmlCleanupParser();

    return 0;
}
