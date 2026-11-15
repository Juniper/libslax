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
 * pin_output -- the API for making output.
 *
 * Everything that makes output calls the callbacks of a sink.  A sink
 * is two pointers: a pointer to a shared, constant funcs structure
 * holding the callbacks (and a name for debugging), and an opaque
 * pointer for that sink's own state.  Every sink of one kind points
 * at the same funcs structure.  One kind writes XML to a file; another
 * (later) builds a fragment in the store; the fan-out kind passes
 * each event to a list of other sinks.  The code making the output
 * doesn't know which kind it has.
 *
 * Callers don't call the funcs directly; they use a writer
 * (pin_output_writer_t), which keeps the stack of open elements and
 * the end-of-attributes state, so every sink sees the same
 * well-formed event stream:
 *
 *   start (open (ns | attrib | attrib_raw)* end_attribs?
 *              (text | comment | pi | open ... close)* close)* finish
 *
 * end_attribs is sent before the first child of an element; an
 * element that closes with no children gets no end_attribs, so a sink
 * can write it as an empty element.  A NULL callback means the sink
 * ignores that event.  Callbacks return 0 on success and -1 on
 * failure.
 *
 * See build/plans/back-on-track.rst, B.1.
 */

#ifndef LIBSLAX_PIN_OUTPUT_H
#define LIBSLAX_PIN_OUTPUT_H

#include <stdio.h>
#include <stddef.h>

#include <libpsu/psucommon.h>

#define PIN_OUTPUT_FUNCS_VERSION	1

/* Flags for the text callback */
#define PIN_OUTPUT_TEXT_UNESC	(1<<0) /* Text must not be escaped */

/* Severity for the error callback */
typedef enum pin_output_severity_e {
    PIN_OUTPUT_SEV_MESSAGE = 0,	/* xsl:message (not an error) */
    PIN_OUTPUT_SEV_WARNING,	/* Keep going */
    PIN_OUTPUT_SEV_ERROR,	/* Something failed; keep going if we can */
    PIN_OUTPUT_SEV_FATAL,	/* The run stops */
} pin_output_severity_t;

/* Where an error happened; any field can be zero/NULL */
typedef struct pin_output_loc_s {
    const char *pol_filename;	/* Input file name */
    unsigned pol_line;		/* Line number (0 = unknown) */
    unsigned pol_offset;	/* Offset in the line or file */
} pin_output_loc_t;

/*
 * Argument lists for the callbacks, one macro per callback, so that
 * adding an argument means changing only the macro (and the callers).
 * An implementation is declared with its macro:
 *
 *     static int
 *     my_sink_open (PIN_OUTPUT_OPEN_ARGS)
 *
 * Every callback gets the sink's opaque pointer.  "prefix" and "uri"
 * are the namespace of a name; either can be NULL.  The _PASS macros
 * are the same lists without "opaque", for a sink (such as fan-out)
 * that hands an event on to other sinks.
 */
#define PIN_OUTPUT_START_ARGS \
    void *opaque UNUSED
#define PIN_OUTPUT_START_PASS

#define PIN_OUTPUT_OPEN_ARGS \
    void *opaque UNUSED, const char *prefix UNUSED, \
    const char *uri UNUSED, const char *name UNUSED
#define PIN_OUTPUT_OPEN_PASS , prefix, uri, name

#define PIN_OUTPUT_NS_ARGS \
    void *opaque UNUSED, const char *prefix UNUSED, const char *uri UNUSED
#define PIN_OUTPUT_NS_PASS , prefix, uri

#define PIN_OUTPUT_ATTRIB_ARGS \
    void *opaque UNUSED, const char *prefix UNUSED, \
    const char *uri UNUSED, const char *name UNUSED, \
    const char *value UNUSED
#define PIN_OUTPUT_ATTRIB_PASS , prefix, uri, name, value

#define PIN_OUTPUT_ATTRIB_RAW_ARGS \
    void *opaque UNUSED, const char *text UNUSED
#define PIN_OUTPUT_ATTRIB_RAW_PASS , text

#define PIN_OUTPUT_END_ATTRIBS_ARGS \
    void *opaque UNUSED
#define PIN_OUTPUT_END_ATTRIBS_PASS

#define PIN_OUTPUT_TEXT_ARGS \
    void *opaque UNUSED, const char *data UNUSED, size_t len UNUSED, \
    unsigned flags UNUSED
#define PIN_OUTPUT_TEXT_PASS , data, len, flags

#define PIN_OUTPUT_COMMENT_ARGS \
    void *opaque UNUSED, const char *data UNUSED
#define PIN_OUTPUT_COMMENT_PASS , data

#define PIN_OUTPUT_PI_ARGS \
    void *opaque UNUSED, const char *target UNUSED, const char *data UNUSED
#define PIN_OUTPUT_PI_PASS , target, data

#define PIN_OUTPUT_CLOSE_ARGS \
    void *opaque UNUSED, const char *prefix UNUSED, \
    const char *uri UNUSED, const char *name UNUSED
#define PIN_OUTPUT_CLOSE_PASS , prefix, uri, name

#define PIN_OUTPUT_FINISH_ARGS \
    void *opaque UNUSED
#define PIN_OUTPUT_FINISH_PASS

#define PIN_OUTPUT_ERROR_ARGS \
    void *opaque UNUSED, pin_output_severity_t sev UNUSED, \
    const pin_output_loc_t *locp UNUSED, const char *msg UNUSED
#define PIN_OUTPUT_ERROR_PASS , sev, locp, msg

#define PIN_OUTPUT_FREE_ARGS \
    void *opaque UNUSED
#define PIN_OUTPUT_FREE_PASS

typedef int (*pin_output_start_fn_t)(PIN_OUTPUT_START_ARGS);
typedef int (*pin_output_open_fn_t)(PIN_OUTPUT_OPEN_ARGS);
typedef int (*pin_output_ns_fn_t)(PIN_OUTPUT_NS_ARGS);
typedef int (*pin_output_attrib_fn_t)(PIN_OUTPUT_ATTRIB_ARGS);
typedef int (*pin_output_attrib_raw_fn_t)(PIN_OUTPUT_ATTRIB_RAW_ARGS);
typedef int (*pin_output_end_attribs_fn_t)(PIN_OUTPUT_END_ATTRIBS_ARGS);
typedef int (*pin_output_text_fn_t)(PIN_OUTPUT_TEXT_ARGS);
typedef int (*pin_output_comment_fn_t)(PIN_OUTPUT_COMMENT_ARGS);
typedef int (*pin_output_pi_fn_t)(PIN_OUTPUT_PI_ARGS);
typedef int (*pin_output_close_fn_t)(PIN_OUTPUT_CLOSE_ARGS);
typedef int (*pin_output_finish_fn_t)(PIN_OUTPUT_FINISH_ARGS);
typedef void (*pin_output_error_fn_t)(PIN_OUTPUT_ERROR_ARGS);
typedef void (*pin_output_free_fn_t)(PIN_OUTPUT_FREE_ARGS);

/* The callbacks for one kind of sink */
typedef struct pin_output_funcs_s {
    unsigned pof_version;	/* PIN_OUTPUT_FUNCS_VERSION */
    const char *pof_name;	/* Developer-friendly name ("xml") */

    pin_output_start_fn_t pof_start;
    pin_output_open_fn_t pof_open;
    pin_output_ns_fn_t pof_ns;
    pin_output_attrib_fn_t pof_attrib;
    pin_output_attrib_raw_fn_t pof_attrib_raw;
    pin_output_end_attribs_fn_t pof_end_attribs;
    pin_output_text_fn_t pof_text;
    pin_output_comment_fn_t pof_comment;
    pin_output_pi_fn_t pof_pi;
    pin_output_close_fn_t pof_close;
    pin_output_finish_fn_t pof_finish;
    pin_output_error_fn_t pof_error;
    pin_output_free_fn_t pof_free; /* Release the sink's state */
} pin_output_funcs_t;

/* A sink: shared funcs plus this sink's state */
typedef struct pin_output_s {
    const pin_output_funcs_t *po_funcs; /* Callbacks (shared, constant) */
    void *po_opaque;		/* This sink's state */
} pin_output_t;

static inline const char *
pin_output_name (const pin_output_t *sinkp)
{
    return (sinkp && sinkp->po_funcs && sinkp->po_funcs->pof_name)
	? sinkp->po_funcs->pof_name : "unknown";
}

/* Release a sink's state (via pof_free) and clear it */
void
pin_output_free (pin_output_t *sinkp);

/*
 * The writer: the engine side of a sink.  It records each open
 * element so close needs no name, and sends end_attribs before the
 * first child.  Names are copied into pw_names, since the caller's
 * strings may live in an mmap that can move.
 */
typedef struct pin_output_writer_s {
    pin_output_t pw_sink;	/* The sink we write to */
    unsigned pw_depth;		/* Number of open elements */
    unsigned pw_attribs_open;	/* Open element can still take attribs */
    unsigned pw_failed;		/* A callback has failed */
    char *pw_names;		/* Packed "prefix\0uri\0name\0" per depth */
    size_t pw_names_len;	/* Bytes in use */
    size_t pw_names_size;	/* Bytes allocated */
    size_t *pw_offsets;		/* Start of each depth in pw_names */
    unsigned pw_offsets_size;	/* Slots allocated */
} pin_output_writer_t;

void
pin_output_writer_init (pin_output_writer_t *wrp, const pin_output_t *sinkp);

void
pin_output_writer_cleanup (pin_output_writer_t *wrp);

int pin_output_start(pin_output_writer_t *wrp);
int pin_output_open(pin_output_writer_t *wrp, const char *prefix,
		    const char *uri, const char *name);
int pin_output_ns(pin_output_writer_t *wrp, const char *prefix,
		  const char *uri);
int pin_output_attrib(pin_output_writer_t *wrp, const char *prefix,
		      const char *uri, const char *name, const char *value);
int pin_output_attrib_raw(pin_output_writer_t *wrp, const char *text);
int pin_output_end_attribs(pin_output_writer_t *wrp);
int pin_output_text(pin_output_writer_t *wrp, const char *data, size_t len,
		    unsigned flags);
int pin_output_comment(pin_output_writer_t *wrp, const char *data);
int pin_output_pi(pin_output_writer_t *wrp, const char *target,
		  const char *data);
int pin_output_close(pin_output_writer_t *wrp);
int pin_output_finish(pin_output_writer_t *wrp);
void pin_output_error(pin_output_writer_t *wrp, pin_output_severity_t sev,
		      const pin_output_loc_t *locp, const char *fmt, ...);

/*
 * The fan-out sink passes each event to every sink in its list, in
 * order.  pin_output_fanout_add() takes over the added sink; freeing
 * the fan-out sink frees them all.
 */
int
pin_output_fanout_init (pin_output_t *sinkp);

int
pin_output_fanout_add (pin_output_t *fanoutp, const pin_output_t *sinkp);

unsigned
pin_output_fanout_count (const pin_output_t *fanoutp);

/*
 * The destination stack: output goes to the top writer.  The bottom
 * is the result (the fan-out of the attached sinks); later, a
 * variable with a content body pushes a store sink for the length of
 * that body.
 */
typedef struct pin_output_stack_s {
    pin_output_writer_t *pos_writers; /* Writers, bottom first */
    unsigned pos_depth;		/* Writers in use */
    unsigned pos_size;		/* Writers allocated */
} pin_output_stack_t;

void
pin_output_stack_init (pin_output_stack_t *stackp);

void
pin_output_stack_cleanup (pin_output_stack_t *stackp);

pin_output_writer_t *
pin_output_stack_push (pin_output_stack_t *stackp, const pin_output_t *sinkp);

void
pin_output_stack_pop (pin_output_stack_t *stackp);

static inline pin_output_writer_t *
pin_output_stack_top (pin_output_stack_t *stackp)
{
    return stackp->pos_depth
	? &stackp->pos_writers[stackp->pos_depth - 1] : NULL;
}

/*
 * The XML sink writes to a FILE *.  It doesn't close the file.  With
 * no options, it indents by 3 and writes no XML declaration.
 */
typedef struct pin_output_xml_opts_s {
    const char *poxo_encoding;	/* Write an XML declaration (NULL = don't) */
    unsigned poxo_indent;	/* Indent per level (3 is the usual) */
    unsigned poxo_debug;	/* Write debug comments */
} pin_output_xml_opts_t;

int
pin_output_xml_init (pin_output_t *sinkp, FILE *out,
		     const pin_output_xml_opts_t *optsp);

#endif /* LIBSLAX_PIN_OUTPUT_H */
