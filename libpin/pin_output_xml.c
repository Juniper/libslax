/*
 * Copyright (c) 2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer, November 2026
 *
 * pin_output_xml -- the XML sink: writes output events as XML to a
 * FILE *.  The formatting (newlines, indentation, the debug comments)
 * is what pin_parse_emit_xml_cb() used to do.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "slaxconfig.h"
#include <libpsu/psucommon.h>
#include <libpin/pin_common.h>
#include <libpin/pin_output.h>

typedef struct pin_output_xml_s {
    FILE *pox_out;		/* Where we write */
    const char *pox_encoding;	/* Encoding for XML declaration (NULL=omit) */
    unsigned pox_indent;	/* Current indent amount */
    unsigned pox_incr;		/* Indent increment */
    unsigned pox_debug;		/* Write debug comments */
    unsigned pox_empty;		/* Open element has had no end_attribs */
    unsigned pox_last;		/* Last event, as a PIN_TYPE_* */
} pin_output_xml_t;

static void
pin_output_xml_debug (pin_output_xml_t *xp, unsigned type, const char *data)
{
    if (xp->pox_debug)
	fprintf(xp->pox_out, "<!-- [[%u]%s%s%s] -->", type, data ? "[" : "",
		data ?: "", data ? "]" : "");
}

static int
pin_output_xml_is_ws (const char *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
	if (!isspace((unsigned char) data[i]))
	    return 0;
    return 1;
}

static int
pin_output_xml_start (PIN_OUTPUT_START_ARGS)
{
    pin_output_xml_t *xp = opaque;

    pin_output_xml_debug(xp, PIN_TYPE_ROOT, NULL);

    if (xp->pox_encoding)
	fprintf(xp->pox_out, "<?xml version=\"1.0\" encoding=\"%s\"?>\n",
		xp->pox_encoding);
    if (xp->pox_debug)
	fprintf(xp->pox_out, "<!-- start of output>\n");

    xp->pox_last = PIN_TYPE_ROOT;
    return 0;
}

static int
pin_output_xml_open (PIN_OUTPUT_OPEN_ARGS)
{
    pin_output_xml_t *xp = opaque;

    pin_output_xml_debug(xp, PIN_TYPE_OPEN, name);

    if (xp->pox_last != PIN_TYPE_ROOT && xp->pox_last != PIN_TYPE_CLOSE
	    && xp->pox_last != PIN_TYPE_EMPTY)
	fprintf(xp->pox_out, "\n");

    fprintf(xp->pox_out, "%*s<%s%s%s", xp->pox_indent, "",
	    prefix ?: "", prefix ? ":" : "", name);
    xp->pox_indent += xp->pox_incr;
    xp->pox_empty = 1;
    xp->pox_last = PIN_TYPE_OPEN;
    return 0;
}

static int
pin_output_xml_ns (PIN_OUTPUT_NS_ARGS)
{
    pin_output_xml_t *xp = opaque;

    pin_output_xml_debug(xp, PIN_TYPE_NS, NULL);
    fprintf(xp->pox_out, " xmlns%s%s=\"%s\"",
	    prefix ? ":" : "", prefix ?: "", uri ?: "");
    xp->pox_last = PIN_TYPE_NS;
    return 0;
}

static int
pin_output_xml_attrib (PIN_OUTPUT_ATTRIB_ARGS)
{
    pin_output_xml_t *xp = opaque;

    pin_output_xml_debug(xp, PIN_TYPE_ATTRIB, value);
    fprintf(xp->pox_out, " %s%s%s=\"%s\"", prefix ?: "", prefix ? ":" : "",
	    name, value ?: "");
    xp->pox_last = PIN_TYPE_ATTRIB;
    return 0;
}

static int
pin_output_xml_attrib_raw (PIN_OUTPUT_ATTRIB_RAW_ARGS)
{
    pin_output_xml_t *xp = opaque;

    pin_output_xml_debug(xp, PIN_TYPE_ATSTR, text);
    fprintf(xp->pox_out, " %s", text ?: "");
    xp->pox_last = PIN_TYPE_ATSTR;
    return 0;
}

static int
pin_output_xml_end_attribs (PIN_OUTPUT_END_ATTRIBS_ARGS)
{
    pin_output_xml_t *xp = opaque;

    pin_output_xml_debug(xp, PIN_TYPE_EOL_ATTRIB, NULL);
    fprintf(xp->pox_out, ">");
    xp->pox_empty = 0;
    xp->pox_last = PIN_TYPE_EOL_ATTRIB;
    return 0;
}

static int
pin_output_xml_text (PIN_OUTPUT_TEXT_ARGS)
{
    pin_output_xml_t *xp = opaque;
    int unesc = (flags & PIN_OUTPUT_TEXT_UNESC) ? 1 : 0;

    if (data == NULL)
	data = "";

    pin_output_xml_debug(xp, unesc ? PIN_TYPE_UNESC : PIN_TYPE_TEXT, data);

    if (unesc) {
	/* Whitespace-only unescaped text is dropped, and isn't "last" */
	if (pin_output_xml_is_ws(data, len))
	    return 0;

	fprintf(xp->pox_out, "%s%.*s%s", xp->pox_debug ? "[unes]" : "",
		(int) len, data, xp->pox_debug ? "[/unes]": "");
	xp->pox_last = PIN_TYPE_UNESC;

    } else {
	/* XXX Text needs to be escaped */
	fprintf(xp->pox_out, "%s%.*s%s", xp->pox_debug ? "[text]" : "",
		(int) len, data, xp->pox_debug ? "[/text]": "");
	xp->pox_last = PIN_TYPE_TEXT;
    }

    return 0;
}

static int
pin_output_xml_comment (PIN_OUTPUT_COMMENT_ARGS)
{
    pin_output_xml_t *xp = opaque;

    pin_output_xml_debug(xp, PIN_TYPE_COMMENT, data);
    if (data)
	fprintf(xp->pox_out, "\n<!--%s-->", data);
    xp->pox_last = PIN_TYPE_COMMENT;
    return 0;
}

static int
pin_output_xml_pi (PIN_OUTPUT_PI_ARGS)
{
    pin_output_xml_t *xp = opaque;

    pin_output_xml_debug(xp, PIN_TYPE_PI, target);
    if (target) {
	if (data)
	    fprintf(xp->pox_out, "\n<?%s %s?>", target, data);
	else
	    fprintf(xp->pox_out, "\n<?%s?>", target);
    }
    xp->pox_last = PIN_TYPE_PI;
    return 0;
}

static int
pin_output_xml_close (PIN_OUTPUT_CLOSE_ARGS)
{
    pin_output_xml_t *xp = opaque;

    if (xp->pox_empty) {
	/* No children: write it as an empty element */
	pin_output_xml_debug(xp, PIN_TYPE_EOL_EMPTY, NULL);
	fprintf(xp->pox_out, "/>\n");
	xp->pox_empty = 0;
	pin_output_xml_debug(xp, PIN_TYPE_CLOSE, name);
	xp->pox_indent -= xp->pox_incr;

    } else {
	pin_output_xml_debug(xp, PIN_TYPE_CLOSE, name);
	xp->pox_indent -= xp->pox_incr;

	if (xp->pox_last == PIN_TYPE_UNESC || xp->pox_last == PIN_TYPE_TEXT)
	    fprintf(xp->pox_out, "</%s%s%s>\n",
		    prefix ?: "", prefix ? ":" : "", name);
	else
	    fprintf(xp->pox_out, "%*s</%s%s%s>\n", xp->pox_indent, "",
		    prefix ?: "", prefix ? ":" : "", name);
    }

    xp->pox_last = PIN_TYPE_CLOSE;
    return 0;
}

static int
pin_output_xml_finish (PIN_OUTPUT_FINISH_ARGS)
{
    pin_output_xml_t *xp = opaque;

    pin_output_xml_debug(xp, PIN_TYPE_EOF, NULL);
    if (xp->pox_debug)
	fprintf(xp->pox_out, "<!-- end of output>\n");
    xp->pox_last = PIN_TYPE_EOF;

    return ferror(xp->pox_out) ? -1 : 0;
}

/*
 * Errors go to stderr, not to the output file, in the same form as
 * pin_source_failure().
 */
static void
pin_output_xml_error (PIN_OUTPUT_ERROR_ARGS)
{
    static const char *sev_names[] = {
	[PIN_OUTPUT_SEV_MESSAGE] = "",
	[PIN_OUTPUT_SEV_WARNING] = "warning: ",
	[PIN_OUTPUT_SEV_ERROR] = "error: ",
	[PIN_OUTPUT_SEV_FATAL] = "error: ",
    };
    const char *sev_name = ((unsigned) sev < sizeof(sev_names) / sizeof(sev_names[0]))
	? sev_names[sev] : "";

    if (locp && locp->pol_filename) {
	if (locp->pol_line)
	    fprintf(stderr, "%s:%u:(%u): ", locp->pol_filename,
		    locp->pol_line, locp->pol_offset);
	else
	    fprintf(stderr, "%s:(%u): ", locp->pol_filename,
		    locp->pol_offset);
    }

    fprintf(stderr, "%s%s\n", sev_name, msg ?: "");
    fflush(stderr);
}

static void
pin_output_xml_free (PIN_OUTPUT_FREE_ARGS)
{
    free(opaque);
}

static const pin_output_funcs_t pin_output_xml_funcs = {
    .pof_version     = PIN_OUTPUT_FUNCS_VERSION,
    .pof_name        = "xml",
    .pof_start       = pin_output_xml_start,
    .pof_open        = pin_output_xml_open,
    .pof_ns          = pin_output_xml_ns,
    .pof_attrib      = pin_output_xml_attrib,
    .pof_attrib_raw  = pin_output_xml_attrib_raw,
    .pof_end_attribs = pin_output_xml_end_attribs,
    .pof_text        = pin_output_xml_text,
    .pof_comment     = pin_output_xml_comment,
    .pof_pi          = pin_output_xml_pi,
    .pof_close       = pin_output_xml_close,
    .pof_finish      = pin_output_xml_finish,
    .pof_error       = pin_output_xml_error,
    .pof_free        = pin_output_xml_free,
};

int
pin_output_xml_init (pin_output_t *sinkp, FILE *out,
		     const pin_output_xml_opts_t *optsp)
{
    pin_output_xml_t *xp = calloc(1, sizeof(*xp));
    if (xp == NULL)
	return -1;

    xp->pox_out = out;
    xp->pox_incr = 3;
    if (optsp) {
	xp->pox_encoding = optsp->poxo_encoding;
	xp->pox_incr = optsp->poxo_indent;
	xp->pox_debug = optsp->poxo_debug;
    }

    sinkp->po_funcs = &pin_output_xml_funcs;
    sinkp->po_opaque = xp;
    return 0;
}
