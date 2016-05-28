/*
 * $Id$
 *
 * Copyright (c) 2000-2007, 2011, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * This file defines a simple light-weight file buffering mechanism
 * that avoids copying data by returning a pointer into its buffer.
 */

#ifndef __JNX_FBUF_H__
#define __JNX_FBUF_H__

#ifndef FBUF_BUFSIZ
# define FBUF_BUFSIZ	8192
#endif
#define FBUF_BASE_SIZE	(FBUF_BUFSIZ - 1) /* Initial buffer size */
#define FBUF_INCR_SIZE	FBUF_BUFSIZ	/* Incremental size change */

struct fbuf_s;

#define FBUF_TRACE_ARGS \
struct fbuf_s *fbp UNUSED, const char *buf UNUSED, size_t buflen UNUSED

typedef void (*fbuf_trace_t)(FBUF_TRACE_ARGS);
typedef void (*fbuf_stderr_fn)(int fd);

typedef struct fbuf_s {
    int fb_fd;			/* File descriptor */
    int fb_flags;		/* Flags for this file buffer */
    char *fb_buf;		/* Start of the buffer */
    char *fb_ptr;		/* Current point in the buffer */
    int fb_size;		/* Size of the buffer */
    int fb_size_limit;		/* Maximum numbers of bytes to buffer */
    int fb_left;		/* Number of bytes of input left */
    unsigned fb_start;		/* Starting line number */
    unsigned fb_line;		/* Current line number */
    unsigned fb_timeout;	/* Read timeout */
    fbuf_trace_t fb_trace;	/* Trace function */
    void *fb_trace_cookie;	/* Trace cookie */
    fbuf_trace_t fb_record;	/* Record function */
    char *fb_rec;		/* Record pointer */
    pid_t fb_pid;		/* Process ID for popen */
    int fb_stderr;		/* Second pipe to child */
    fbuf_stderr_fn fb_error;
} fbuf_t;

/* Flags for fb_flags: */
#define FBF_EOF		(1<<0)	/* Eof seen */
#define FBF_XMLOPEN	(1<<1)	/* Seen xml open tag */
#define FBF_CHOMPCR	(1<<2)	/* Chomp a '\r' off the next read */
#define FBF_CHOMPLF	(1<<3)	/* Chomp a '\n' off the next read */
#define FBF_LINECNT	(1<<4)	/* Maintain a line count */
#define FBF_CLOSE	(1<<5)	/* Close fd on close */
#define FBF_SEQPACKET	(1<<6)  /* over a SEQPACKET socket */
#define FBF_EOF_PENDING (1<<7)  /* Eof seen while looking ahead for abort */
#define FBF_DATA        (1<<8)  /* Get data one line at a time */
#define FBF_UNGETTED    (1<<9)  /* An unget operation has been performed */
#define FBF_STRING	(1<<10)	/* Input is a static string (no read) */

/*
 * fbuf_eof: return non-zere if the file buffer contains buffered data.
 */
static inline slax_boolean_t
fbuf_eof (fbuf_t *fbp)
{
    return (fbp->fb_flags & FBF_EOF);
}

static inline slax_boolean_t
fbuf_eof_pending (fbuf_t *fbp)
{
    return (fbp->fb_flags & FBF_EOF_PENDING);
}

static inline void
fbuf_set_eof (fbuf_t *fbp)
{
    fbp->fb_left = 0;
    fbp->fb_flags |= FBF_EOF;
}

static inline void
fbuf_set_timeout (fbuf_t *fbp, unsigned timeout)
{
    fbp->fb_timeout = timeout;
}

static inline void
fbuf_set_data (fbuf_t *fbp)
{
    fbp->fb_flags |= FBF_DATA;
}

static inline void
fbuf_unset_data (fbuf_t *fbp)
{
    fbp->fb_flags &= ~FBF_DATA;
}

static inline void
fbuf_set_ungetted (fbuf_t *fbp)
{
    fbp->fb_flags |= FBF_UNGETTED;
}

static inline void
fbuf_unset_ungetted (fbuf_t *fbp)
{
    fbp->fb_flags &= ~FBF_UNGETTED;
};


static inline slax_boolean_t
fbuf_has_buffered (fbuf_t *fbp)
{
    return fbp->fb_left > 0;
}

char *fbuf_get_data_line (fbuf_t *fbp, size_t *bytes_read);

fbuf_t *fbuf_fdopen (int fd, int flags);

/* unget n bytes in fbuf buffer */
int fbuf_ungets (fbuf_t *fbp, int n);
/* attempts to read a complete string.  Blocks. */
char *fbuf_gets (fbuf_t *fbp);
/* attempts to read a complete string looping "tries" times */
#define FBUF_GETS_TRY_FOREVER 0 /* see FBUF_GET_XML_TRIES */
char *fbuf_gets_ex (fbuf_t *fbp, const int tries);


void fbuf_close (fbuf_t *fbp);
void fbuf_pclose (fbuf_t *fbp, int *status);
fbuf_t *fbuf_popen (const char *cmd, ...);
fbuf_t *fbuf_popen2 (fbuf_stderr_fn, const char *cmd, ...);
fbuf_t *fbuf_pipe_popen (fbuf_stderr_fn, const char *cmd0, ...);
fbuf_t *fbuf_open (const char *path);

slax_boolean_t fbuf_is_leading (fbuf_t *fbp, const char *leading);
slax_boolean_t fbuf_is_aborting (fbuf_t *fbp);
slax_boolean_t fbuf_has_pending (fbuf_t *fbp);

static inline slax_boolean_t
fbuf_is_readable (fbuf_t *fbp)
{
    return fbuf_has_buffered(fbp) || fbuf_has_pending(fbp);
}

/* 
 * fbuf_get_xml_namespace() & fbuf_get_xml()
 *
 * Returns the next XML node, the type and any attributes.  
 *
 * If the fbuf's fd is set to non-blocking, and there is not
 * enough data to return a complete node (e.g., no data or only
 * a partial tag), errno will be set to EWOULDBLOCK and the 
 * returned value will be a pointer to a null byte 
 * (e.g., c && *c = 0).
 *
 * Returns NULL only on error (bad fb, non-XML garbage in the 
 * stream, .. )
 */

char *fbuf_get_xml_namespace (fbuf_t *fbp, int *typep, char **namespacep,
			      char **restp, unsigned flags);
char *fbuf_get_xml (fbuf_t *fbp, int *typep, char **restp, unsigned flags);

/* Values for flags: */
#define FXF_COMPLETE	(1<<0)	/* Get complete text for XML_TYPE_DATA */
#define FXF_PRESERVE	(1<<1)	/* Preserve whitespace in DATA tags */
#define FXF_LEAVE_NS	(1<<2)	/* Do not strip the namespace from tags */

/* Values for *typep */
#define XML_TYPE_UNKNOWN	0 /* Unknown or unparsable */
#define XML_TYPE_ERROR		1 /* Error parsing tag/data/etc */
#define XML_TYPE_PROC		2 /* Processing instruction */
#define XML_TYPE_OPEN		3 /* <foo> */
#define XML_TYPE_DATA		4 /* foo-de-foo-de-foo */
#define XML_TYPE_CLOSE		5 /* </foo> */
#define XML_TYPE_COMMENT	6 /* <!-- foo --> */
#define XML_TYPE_EOF		7 /* EOF */
#define XML_TYPE_EMPTY		8 /* <foo/> */
#define XML_TYPE_ABORT		9 /* <abort/> */
#define XML_TYPE_NOOP		10 /* harmless do-nothing */
#define XML_TYPE_RESYNC		11 /* ]]>]]> */

#define NUM_XML_TYPE		12

const char *fbuf_xml_type (int type);

static inline void *
fbuf_get_trace_cookie (fbuf_t *fbp)
{
    return fbp->fb_trace_cookie;
}

static inline void
fbuf_set_trace_cookie (fbuf_t *fbp, void *cookie)
{
    fbp->fb_trace_cookie = cookie;
}

static inline void
fbuf_set_trace_func (fbuf_t *fbp, fbuf_trace_t func)
{
    fbp->fb_trace = func;
}

slax_boolean_t fbuf_trace_tagged (fbuf_t *fbp, FILE *fp, const char *tag);

static inline int
fbuf_fileno (fbuf_t *fbp)
{
    return fbp->fb_fd;
}

void fbuf_record_data (fbuf_t *fbp, fbuf_trace_t func);

static inline void
fbuf_reset_linecnt (fbuf_t *fbp)
{
    fbp->fb_flags |= FBF_LINECNT;
    fbp->fb_line = fbp->fb_start = 1;
}

static inline void
fbuf_clear_linecnt (fbuf_t *fbp)
{
    fbp->fb_flags &= ~FBF_LINECNT;
    fbp->fb_line = fbp->fb_start = 0;
}

static inline unsigned
fbuf_get_linecnt (fbuf_t *fbp)
{
    return fbp->fb_start;
}

static inline slax_boolean_t
fbuf_is_seqpacket (fbuf_t *fbp)
{
    return (fbp->fb_flags & FBF_SEQPACKET);
}

static inline void
fbuf_set_size_limit (fbuf_t *fbp, int size_limit)
{
    fbp->fb_size_limit = size_limit;
}

fbuf_t *
fbuf_from_string (char *data, int len);

fbuf_t *
fbuf_from_const_string (const char *const_data, int len);

#endif /* __JNX_FBUF_H__ */
