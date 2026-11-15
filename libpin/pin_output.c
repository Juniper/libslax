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
 * pin_output -- the writer (engine side of a sink), the fan-out sink
 * and the destination stack.  See pin_output.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "slaxconfig.h"
#include <libpin/pin_output.h>

#define PIN_OUTPUT_CALL(_wrp, _fn, _args...)				\
    (((_wrp)->pw_sink.po_funcs && (_wrp)->pw_sink.po_funcs->_fn)		\
     ? (_wrp)->pw_sink.po_funcs->_fn((_wrp)->pw_sink.po_opaque, ## _args) \
     : 0)

/* Record a failure; once failed, a writer stays failed */
static int
pin_output_rc (pin_output_writer_t *wrp, int rc)
{
    if (rc < 0)
	wrp->pw_failed = 1;
    return rc;
}

void
pin_output_free (pin_output_t *sinkp)
{
    if (sinkp == NULL)
	return;

    if (sinkp->po_funcs && sinkp->po_funcs->pof_free)
	sinkp->po_funcs->pof_free(sinkp->po_opaque);

    sinkp->po_funcs = NULL;
    sinkp->po_opaque = NULL;
}

void
pin_output_writer_init (pin_output_writer_t *wrp, const pin_output_t *sinkp)
{
    memset(wrp, 0, sizeof(*wrp));
    if (sinkp)
	wrp->pw_sink = *sinkp;
}

void
pin_output_writer_cleanup (pin_output_writer_t *wrp)
{
    free(wrp->pw_names);
    free(wrp->pw_offsets);
    memset(wrp, 0, sizeof(*wrp));
}

/* Append one string (NULL is stored as an empty string) to pw_names */
static int
pin_output_names_add (pin_output_writer_t *wrp, const char *str)
{
    size_t len = str ? strlen(str) : 0;

    if (wrp->pw_names_len + len + 1 > wrp->pw_names_size) {
	size_t newsize = wrp->pw_names_size ? wrp->pw_names_size * 2 : 256;
	while (newsize < wrp->pw_names_len + len + 1)
	    newsize *= 2;

	char *np = realloc(wrp->pw_names, newsize);
	if (np == NULL)
	    return -1;

	wrp->pw_names = np;
	wrp->pw_names_size = newsize;
    }

    if (len)
	memcpy(wrp->pw_names + wrp->pw_names_len, str, len);
    wrp->pw_names[wrp->pw_names_len + len] = '\0';
    wrp->pw_names_len += len + 1;

    return 0;
}

/* Turn a stored empty string back into NULL */
static const char *
pin_output_names_get (const char *cp)
{
    return (cp && *cp) ? cp : NULL;
}

/* Send end_attribs if the open element hasn't had it yet */
static int
pin_output_content (pin_output_writer_t *wrp)
{
    if (!wrp->pw_attribs_open)
	return 0;

    wrp->pw_attribs_open = 0;
    return pin_output_rc(wrp, PIN_OUTPUT_CALL(wrp, pof_end_attribs));
}

int
pin_output_start (pin_output_writer_t *wrp)
{
    return pin_output_rc(wrp, PIN_OUTPUT_CALL(wrp, pof_start));
}

int
pin_output_open (pin_output_writer_t *wrp, const char *prefix,
		 const char *uri, const char *name)
{
    if (pin_output_content(wrp) < 0)
	return -1;

    if (wrp->pw_depth + 1 > wrp->pw_offsets_size) {
	unsigned newsize = wrp->pw_offsets_size ? wrp->pw_offsets_size * 2 : 32;
	size_t *op = realloc(wrp->pw_offsets, newsize * sizeof(*op));
	if (op == NULL)
	    return pin_output_rc(wrp, -1);

	wrp->pw_offsets = op;
	wrp->pw_offsets_size = newsize;
    }

    size_t start = wrp->pw_names_len;
    if (pin_output_names_add(wrp, prefix) < 0
	    || pin_output_names_add(wrp, uri) < 0
	    || pin_output_names_add(wrp, name) < 0) {
	wrp->pw_names_len = start;
	return pin_output_rc(wrp, -1);
    }

    wrp->pw_offsets[wrp->pw_depth] = start;
    wrp->pw_depth += 1;
    wrp->pw_attribs_open = 1;

    return pin_output_rc(wrp, PIN_OUTPUT_CALL(wrp, pof_open,
					      prefix, uri, name));
}

int
pin_output_ns (pin_output_writer_t *wrp, const char *prefix, const char *uri)
{
    if (!wrp->pw_attribs_open)
	return pin_output_rc(wrp, -1); /* Too late for this element */

    return pin_output_rc(wrp, PIN_OUTPUT_CALL(wrp, pof_ns, prefix, uri));
}

int
pin_output_attrib (pin_output_writer_t *wrp, const char *prefix,
		   const char *uri, const char *name, const char *value)
{
    if (!wrp->pw_attribs_open)
	return pin_output_rc(wrp, -1); /* Too late for this element */

    return pin_output_rc(wrp, PIN_OUTPUT_CALL(wrp, pof_attrib,
					      prefix, uri, name, value));
}

int
pin_output_attrib_raw (pin_output_writer_t *wrp, const char *text)
{
    if (!wrp->pw_attribs_open)
	return pin_output_rc(wrp, -1); /* Too late for this element */

    return pin_output_rc(wrp, PIN_OUTPUT_CALL(wrp, pof_attrib_raw, text));
}

/*
 * Explicit end of attributes.  Normally the first child sends it; this
 * is for a caller that knows the element will have children (or that
 * must match output that always ended the start tag).
 */
int
pin_output_end_attribs (pin_output_writer_t *wrp)
{
    return pin_output_content(wrp);
}

int
pin_output_text (pin_output_writer_t *wrp, const char *data, size_t len,
		 unsigned flags)
{
    if (pin_output_content(wrp) < 0)
	return -1;

    return pin_output_rc(wrp, PIN_OUTPUT_CALL(wrp, pof_text,
					      data, len, flags));
}

int
pin_output_comment (pin_output_writer_t *wrp, const char *data)
{
    if (pin_output_content(wrp) < 0)
	return -1;

    return pin_output_rc(wrp, PIN_OUTPUT_CALL(wrp, pof_comment, data));
}

int
pin_output_pi (pin_output_writer_t *wrp, const char *target,
	       const char *data)
{
    if (pin_output_content(wrp) < 0)
	return -1;

    return pin_output_rc(wrp, PIN_OUTPUT_CALL(wrp, pof_pi, target, data));
}

int
pin_output_close (pin_output_writer_t *wrp)
{
    if (wrp->pw_depth == 0)
	return pin_output_rc(wrp, -1); /* Nothing open */

    /* A close with no children gets no end_attribs */
    wrp->pw_attribs_open = 0;
    wrp->pw_depth -= 1;

    size_t start = wrp->pw_offsets[wrp->pw_depth];
    const char *prefix = wrp->pw_names + start;
    const char *uri = prefix + strlen(prefix) + 1;
    const char *name = uri + strlen(uri) + 1;

    int rc = PIN_OUTPUT_CALL(wrp, pof_close, pin_output_names_get(prefix),
			     pin_output_names_get(uri),
			     pin_output_names_get(name));

    wrp->pw_names_len = start;
    return pin_output_rc(wrp, rc);
}

int
pin_output_finish (pin_output_writer_t *wrp)
{
    int rc = 0;

    /* Close anything left open, so the sink sees a balanced stream */
    while (wrp->pw_depth > 0) {
	if (pin_output_close(wrp) < 0)
	    rc = -1;
    }

    if (pin_output_rc(wrp, PIN_OUTPUT_CALL(wrp, pof_finish)) < 0)
	rc = -1;

    return rc;
}

void
pin_output_error (pin_output_writer_t *wrp, pin_output_severity_t sev,
		  const pin_output_loc_t *locp, const char *fmt, ...)
{
    char buf[2048];
    va_list vap;

    va_start(vap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, vap);
    va_end(vap);

    if (wrp->pw_sink.po_funcs && wrp->pw_sink.po_funcs->pof_error)
	wrp->pw_sink.po_funcs->pof_error(wrp->pw_sink.po_opaque, sev,
				       locp, buf);
    else
	fprintf(stderr, "%s\n", buf);
}

/*
 * The fan-out sink.  Each callback passes the event to every sink in
 * the list; the result is -1 if any of them fails.
 */
typedef struct pin_output_fanout_s {
    pin_output_t *pfo_sinks;	/* Sinks, in order */
    unsigned pfo_count;		/* Sinks in use */
    unsigned pfo_size;		/* Sinks allocated */
} pin_output_fanout_t;

/*
 * Hand the event on to every sink.  _pass is the callback's _PASS
 * list (its arguments after "opaque", with a leading comma).
 */
#define PIN_OUTPUT_FANOUT(_opaque, _fn, _pass)				\
    do {								\
	pin_output_fanout_t *_fop = (_opaque);				\
	int _rc = 0;							\
	for (unsigned _i = 0; _i < _fop->pfo_count; _i++) {		\
	    pin_output_t *_sp = &_fop->pfo_sinks[_i];			\
	    if (_sp->po_funcs && _sp->po_funcs->_fn				\
		    && _sp->po_funcs->_fn(_sp->po_opaque _pass) < 0)	\
		_rc = -1;						\
	}								\
	return _rc;							\
    } while (0)

static int
pin_output_fanout_start (PIN_OUTPUT_START_ARGS)
{
    PIN_OUTPUT_FANOUT(opaque, pof_start, PIN_OUTPUT_START_PASS);
}

static int
pin_output_fanout_open (PIN_OUTPUT_OPEN_ARGS)
{
    PIN_OUTPUT_FANOUT(opaque, pof_open, PIN_OUTPUT_OPEN_PASS);
}

static int
pin_output_fanout_ns (PIN_OUTPUT_NS_ARGS)
{
    PIN_OUTPUT_FANOUT(opaque, pof_ns, PIN_OUTPUT_NS_PASS);
}

static int
pin_output_fanout_attrib (PIN_OUTPUT_ATTRIB_ARGS)
{
    PIN_OUTPUT_FANOUT(opaque, pof_attrib, PIN_OUTPUT_ATTRIB_PASS);
}

static int
pin_output_fanout_attrib_raw (PIN_OUTPUT_ATTRIB_RAW_ARGS)
{
    PIN_OUTPUT_FANOUT(opaque, pof_attrib_raw, PIN_OUTPUT_ATTRIB_RAW_PASS);
}

static int
pin_output_fanout_end_attribs (PIN_OUTPUT_END_ATTRIBS_ARGS)
{
    PIN_OUTPUT_FANOUT(opaque, pof_end_attribs, PIN_OUTPUT_END_ATTRIBS_PASS);
}

static int
pin_output_fanout_text (PIN_OUTPUT_TEXT_ARGS)
{
    PIN_OUTPUT_FANOUT(opaque, pof_text, PIN_OUTPUT_TEXT_PASS);
}

static int
pin_output_fanout_comment (PIN_OUTPUT_COMMENT_ARGS)
{
    PIN_OUTPUT_FANOUT(opaque, pof_comment, PIN_OUTPUT_COMMENT_PASS);
}

static int
pin_output_fanout_pi (PIN_OUTPUT_PI_ARGS)
{
    PIN_OUTPUT_FANOUT(opaque, pof_pi, PIN_OUTPUT_PI_PASS);
}

static int
pin_output_fanout_close (PIN_OUTPUT_CLOSE_ARGS)
{
    PIN_OUTPUT_FANOUT(opaque, pof_close, PIN_OUTPUT_CLOSE_PASS);
}

static int
pin_output_fanout_finish (PIN_OUTPUT_FINISH_ARGS)
{
    PIN_OUTPUT_FANOUT(opaque, pof_finish, PIN_OUTPUT_FINISH_PASS);
}

/*
 * An error goes to every sink that takes errors; if none does, it
 * goes to stderr, so it is never lost.
 */
static void
pin_output_fanout_error (PIN_OUTPUT_ERROR_ARGS)
{
    pin_output_fanout_t *fop = opaque;
    int sent = 0;

    for (unsigned i = 0; i < fop->pfo_count; i++) {
	pin_output_t *sp = &fop->pfo_sinks[i];
	if (sp->po_funcs && sp->po_funcs->pof_error) {
	    sp->po_funcs->pof_error(sp->po_opaque PIN_OUTPUT_ERROR_PASS);
	    sent = 1;
	}
    }

    if (!sent)
	fprintf(stderr, "%s\n", msg);
}

static void
pin_output_fanout_free (PIN_OUTPUT_FREE_ARGS)
{
    pin_output_fanout_t *fop = opaque;

    if (fop == NULL)
	return;

    for (unsigned i = 0; i < fop->pfo_count; i++)
	pin_output_free(&fop->pfo_sinks[i]);

    free(fop->pfo_sinks);
    free(fop);
}

static const pin_output_funcs_t pin_output_fanout_funcs = {
    .pof_version     = PIN_OUTPUT_FUNCS_VERSION,
    .pof_name        = "fanout",
    .pof_start       = pin_output_fanout_start,
    .pof_open        = pin_output_fanout_open,
    .pof_ns          = pin_output_fanout_ns,
    .pof_attrib      = pin_output_fanout_attrib,
    .pof_attrib_raw  = pin_output_fanout_attrib_raw,
    .pof_end_attribs = pin_output_fanout_end_attribs,
    .pof_text        = pin_output_fanout_text,
    .pof_comment     = pin_output_fanout_comment,
    .pof_pi          = pin_output_fanout_pi,
    .pof_close       = pin_output_fanout_close,
    .pof_finish      = pin_output_fanout_finish,
    .pof_error       = pin_output_fanout_error,
    .pof_free        = pin_output_fanout_free,
};

int
pin_output_fanout_init (pin_output_t *sinkp)
{
    pin_output_fanout_t *fop = calloc(1, sizeof(*fop));
    if (fop == NULL)
	return -1;

    sinkp->po_funcs = &pin_output_fanout_funcs;
    sinkp->po_opaque = fop;
    return 0;
}

int
pin_output_fanout_add (pin_output_t *fanoutp, const pin_output_t *sinkp)
{
    if (fanoutp->po_funcs != &pin_output_fanout_funcs)
	return -1;

    pin_output_fanout_t *fop = fanoutp->po_opaque;

    if (fop->pfo_count + 1 > fop->pfo_size) {
	unsigned newsize = fop->pfo_size ? fop->pfo_size * 2 : 4;
	pin_output_t *np = realloc(fop->pfo_sinks, newsize * sizeof(*np));
	if (np == NULL)
	    return -1;

	fop->pfo_sinks = np;
	fop->pfo_size = newsize;
    }

    fop->pfo_sinks[fop->pfo_count] = *sinkp;
    fop->pfo_count += 1;
    return 0;
}

unsigned
pin_output_fanout_count (const pin_output_t *fanoutp)
{
    if (fanoutp->po_funcs != &pin_output_fanout_funcs)
	return 0;

    const pin_output_fanout_t *fop = fanoutp->po_opaque;
    return fop->pfo_count;
}

void
pin_output_stack_init (pin_output_stack_t *stackp)
{
    memset(stackp, 0, sizeof(*stackp));
}

/*
 * Pop (and clean up) every writer.  The stack doesn't own the sinks;
 * whoever pushed a sink frees it.
 */
void
pin_output_stack_cleanup (pin_output_stack_t *stackp)
{
    while (stackp->pos_depth > 0)
	pin_output_stack_pop(stackp);

    free(stackp->pos_writers);
    memset(stackp, 0, sizeof(*stackp));
}

/*
 * Push a writer for the sink.  The returned pointer is good until the
 * next push (the array can move).
 */
pin_output_writer_t *
pin_output_stack_push (pin_output_stack_t *stackp, const pin_output_t *sinkp)
{
    if (stackp->pos_depth + 1 > stackp->pos_size) {
	unsigned newsize = stackp->pos_size ? stackp->pos_size * 2 : 4;
	pin_output_writer_t *np = realloc(stackp->pos_writers,
					  newsize * sizeof(*np));
	if (np == NULL)
	    return NULL;

	stackp->pos_writers = np;
	stackp->pos_size = newsize;
    }

    pin_output_writer_t *wrp = &stackp->pos_writers[stackp->pos_depth];
    pin_output_writer_init(wrp, sinkp);
    stackp->pos_depth += 1;

    return wrp;
}

void
pin_output_stack_pop (pin_output_stack_t *stackp)
{
    if (stackp->pos_depth == 0)
	return;

    stackp->pos_depth -= 1;
    pin_output_writer_cleanup(&stackp->pos_writers[stackp->pos_depth]);
}
