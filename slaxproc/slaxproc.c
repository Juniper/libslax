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
#include <getopt.h>             /* Include after xo.h for testing */

static slax_data_list_t plist;
static int nbparams;
static const char **params;

static slax_data_list_t mini_templates;
static xmlDocPtr mini_docp;
static char *mini_buffer;

static int options = XSLT_PARSE_OPTIONS;
static int opt_number = -1; 	/* Index into our long options array */

static char *opt_encoding;	/* Desired document encoding */
static char *opt_expression;	/* Expression to convert */
static char *opt_log_file;	/* Log file name */
static char *opt_show_select;   /* Expression (in script) to show */
static char *opt_show_variable; /* Variable (in script) to show */
static char *opt_trace_file;	/* Trace file name */
static char *opt_version;	/* Desired SLAX version */
static char *opt_xpath;		/* XPath expresion to match on */

static int opt_debugger;	/* Invoke the debugger */
static int opt_dump_tree;	/* Dump parsed element tree */
static int opt_empty_input;	/* Use an empty input file */
static int opt_html;		/* Parse input as HTML */
static int opt_ignore_arguments; /* Done processing arguments */
static int opt_indent;		/* Indent the output (pretty print) */
static int opt_indent_width;	/* Number of spaces to indent (format) */
static int opt_json_flags;	/* Flags for JSON conversion */
static int opt_json_tagging;	/* Tag JSON output */
static int opt_keep_text;	/* Don't add a rule to discard text values */
static int opt_width;		/* Output line width limit */
static int opt_partial;		/* Parse partial contents */
static int opt_slax_output;	/* Make output in SLAX format */
static int opt_want_parens;	/* They really want the parens */

static struct opts {
    int o_do_json_to_xml;
    int o_do_show_select;
    int o_do_show_variable;
    int o_do_xml_to_json;
    int o_do_xml_to_yaml;

    int o_dump_tree;
    int o_encoding;
    int o_expression;
    int o_ignore_arguments;
    int o_indent_width;
    int o_json_tagging;
    int o_keep_text;
    int o_width;
    int o_log_file;
    int o_no_json_types;
    int o_no_randomize;
    int o_no_tty;
    int o_want_parens;
} opts;

static struct option long_opts[] = {
    { "check", no_argument, NULL, 'c' },
    { "xpath", required_argument, NULL, 'X' },
    { "format", no_argument, NULL, 'F' },
    { "json-to-xml", no_argument, &opts.o_do_json_to_xml, 1 },
    { "run", no_argument, NULL, 'r' },
    { "show-select", required_argument, &opts.o_do_show_select, 1 },
    { "show-variable", required_argument, &opts.o_do_show_variable, 1 },
    { "slax-to-xslt", no_argument, NULL, 'x' },
    { "xml-to-json", no_argument, &opts.o_do_xml_to_json, 1 },
    { "xml-to-yaml", no_argument, &opts.o_do_xml_to_yaml, 1 },
    { "xslt-to-slax", no_argument, NULL, 's' },

    { "debug", no_argument, NULL, 'd' },
    { "dump-tree", no_argument, &opts.o_dump_tree, 1 },
    { "empty", no_argument, NULL, 'E' },
    { "encoding", required_argument, &opts.o_encoding, 1 },
    { "exslt", no_argument, NULL, 'e' },
    { "expression", required_argument, &opts.o_expression, 1 },
    { "help", no_argument, NULL, 'h' },
    { "html", no_argument, NULL, 'H' },
    { "ignore-arguments", no_argument, &opts.o_ignore_arguments, 1 },
    { "include", no_argument, NULL, 'I' },
    { "indent", no_argument, NULL, 'g' },
    { "indent-width", required_argument, &opts.o_indent_width, 1 },
    { "input", required_argument, NULL, 'i' },
    { "json-tagging", no_argument, &opts.o_json_tagging, 1 },
    { "keep-text", no_argument, NULL, 1 },
    { "lib", required_argument, NULL, 'L' },
    { "width", required_argument, &opts.o_width, 1 },
    { "log", required_argument, NULL, 'l' },
    { "mini-template", required_argument, NULL, 'm' },
    { "name", required_argument, NULL, 'n' },
    { "no-json-types", no_argument, &opts.o_no_json_types, 1 },
    { "no-randomize", no_argument, &opts.o_no_randomize, 1 },
    { "no-tty", no_argument, &opts.o_no_tty, 1 },
    { "output", required_argument, NULL, 'o' },
    { "param", required_argument, NULL, 'a' },
    { "partial", no_argument, NULL, 'p' },
    { "slax-output", no_argument, NULL, 'S' },
    { "trace", required_argument, NULL, 't' },
    { "verbose", no_argument, NULL, 'v' },
    { "version", no_argument, NULL, 'V' },
    { "want-parens", no_argument, &opts.o_want_parens, 1 },
    { "write-version", required_argument, NULL, 'w' },
    { "yydebug", no_argument, NULL, 'y' },
    { NULL, 0, NULL, 0 }
};

#define GET_FN_DONT	-1	/* Don't turn names into /dev/std{in,out} */
#define GET_FN_INPUT	0	/* Turn "-" into /dev/stdin */
#define GET_FN_OUTPUT	1	/* Turn "-" into /dev/stdout */
#define GET_FN_SCRIPT	2	/* Script name (can't be stdin) */

static const char *
get_filename (const char *filename, char ***pargv, int outp)
{
    if (filename == NULL) {
	filename = **pargv;
	if (filename)
	    *pargv += 1;
	else if (outp == GET_FN_SCRIPT)
	    return NULL;
	else
	    filename = "-";
    }

    if (outp != GET_FN_DONT && slaxFilenameIsStd(filename))
	filename = (outp == GET_FN_OUTPUT) ? "/dev/stdout" : "/dev/stdin";

    return filename;
}

static int
write_doc (FILE *outfile, xmlDocPtr docp, slaxWriterFlags_t flags)
{
    if (opt_partial)
	flags |= SWDF_PARTIAL;
    if (opt_want_parens)
	flags |= SWDF_WANT_PARENS;

    slax_writer_t *swp = slaxGetWriter((slaxWriterFunc_t) fprintf, outfile);
    if (swp) {
	slaxWriteSetFlags(swp, flags);
	if (opt_version)
	    slaxWriteSetVersion(swp, opt_version);
	if (opt_width)
	    slaxWriteSetWidth(swp, opt_width);

	return slaxWriteDocument(swp, docp);
    }

    return 0;
}

static int
do_format (const char *name, const char *output,
		 const char *input, char **argv)
{
    FILE *infile = NULL, *outfile;
    xmlDocPtr docp;

    if (mini_docp == NULL) {
	/* We'll honor "-n" as well as "-i" */
	if (input == NULL)
	    input = name;
	input = get_filename(input, &argv, GET_FN_INPUT);
    }

    output = get_filename(output, &argv, GET_FN_OUTPUT);

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

    write_doc(outfile, docp, 0);

    if (outfile != stdout)
	fclose(outfile);

    xmlFreeDoc(docp);

    return 0;
}

static int
do_slax_to_xslt (const char *name, const char *output,
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

    if (input == NULL)		/* Allow either -i or -n */
	input = name;

    if (mini_docp == NULL)
	input = get_filename(input, &argv, GET_FN_DONT);
    output = get_filename(output, &argv, GET_FN_DONT);

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

    if (input == NULL)		/* Allow either -i or -n */
	input = name;

    input = get_filename(input, &argv, GET_FN_DONT);
    output = get_filename(output, &argv, GET_FN_DONT);

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

    write_doc(outfile, docp, 0);

    if (outfile != stdout)
	fclose(outfile);

    xmlFreeDoc(docp);

    return 0;
}

static int
do_json_to_xml (const char *name, const char *output,
		 const char *input, char **argv)
{
    xmlDocPtr docp;
    FILE *outfile;

    if (input == NULL)		/* Allow either -i or -n */
	input = name;

    input = get_filename(input, &argv, GET_FN_INPUT);
    output = get_filename(output, &argv, GET_FN_DONT);

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

    if (input == NULL)		/* Allow either -i or -n */
	input = name;

    input = get_filename(input, &argv, GET_FN_INPUT);
    output = get_filename(output, &argv, GET_FN_DONT);

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

    if (input == NULL)		/* Allow either -i or -n */
	input = name;

    input = get_filename(input, &argv, GET_FN_INPUT);
    output = get_filename(output, &argv, GET_FN_DONT);

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
do_show_select (const char *name, const char *output,
                  const char *input, char **argv)
{
    FILE *infile, *outfile = NULL;
    xmlDocPtr docp, newdocp;
    xmlNodePtr root, newroot;
    xmlXPathContextPtr xpath_context;
    xmlXPathObjectPtr objp;
    xmlNodeSetPtr nsp;
    int i;

    if (input == NULL)		/* Allow either -i or -n */
	input = name;

    input = get_filename(input, &argv, GET_FN_DONT);
    output = get_filename(output, &argv, GET_FN_DONT);

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

    scriptname = get_filename(name, &argv, GET_FN_SCRIPT);
    if (!opt_empty_input)
	input = get_filename(input, &argv, GET_FN_DONT);
    output = get_filename(output, &argv, GET_FN_DONT);

    if (mini_docp) {
	scriptdoc = mini_docp;
	scriptname = "mini-template";
    } else {
	if (scriptname == NULL)
	    errx(1, "missing script filename");

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
	indoc = htmlReadFile(input, opt_encoding, options);
    else
	indoc = xmlReadFile(input, opt_encoding, options);
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
	    write_doc(outfile, res, SWDF_PARTIAL);
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

    if (input == NULL)		/* Allow either -i or -n */
	input = name;

    if (!opt_empty_input)
	input = get_filename(input, &argv, GET_FN_DONT);
    output = get_filename(output, &argv, GET_FN_DONT);

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
	indoc = htmlReadFile(input, opt_encoding, options);
    else
	indoc = xmlReadFile(input, opt_encoding, options);
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
	    write_doc(outfile, res, SWDF_PARTIAL);
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
	  const char *input, char **argv)
{
    xmlDocPtr scriptdoc;
    FILE *scriptfile;
    xsltStylesheetPtr script;

    /* We'll honor "-i" as well as "-n" */
    if (name == NULL)
	name = input;

    name = get_filename(name, &argv, GET_FN_INPUT);

    if (name == NULL)
	errx(1, "missing script filename");

    if (slaxFilenameIsStd(name))
	errx(1, "script file cannot be stdin");

    scriptfile = fopen(name, "r");
    if (scriptfile == NULL)
	err(1, "file open failed for '%s'", name);

    scriptdoc = slaxLoadFile(name, scriptfile, NULL, 0);
    if (scriptdoc == NULL)
	errx(1, "cannot parse: '%s'", name);
    if (scriptfile != stdin)
	fclose(scriptfile);

    script = xsltParseStylesheetDoc(scriptdoc);
    if (script == NULL || script->errors != 0)
	errx(1, "%d errors parsing script: '%s'",
	     script ? script->errors : 1, name);

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
"\t--encoding <name>: specifies the input document encoding\n"
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
"\t--want-parens: emit parens for control statements even for V1.3+\n"
"\t--width <num>: Target line length before wrapping (for --format)\n"
"\t--write-version <version> OR -w <version>: write in version\n"
"\nProject libslax home page: https://github.com/Juniper/libslax\n"
"\n");
}

static char *
check_arg (const char *name)
{
    char *arg = optarg;

    if (arg)
	return arg;

    if (opt_number > 0) {
	struct option *op = long_opts + opt_number;
	const char *opt = op ? op->name : "valid";
	errx(1, "missing %s argument for '--%s' option", name, opt);
    } else {
	errx(1, "invalid argument for '%s'", name);
    }

    return NULL;
}

int
main (int argc UNUSED, char **argv)
{
    const char *cp;
    const char *input = NULL, *output = NULL, *name = NULL;
    int (*func)(const char *, const char *, const char *, char **) = NULL;
    int use_exslt = FALSE;
    FILE *trace_fp = NULL;
    int randomize = 1;
    int logger = FALSE;
    slax_data_node_t *dnp;
    int i;
    unsigned ioflags = 0;

    slaxDataListInit(&plist);
    slaxDataListInit(&mini_templates);

    int rc;
    for (;;) {
	if (opt_ignore_arguments)
	    break;

	rc = getopt_long(argc, argv,
			 "a:cdEeFhHI:gi:l:L:m:n:o:prsSt:vVw:xXy",
			 long_opts, &opt_number);
	if (rc < 0)
	    break;

	if (logger) {
	    fprintf(stderr,
		    "getopt: rc %d/%02x optind %d, opt_number %d, arg '%s'\n",
		    rc, rc, optind, opt_number, optarg);
	}

        switch (rc) {
	/*
	 * We mirror the order in print_help() above:
	 * - first list the modes in alphabetically order
	 * - then list the options in alphabetically order
	 */

	case '?':
	case ':':
	    errx(1, "argument processing error");

	case 'c':
	    if (func)
		errx(1, "open one action allowed");
	    func = do_check;
	    break;

	case 'X':
	    if (func)
		errx(1, "open one action allowed");
	    func = do_xpath;
	    opt_xpath = check_arg("xpath expression");
	    break;

	case 'F':
	    if (func)
		errx(1, "open one action allowed");
	    func = do_format;
	    break;

	case 'r':
	    if (func)
		errx(1, "open one action allowed");
	    func = do_run;
	    break;

	case 'x':
	    if (func)
		errx(1, "open one action allowed");
	    func = do_slax_to_xslt;
	    break;

	case 's':
	    if (func)
		errx(1, "open one action allowed");
	    func = do_xslt_to_slax;
	    break;

/* Non-mode flags start here */

	case 'd':
	    opt_debugger = TRUE;
	    break;

	case 'E':
	    opt_empty_input = TRUE;
	    break;

	case 'e' :
	    use_exslt = TRUE;
	    break;

	case 'h':
	    print_help();
	    return -1;

	case 'H':
	    opt_html = TRUE;
	    break;

	case 'I':
	    slaxIncludeAdd(check_arg("include path"));
	    break;

	case 'g':
	    opt_indent = TRUE;
	    break;

	case 'i':
	    input = check_arg("input file");
	    break;

	case 'L':
	    slaxDynAdd(check_arg("library path"));
	    break;

	case 'l':
	    opt_log_file = check_arg("log file name");
	    break;

	case 'm':
	    slaxDataListAdd(&mini_templates, check_arg("template"));
	    break;

	case 'n':
	    name = check_arg("script name");
	    break;

	case 'o':
	    output = check_arg("output file name");
	    break;
	    
	case 'S':
	    opt_slax_output = TRUE;
	    break;

	case 'a':
	    {
		char *pname = check_arg("parameter name");
		char *eq = strchr(pname, '=');
		char *pvalue;
		size_t pnamelen;

		if (eq) {
		    pnamelen = eq - pname;
		    pvalue = eq + 1;

		} else {
		    pnamelen = strlen(pname);
		    optarg = argv[optind++];
		    pvalue = check_arg("parameter value");
		}

		char *tvalue;
		char quote;
		int plen;

		plen = strlen(pvalue);
		tvalue = alloca(plen + 3);

		quote = strrchr(pvalue, '\"') ? '\'' : '\"';
		tvalue[0] = quote;
		memcpy(tvalue + 1, pvalue, plen);
		tvalue[plen + 1] = quote;
		tvalue[plen + 2] = '\0';

		nbparams += 1;
		slaxDataListAddLenNul(&plist, pname, pnamelen);
		slaxDataListAddNul(&plist, tvalue);

		break;
	    }

	case 'p':
	    opt_partial = TRUE;
	    break;

	case 't':
	    opt_trace_file = check_arg("trace file name");
	    break;

	case 'v':
	    logger = TRUE;
	    break;

	case 'V':
	    print_version();
	    exit(0);

	case 'w':
	    opt_version = check_arg("version number");
	    break;

	case 'y':
	    slaxYyDebug = TRUE;
	    break;

	case 0:
	    if (opts.o_do_json_to_xml) {
		if (func)
		    errx(1, "open one action allowed");
		func = do_json_to_xml;

	    } else if (opts.o_do_show_select) {
		if (func)
		    errx(1, "open one action allowed");
		func = do_show_select;
		opt_show_select = check_arg("selection");

	    } else if (opts.o_do_show_variable) {
		if (func)
		    errx(1, "open one action allowed");
		func = do_show_variable;
		opt_show_variable = check_arg("variable name");

	    } else if (opts.o_do_xml_to_json) {
		if (func)
		    errx(1, "open one action allowed");
		func = do_xml_to_json;

	    } else if (opts.o_do_xml_to_yaml) {
		if (func)
		    errx(1, "open one action allowed");
		func = do_xml_to_yaml;

/* Non-mode flags start here */

	    } else if (opts.o_dump_tree) {
		opt_dump_tree = TRUE;

	    } else if (opts.o_encoding) {
		opt_encoding = check_arg("text encoding");

	    } else if (opts.o_expression) {
		opt_expression = check_arg("expression");

	    } else if (opts.o_ignore_arguments) {
		opt_ignore_arguments = TRUE;

	    } else if (opts.o_indent_width) {
		char *str = check_arg("indent width");
		if (str)
		    opt_indent_width = atoi(str);

	    } else if (opts.o_json_tagging) {
		opt_json_tagging = TRUE;

	    } else if (opts.o_no_json_types) {
		opt_json_flags |= SDF_NO_TYPES;

	    } else if (opts.o_keep_text) {
		opt_keep_text = TRUE;

	    } else if (opts.o_width) {
		char *str = check_arg("width");
		if (str)
		    opt_width = atoi(str);

	    } else if (opts.o_no_randomize) {
		randomize = 0;

	    } else if (opts.o_no_tty) {
		ioflags |= SIF_NO_TTY;

	    } else if (opts.o_want_parens) {
		opt_want_parens = TRUE;

            } else {
                print_help();
                return 1;
            }
	    break;

	default:
	    errx(1, "unknown option '%c' (%d)", rc, rc);
	}

	bzero(&opts, sizeof(opts));
	opt_number = -1;
    }

    argc -= optind;
    argv += optind;

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

    if (opt_trace_file) {
	if (slaxFilenameIsStd(opt_trace_file))
	    trace_fp = stderr;
	else {
	    trace_fp = fopen(opt_trace_file, "w");
	    if (trace_fp == NULL)
		err(1, "could not open trace file: '%s'", opt_trace_file);
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
