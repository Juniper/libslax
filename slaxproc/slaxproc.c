/*
 * $Id$
 */

#include "slaxinternals.h"
#include <libslax/slax.h>
#include "config.h"

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
do_slax_to_xslt (const char *output, const char *input, char **argv)
{
    FILE *infile, *outfile;
    FILE *file;
    xmlDocPtr docp;

    input = get_filename(input, &argv, -1);
    output = get_filename(output, &argv, -1);

    if (is_filename_std(input))
	infile = stdin;
    else {
	infile = fopen(input, "r");
	if (infile == NULL) {
	    err(1, "file open failed for '%s'", input);
	}
    }

    docp = slaxLoadFile(input, infile, NULL);

    if (infile != stdin)
	fclose(infile);

    /*
    docp = slaxLoader(input, NULL, XSLT_PARSE_OPTIONS,
                               NULL, XSLT_LOAD_START);
    */

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

    return 0;
}

static int
do_xslt_to_slax (const char *output, const char *input, char **argv)
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
	    err(1, "could not open file: '%s'", outfile);;
    }

    slaxWriteDoc((slaxWriterFunc_t) fprintf, outfile, docp);
    if (outfile != stdout)
	fclose(outfile);

    return 0;
}

static int
do_run (const char *output, const char *input, char **argv)
{
    xmlDocPtr docp;
    FILE *scriptfile;
    const char *script;

    script = get_filename(NULL, &argv, -1);
    input = get_filename(input, &argv, -1);
    output = get_filename(output, &argv, -1);

    if (is_filename_std(script))
	scriptfile = stdin;
    else {
	scriptfile = fopen(script, "r");
	if (scriptfile == NULL) {
	    err(1, "file open failed for '%s'", script);
	}
    }

    docp = slaxLoadFile(script, scriptfile, NULL);
    if (docp == NULL)
	errx(1, "cannot parse: '%s'", script);

    return 0;
}

int
main (int argc UNUSED, char **argv)
{
    xmlDocPtr docp;
    const char *cp;
    const char *input = NULL, *output = NULL;
    int (*func)(const char *output, const char *input, char **argv) = NULL;

    for (argv++; *argv; argv++) {
	cp = *argv;

	if (*cp != '-')
	    break;

	if (streq(cp, "--version")) {
	    puts(PACKAGE_VERSION);
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

	} else if (streq(cp, "--input") || streq(cp, "-i")) {
	    input = *++argv;

	} else if (streq(cp, "--output") || streq(cp, "-o")) {
	    output = *++argv;

	} else if (streq(cp, "--debug") || streq(cp, "-d")) {
	    slaxDebug = TRUE;

	} else if (streq(cp, "--text-as-element") || streq(cp, "-t")) {
	    slaxSetTextAsElement(TRUE);

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
    slaxEnable(SLAX_FORCE);

    func(output, input, argv);

    return 0;
}
