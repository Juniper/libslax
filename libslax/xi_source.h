/*
 * $Id$
 *
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer (phil@) June 2016
 */

#ifndef LIBSLAX_XI_SOURCE_H
#define LIBSLAX_XI_SOURCE_H

/*
 * A note about attribute encoding: There are two distinct schemes.
 * Under the first, the complete, unparsed, still-escaped string of
 * attributes is recorded as an XI_TYPE_ATSTR.  The second handles
 * parsed attributes by pulling them into name/value pairs and
 * recording the name as XI_TYPE_ATNAME and the value as
 * XI_TYPE_ATVALUE under a node of type XI_TYPE_ATTRIB.
 *
 * Similarly parsed namespaces are XI_TYPE_NSPREF and XI_TYPE_NSVALUE
 * under an XI_TYPE_NS node, but the value is a string under the
 * parser's list of valid namespaces.  When the XI_TYPE_NSPREF is not
 * present, the namespace is the default one.
 */

/*
 * Parser source object
 *
 * Note that we return pointers directly into our buffer.
 */
typedef struct xi_source_s {
    int xps_fd;			/* File being read */
    char *xps_filename;		/* Filename */
    unsigned xps_lineno;	/* Line number of input */
    unsigned xps_offset;	/* Offset in the file */
    xi_source_flags_t xps_flags; /* Flags for this source */
    char *xps_bufp;		/* Input buffer */
    char *xps_curp;		/* Current data point */
    unsigned xps_len;		/* Number of bytes in the input buffer */
    unsigned xps_size;		/* Size of the input buffer (max) */
    xi_node_type_t xps_last;	/* Type of last token returned */
} xi_source_t;

/* Flags for ps_flags: */
#define XPSF_MMAP_INPUT	(1<<0)	/* File is mmap'd */
#define XPSF_IGNORE_WS	(1<<1)	/* Ignore whitespace-only mixed content */
#define XPSF_NO_READ	(1<<2)	/* Don't read() on this fd */
#define XPSF_EOF_SEEN	(1<<3)	/* EOF has been seen; read should fail */
#define XPSF_READ_ALL	(1<<4)	/* File is read completely into memory */
#define XPSF_CLOSE_FD	(1<<5)	/* Close fd when cleaning up */
#define XPSF_TRIM_WS	(1<<6)	/* Trim whitespace from data */
#define XPSF_VALIDATE	(1<<7)	/* Validate input */
#define XPSF_LINE_NO	(1<<8)	/* Track line numbers for input */
#define XPSF_IGNORE_COMMENTS (1<<9) /* Discard comments */
#define XPSF_IGNORE_DTD (1<<10) /* Discard DTDs */

xi_source_t *
xi_source_create (int fd, xi_source_flags_t flags);

xi_source_t *
xi_source_open (const char *path, xi_source_flags_t flags);

void
xi_source_destroy (xi_source_t *srcp);

xi_node_type_t
xi_source_next_token (xi_source_t *srcp, char **datap, char **restp);

size_t
xi_source_unescape (xi_source_t *srcp, char *start, unsigned len);

void
xi_source_failure (xi_source_t *srcp, int errnum, const char *fmt, ...);

#endif /* LIBSLAX_XI_SOURCE_H */
