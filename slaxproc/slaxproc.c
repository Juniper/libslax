/*
 * Copyright (c) 2006-2013, Juniper Networks, Inc.
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

#include <libxslt/transform.h>
#include <libxml/HTMLparser.h>
#include <libxslt/xsltutils.h>
#include <libxml/globals.h>
#include <libxml/xpathInternals.h>
#include <libexslt/exslt.h>
#include <libslax/slaxdyn.h>
#include <libslax/slaxdata.h>
#include <libslax/jsonlexer.h>
#include <libslax/jsonwriter.h>
#include <libslax/yamlwriter.h>

#include <err.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>

static slax_data_list_t plist;
static int nbparams;
static const char **params;

static slax_data_list_t mini_templates;
static xmlDocPtr mini_docp;
static char *mini_buffer;

static int options = XSLT_PARSE_OPTIONS;
static char *encoding;		/* Desired document encoding */
static char *opt_version;	/* Desired SLAX version */
static char *opt_expression;	/* Expression to convert */
static char **opt_args;
static char *opt_show_variable; /* Variable (in script) to show */
static char *opt_show_select;   /* Expression (in script) to show */
static char *opt_xpath;		/* XPath expresion to match on */

static int opt_html;		/* Parse input as HTML */
static int opt_indent;		/* Indent the output (pretty print) */
static int opt_indent_width;	/* Number of spaces to indent (format) */
static int opt_partial;		/* Parse partial contents */
static int opt_debugger;	/* Invoke the debugger */
static int opt_empty_input;	/* Use an empty input file */
static int opt_slax_output;	/* Make output in SLAX format */
static int opt_json_tagging;	/* Tag JSON output */
static int opt_json_flags;	/* Flags for JSON conversion */
static int opt_keep_text;	/* Don't add a rule to discard text values */
static int opt_dump_tree;	/* Dump parsed element tree */

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
    FILE *infile = NULL, *outfile;
    xmlDocPtr docp;

    if (mini_docp == NULL)
	input = get_filename(input, &argv, -1);
    output = get_filename(output, &argv, -1);

    if (mini_docp)
	docp = mini_docp;
    else {
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

	if (opt_dump_tree)
	    slaxDumpTree(docp->children, "", 0);
    }

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
    FILE *infile = NULL, *outfile;
    xmlDocPtr docp;

    if (opt_expression) {
	char *res = slaxConvertExpression(opt_expression, TRUE);
	if (res) {
	    printf("%s\n", res);
	    xmlFree(res);
	}
	return res ? 0 : -1;
    }

    if (mini_docp == NULL)
	input = get_filename(input, &argv, -1);
    output = get_filename(output, &argv, -1);

    if (mini_docp)
	docp = mini_docp;
    else {
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

	if (opt_dump_tree)
	    slaxDumpTree(docp->children, "", 0);
    }

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

    if (opt_expression) {
	char *res = slaxConvertExpression(opt_expression, FALSE);
	if (res) {
	    printf("%s\n", res);
	    xmlFree(res);
	}
	return res ? 0 : -1;
	return 0;
    }

    input = get_filename(input, &argv, -1);
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

static int
do_json_to_xml (const char *name UNUSED, const char *output,
		 const char *input, char **argv)
{
    xmlDocPtr docp;
    FILE *outfile;

    input = get_filename(input, &argv, 0);
    output = get_filename(output, &argv, -1);

    docp = slaxJsonFileToXml(input, NULL, opt_json_flags);
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

    slaxDumpToFd(fileno(outfile), docp, opt_partial);

    if (outfile != stdout)
	fclose(outfile);

    xmlFreeDoc(docp);

    return 0;
}

static int
do_xml_to_json (const char *name UNUSED, const char *output,
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

    slaxJsonWriteDoc((slaxWriterFunc_t) fprintf, outfile, docp,
		     opt_indent ? JWF_PRETTY : 0);

    if (outfile != stdout)
	fclose(outfile);

    xmlFreeDoc(docp);

    return 0;
}

static int
do_xml_to_yaml (const char *name UNUSED, const char *output,
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

    slaxYamlWriteDoc((slaxWriterFunc_t) fprintf, outfile, docp,
		     opt_indent ? JWF_PRETTY : 0);

    if (outfile != stdout)
	fclose(outfile);

    xmlFreeDoc(docp);

    return 0;
}

static int
do_show_select (const char *name UNUSED, const char *output,
                  const char *input, char **argv)
{
    FILE *infile, *outfile = NULL;
    xmlDocPtr docp, newdocp;
    xmlNodePtr root, newroot;
    xmlXPathContextPtr xpath_context;
    xmlXPathObjectPtr objp;
    xmlNodeSetPtr nsp;
    int i;

    input = get_filename(input, &argv, -1);
    output = get_filename(output, &argv, -1);

    if (slaxFilenameIsStd(input))
	infile = stdin;
    else {
	infile = fopen(input, "r");
	if (infile == NULL)
	    err(1, "file open failed for '%s'", input);
    }

    docp = slaxLoadFile(input, infile, xmlDictCreate(), opt_partial);
    if (docp == NULL)
	errx(1, "cannot parse file: '%s'", input);

    if (infile != stdin)
	fclose(infile);

    root = xmlDocGetRootElement(docp);
    if (root == NULL)
        err(1, "invalid document (no root node)");

    /* Setup a context in which the expression can be evaluated */
    xpath_context = xmlXPathNewContext(docp);
    if (xpath_context == NULL)
        err(1, "xpath context creation failed");

    xmlXPathRegisterNs(xpath_context, (const xmlChar *) XSL_PREFIX,
                       (const xmlChar *) XSL_URI);

    /* Evaluate with our document's root node as "." */
    xpath_context->node = root;

    newdocp = xmlNewDoc((const xmlChar *) XML_DEFAULT_VERSION);
    if (newdocp == NULL)
        err(1, "document context creation failed");

    newdocp->standalone = 1;
    newdocp->dict = xmlDictCreate();

    newroot = xmlNewDocNode(newdocp, NULL, (const xmlChar *) "select", NULL);
    if (newroot == NULL)
        err(1, "document context creation failed");

    xmlDocSetRootElement(newdocp, newroot);

    if (output == NULL || slaxFilenameIsStd(output))
        outfile = stdout;
    else {
        outfile = fopen(output, "w");
        if (outfile == NULL)
            err(1, "could not open file: '%s'", output);
    }

    objp = xmlXPathEvalExpression((const xmlChar *) opt_show_select,
                                  xpath_context);
    if (objp->type == XPATH_NODESET) {
        nsp = objp->nodesetval;
        if (nsp && nsp->nodeNr > 0) {

            for (i = 0; i < nsp->nodeNr; i++) {
		xmlNodePtr newp = xmlDocCopyNode(nsp->nodeTab[i], newdocp, 1);
		if (newp)
		    xmlAddChild(newroot, newp);
            }
        }
    }

    slaxDumpToFd(fileno(outfile), newdocp, opt_partial);

    if (outfile && outfile != stdout)
	fclose(outfile);

    xmlXPathFreeContext(xpath_context);
    xmlFreeDoc(newdocp);
    xmlFreeDoc(docp);

    return 0;
}

static int
do_show_variable (const char *name UNUSED, const char *output,
                  const char *input, char **argv)
{
    char select_argument[BUFSIZ];

    if (opt_show_variable == NULL)
        errx(1, "no variable name");

    if (opt_show_variable[0] == '$')
        opt_show_variable += 1;

    snprintf(select_argument, sizeof(select_argument),
            "xsl:variable[@name='%s']", opt_show_variable);
    opt_show_select = select_argument;

    return do_show_select(name, output, input, argv);
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

    if (mini_docp) {
	scriptdoc = mini_docp;
	scriptname = "mini-template";
    } else {
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
    }

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
	if (mini_docp)
	    slaxDebugSetScriptBuffer(mini_buffer);

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

static const char xpath_script[] = "\
version " SLAX_VERSION ";\n\
main <results> { copy-of %s; }\n";

static int
do_xpath (const char *name UNUSED, const char *output,
	  const char *input, char **argv)
{
    const char *scriptname = "xpath";
    xmlDocPtr scriptdoc;
    FILE *outfile;
    xmlDocPtr indoc;
    xsltStylesheetPtr script;
    xmlDocPtr res = NULL;
    char *buf;
    int len;

    if (!opt_empty_input)
	input = get_filename(input, &argv, -1);
    output = get_filename(output, &argv, -1);

    len = strlen(xpath_script) + strlen(opt_xpath) + 1;
    buf = malloc(len);
    if (buf == NULL)
	errx(1, "out of memory");

    snprintf(buf, len, xpath_script, opt_xpath);

    scriptdoc = slaxLoadBuffer(scriptname, buf, NULL, 0);
    if (scriptdoc == NULL)
	errx(1, "cannot parse: '%s'", opt_xpath);

    script = xsltParseStylesheetDoc(scriptdoc);
    if (script == NULL || script->errors != 0)
	errx(1, "%d errors parsing script: '%s'",
	     script ? script->errors : 1, opt_xpath);

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

static const char mini_prefix[] = "version " SLAX_VERSION ";\n";
static const char mini_discard[] = "match text() { }\n";

static void
build_mini_template (void)
{
    size_t len = sizeof(mini_prefix);
    slax_data_node_t *dnp;
    char *buf, *cp;

    SLAXDATALIST_FOREACH(dnp, &mini_templates) {
	len += dnp->dn_len + 2;
    }
    if (!opt_keep_text)
	len += sizeof(mini_discard);

    buf = xmlMalloc(len + 1);
    if (buf == NULL)
	errx(1, "could not allocate mini-template");

    memcpy(buf, mini_prefix, sizeof(mini_prefix));
    cp = buf + sizeof(mini_prefix) - 1;

    SLAXDATALIST_FOREACH(dnp, &mini_templates) {
	memcpy(cp, dnp->dn_data, dnp->dn_len);
	cp += dnp->dn_len - 1;
	*cp++ = '\n';
	*cp++ = '\n';
    }
    if (!opt_keep_text) {
	memcpy(cp, mini_discard, sizeof(mini_discard));
	cp += sizeof(mini_discard) - 1;
    }

    /*
     * We need to copy our buffer _before_ calling the parser,
     * since the parser frees the buffer for us.
     */
    mini_buffer = strdup(buf);

    mini_docp = slaxLoadBuffer("mini-template", buf, NULL, FALSE);
    if (mini_docp == NULL)
	errx(1, "parse error for mini-template");
}

static void
print_version (void)
{
    printf("version " SLAX_VERSION ";\n");
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
    fprintf(stderr,
"Usage: slaxproc [mode] [options] [script] [files]\n"
"    Modes:\n"
"\t--check OR -c: check syntax and content for a SLAX script\n"
"\t--format OR -F: format (pretty print) a SLAX script\n"
"\t--json-to-xml: Turn JSON data into XML\n"
"\t--run OR -r: run a SLAX script (the default mode)\n"
"\t--show-select: show XPath selection from the input document\n"
"\t--show-variable: show contents of a global variable\n"
"\t--slax-to-xslt OR -x: turn SLAX into XSLT\n"
"\t--xml-to-json: turn XML into JSON\n"
"\t--xml-to-yaml: turn XML into YAML\n"
"\t--xpath <xpath> OR -X <xpath>: select XPath data from input\n"
"\t--xslt-to-slax OR -s: turn XSLT into SLAX\n"
"\n"
"    Options:\n"
"\t--debug OR -d: enable the SLAX/XSLT debugger\n"
"\t--empty OR -E: give an empty document for input\n"
"\t--exslt OR -e: enable the EXSLT library\n"
"\t--expression <expr>: convert an expression\n"
"\t--help OR -h: display this help message\n"
"\t--html OR -H: Parse input data as HTML\n"
"\t--ignore-arguments: Do not process any further arguments\n"
"\t--include <dir> OR -I <dir>: search directory for includes/imports\n"
"\t--indent OR -g: indent output ala output-method/indent\n"
"\t--indent-width <num>: Number of spaces to indent (for --format)\n"
"\t--input <file> OR -i <file>: take input from the given file\n"
"\t--json-tagging: tag json-style input with the 'json' attribute\n"
"\t--keep-text: mini-templates should not discard text\n"
"\t--lib <dir> OR -L <dir>: search directory for extension libraries\n"
"\t--log <file>: use given log file\n"
"\t--mini-template <code> OR -m <code>: wrap template code in a script\n"
"\t--name <file> OR -n <file>: read the script from the given file\n"
"\t--no-json-types: do not insert 'type' attribute for --json-to-xml\n"
"\t--no-randomize: do not initialize the random number generator\n"
"\t--no-tty: do not fall back to stdin for tty io\n"
"\t--output <file> OR -o <file>: make output into the given file\n"
"\t--param <name> <value> OR -a <name> <value>: pass parameters\n"
"\t--partial OR -p: allow partial SLAX input to --slax-to-xslt\n"
"\t--slax-output OR -S: Write the result using SLAX-style XML (braces, etc)\n"
"\t--trace <file> OR -t <file>: write trace data to a file\n"
"\t--verbose OR -v: enable debugging output (slaxLog())\n"
"\t--version OR -V: show version information (and exit)\n"
"\t--write-version <version> OR -w <version>: write in version\n"
"\nProject libslax home page: https://github.com/Juniper/libslax\n"
"\n");
}

static char *
check_arg (const char *name, char ***argvp)
{
    char *opt, *arg;

    opt = **argvp;
    *argvp += 1;
    arg = **argvp;

    if (arg == NULL)
	errx(1, "missing %s argument for '%s' option", name, opt);

    return arg;
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
    slaxDataListInit(&mini_templates);

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

	} else if (streq(cp, "--xpath") || streq(cp, "-X")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_xpath;
	    opt_xpath = check_arg("xpath expression", &argv);

	} else if (streq(cp, "--format") || streq(cp, "-F")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_format;

	} else if (streq(cp, "--json-to-xml")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_json_to_xml;

	} else if (streq(cp, "--run") || streq(cp, "-r")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_run;

	} else if (streq(cp, "--show-select")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_show_select;
            opt_show_select = check_arg("select", &argv);

	} else if (streq(cp, "--show-variable")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_show_variable;
            opt_show_variable = check_arg("variable name", &argv);

	} else if (streq(cp, "--slax-to-xslt") || streq(cp, "-x")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_slax_to_xslt;

	} else if (streq(cp, "--xml-to-json")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_xml_to_json;

	} else if (streq(cp, "--xml-to-yaml")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_xml_to_yaml;

	} else if (streq(cp, "--xslt-to-slax") || streq(cp, "-s")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_xslt_to_slax;

/* Non-mode flags start here */
	} else if (streq(cp, "--debug") || streq(cp, "-d")) {
	    opt_debugger = TRUE;

	} else if (streq(cp, "--dump-tree")) {
	    opt_dump_tree = TRUE;

	} else if (streq(cp, "--empty") || streq(cp, "-E")) {
	    opt_empty_input = TRUE;

	} else if (streq(cp, "--exslt") || streq(cp, "-e")) {
	    use_exslt = TRUE;

	} else if (streq(cp, "--expression")) {
	    opt_expression = check_arg("expression", &argv);


	} else if (streq(cp, "--help") || streq(cp, "-h")) {
	    print_help();
	    return -1;

	} else if (streq(cp, "--html") || streq(cp, "-H")) {
	    opt_html = TRUE;

	} else if (streq(cp, "--ignore-arguments")) {
	    opt_ignore_arguments = TRUE;
	    break;

	} else if (streq(cp, "--include") || streq(cp, "-I")) {
	    slaxIncludeAdd(check_arg("include path", &argv));

	} else if (streq(cp, "--indent") || streq(cp, "-g")) {
	    opt_indent = TRUE;

	} else if (streq(cp, "--indent-width")) {
	    char *str = check_arg("width", &argv);
	    if (str)
		opt_indent_width = atoi(str);

	} else if (streq(cp, "--input") || streq(cp, "-i")) {
	    input = check_arg("input file", &argv);

	} else if (streq(cp, "--json-tagging")) {
	    opt_json_tagging = TRUE;

	} else if (streq(cp, "--keep-text")) {
	    opt_keep_text = TRUE;

	} else if (streq(cp, "--lib") || streq(cp, "-L")) {
	    slaxDynAdd(check_arg("library path", &argv));

	} else if (streq(cp, "--log") || streq(cp, "-l")) {
	    opt_log_file = check_arg("log file name", &argv);

	} else if (streq(cp, "--mini-template") || streq(cp, "-m")) {
	    slaxDataListAdd(&mini_templates, check_arg("template", &argv));

	} else if (streq(cp, "--name") || streq(cp, "-n")) {
	    name = check_arg("script name", &argv);

	} else if (streq(cp, "--no-json-types")) {
	    opt_json_flags |= SDF_NO_TYPES;

	} else if (streq(cp, "--no-randomize")) {
	    randomize = 0;

	} else if (streq(cp, "--no-tty")) {
	    ioflags |= SIF_NO_TTY;

	} else if (streq(cp, "--output") || streq(cp, "-o")) {
	    output = check_arg("output file name", &argv);

	} else if (streq(cp, "--param") || streq(cp, "-a")) {
	    char *pname = check_arg("parameter name", &argv);
	    char *pvalue = check_arg("parameter value", &argv);
	    char *tvalue;
	    char quote;
	    int plen;

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
	    trace_file = check_arg("trace file name", &argv);

	} else if (streq(cp, "--verbose") || streq(cp, "-v")) {
	    logger = TRUE;

	} else if (streq(cp, "--version") || streq(cp, "-V")) {
	    print_version();
	    exit(0);

	} else if (streq(cp, "--write-version") || streq(cp, "-w")) {
	    opt_version = check_arg("version number", &argv);

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

    if (opt_indent_width)
	slaxSetIndent(opt_indent_width);

    if (opt_json_tagging)
	slaxJsonTagging(TRUE);

    if (opt_log_file) {
	FILE *fp = fopen(opt_log_file, "w");
	if (fp == NULL)
	    err(1, "could not open log file: '%s'", opt_log_file);

	slaxLogEnable(TRUE);
	slaxLogToFile(fp);

    } else if (logger) {
	slaxLogEnable(TRUE);
    }

    if (use_exslt) {
	exsltRegisterAll();
	slaxDynMarkExslt();
    }

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

    if (!TAILQ_EMPTY(&mini_templates))
	build_mini_template();

    func(name, output, input, argv);

    if (trace_fp && trace_fp != stderr)
	fclose(trace_fp);

    slaxDynClean();
    xsltCleanupGlobals();
    xmlCleanupParser();

    exit(slaxGetExitCode());
}
