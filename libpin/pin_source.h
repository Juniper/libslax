/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer (phil@) June 2016
 */

#ifndef LIBSLAX_PIN_SOURCE_H
#define LIBSLAX_PIN_SOURCE_H

#include <libpin/pin_common.h>

/*
 * A note about attribute encoding: There are two distinct schemes.
 * Under the first, the complete, unparsed, still-escaped string of
 * attributes is recorded as an PIN_TYPE_ATSTR.  The second handles
 * parsed attributes by pulling them into name/value pairs and
 * recording the name as PIN_TYPE_ATNAME and the value as
 * PIN_TYPE_ATVALUE under a node of type PIN_TYPE_ATTRIB.
 */

/*
 * Parser source object
 *
 * Note that we return pointers directly into our buffer.
 */
struct pin_source_s {
    int pps_fd;			/* File being read */
    char *pps_filename;		/* Filename */
    unsigned pps_lineno;	/* Line number of input */
    unsigned pps_offset;	/* Offset in the file */
    pin_source_flags_t pps_flags; /* Flags for this source */
    char *pps_bufp;		/* Input buffer */
    char *pps_curp;		/* Current data point */
    unsigned pps_len;		/* Number of bytes in the input buffer */
    unsigned pps_size;		/* Size of the input buffer (max) */
    pin_node_type_t pps_last;	/* Type of last token returned */
}; /* pin_source_t */

/* Flags for ps_flags: */
#define PPSF_MMAP_INPUT	(1<<0)	/* File is mmap'd */
#define PPSF_IGNORE_WS	(1<<1)	/* Ignore whitespace-only mixed content */
#define PPSF_NO_READ	(1<<2)	/* Don't read() on this fd */
#define PPSF_EOF_SEEN	(1<<3)	/* EOF has been seen; read should fail */
#define PPSF_READ_ALL	(1<<4)	/* File is read completely into memory */
#define PPSF_CLOSE_FD	(1<<5)	/* Close fd when cleaning up */
#define PPSF_TRIM_WS	(1<<6)	/* Trim whitespace from data */
#define PPSF_VALIDATE	(1<<7)	/* Validate input */
#define PPSF_LINE_NO	(1<<8)	/* Track line numbers for input */
#define PPSF_IGNORE_COMMENTS (1<<9) /* Discard comments */
#define PPSF_IGNORE_DTD (1<<10) /* Discard DTDs */

pin_source_t *
pin_source_create (int fd, pin_source_flags_t flags);

pin_source_t *
pin_source_open (const char *path, pin_source_flags_t flags);

void
pin_source_destroy (pin_source_t *srcp);

pin_node_type_t
pin_source_next_token (pin_source_t *srcp, char **datap, char **restp);

size_t
pin_source_unescape (pin_source_t *srcp, char *start, unsigned len);

void
pin_source_failure (pin_source_t *srcp, int errnum, const char *fmt, ...);

#endif /* LIBSLAX_PIN_SOURCE_H */
