/*
 * $Id$
 *
 * Copyright (c) 2000-2008, 2011, 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * (Originally libjuniper/fbuf.c)
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <paths.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <assert.h>
#include <stdarg.h>

#include "slaxconfig.h"
#include <libslax/slax.h>
#if 0
#include <libjuise/common/aux_types.h>
#include <libjuise/string/strextra.h>
#include <libjuise/common/bits.h>
#include <libjuise/xml/xmlutil.h>
#include <libjuise/io/trace.h>
#include <libjuise/io/logging.h>
#endif

#if defined(HOSTPROG) && !defined(va_copy)
#define va_copy(dest, src) ((dest) = (src))
#endif

#ifdef UNIT_TEST
# define FBUF_BUFSIZ 8192			/* make it easier to test */
#endif

#include <libslax/fbuf.h>

#ifndef NEED_STR_EXTRA
/*
 * Functions originally defined in libjuise/string/strextra.h
 */
static inline char *
strnextws (char *input)
{
    if (input) {
	while (*input && !isspace((int) *input)) {
	    input++;
	}
    }
    return input;
}

/*
 * strtrimws: skip over leading whitespace
 */
static inline char *
strtrimws (char *input)
{
    while (isspace((int) *input))
	input += 1;
    return input;
}

/*
 * strtrimws: skip over leading whitespace
 */
static inline char *
strtrimwsc (char *input, unsigned *count)
{
    while (isspace((int) *input)) {
	if (*input == '\n') *count += 1;
	input += 1;
    }
    return input;
}

/*
 * strtrimtailws: skip over trailing whitespace
 */
static inline char *
strtrimtailws (char *input, char *start)
{
    while (start < input && isspace((int) *input))
	input -= 1;
    return input;
}
#endif /* NEED_STR_EXTRA */

/*
 * fbuf_realloc: make a bigger buffer for the fbuf. Buffered data is preserved.
 */
static slax_boolean_t
fbuf_realloc (fbuf_t *fbp, size_t add)
{
    size_t size = fbp->fb_size + add;
    char *newp;

    if (fbp->fb_size_limit > 0 && size > (size_t) fbp->fb_size_limit)
	return TRUE;

    newp = malloc(size + 1);
    if (newp == NULL)
	return TRUE;

    /*
     * Switch to the new buffer; copy over any old data, add a
     * trailing NULL, and toss the old buffer.
     */
    if (fbp->fb_left)
	memcpy(newp, fbp->fb_ptr, fbp->fb_left);
    if (fbp->fb_buf)
	free(fbp->fb_buf);
    newp[ size ] = 0;		/* Guarantee null termination */

    fbp->fb_buf = fbp->fb_ptr = newp; /* Update fb fields */
    
    fbp->fb_size = size;

    return FALSE;
}

/*
 * fbuf_fdopen: wrap a fbuf around an open file descriptor
 */
fbuf_t *
fbuf_fdopen (int fd, int flags)
{
    fbuf_t *fbp;

    fbp = calloc(1, sizeof(*fbp));
    if (fbp == NULL)
	return NULL;

    fbp->fb_fd = fd;
    fbp->fb_flags = flags;
    if (fbuf_realloc(fbp, FBUF_BASE_SIZE)) {
	free(fbp);
	return NULL;
    }

    return fbp;
}

fbuf_t *
fbuf_open (const char *path)
{
    int fd;
    fbuf_t *fbp;

    fd = open(path, O_RDONLY);
    if (fd < 0)
	return NULL;

    fbp = fbuf_fdopen(fd, 0);
    if (fbp == NULL) {
	close(fd);
	return NULL;
    }

    fbp->fb_flags |= FBF_CLOSE;
    return fbp;
}

	/*
	 * Convert va_list into argv[]
	 * shamelessly "lifted" from libc/exec.c
	 */
#define VA_LIST_TO_ARGV(Xap_start, Xargv)		\
    do {						\
	va_list Xap;					\
	u_int Xcount = 0;				\
	va_copy(Xap, Xap_start);			\
	while ((va_arg(Xap, char *)) != NULL) {		\
	    Xcount++;					\
	}						\
	va_end(Xap);					\
	va_copy(Xap, Xap_start);			\
	Xargv = alloca((Xcount + 1) * sizeof(char *));	\
	Xcount = 0;					\
	while (TRUE) {					\
	    char *Xcp;					\
	    Xcp = va_arg(Xap, char *);			\
	    if (Xcp == NULL) {				\
		break;					\
	    }						\
	    Xargv[Xcount++] = Xcp;			\
	}						\
	Xargv[Xcount] = NULL;				\
	va_end(Xap);					\
    } while (0)

/*
 * fbuf_pipe_popen:
 * Open a reading pipe with multiple processes pipelined.
 * The calling syntax is:
 * fbuf_pipe_popen(FIRST_COMMAND, ARGV0_FIRST, ARGV1_FIRST, ..., NULL,
 *                 SECOND_COMMAND, ..., NULL,
 *                 ...
 *                 NULL);
 * If 'error_fn' is not NULL, we create a separate pipe for collecting
 * standard error of child processes. We check if data is available on
 * this pipe in fbuf_get_input(), and call error_fn with the pipe's
 * file descriptor passed in. This pipe is set to nonblocking to
 * prevent a mischievous error_fn from block forever on read().
 */
fbuf_t *
fbuf_pipe_popen (fbuf_stderr_fn error_fn, const char *cmd0, ...)
{
    va_list vap;
    fbuf_t *fbp;
    volatile int des_left, pgid;
    int pdes[2], errdes[2], pid, saved_errno;
    const char * volatile cmd;

    des_left = open(_PATH_DEVNULL, O_RDONLY);
    if (des_left < 0)
	return NULL;

    /*
     * Create pipe to collect stderr of the child processes.
     */
    if (error_fn && pipe(errdes) < 0) {
	saved_errno = errno;
	close(des_left);
	errno = saved_errno;
	return NULL;
    }
    
    pgid = -1;

    va_start(vap, cmd0);
    for (cmd = cmd0; cmd; cmd = va_arg(vap, const char *)) {
	char **argv;

	if (pipe(pdes) < 0) {
	    saved_errno = errno;
	    close(des_left);
	    errno = saved_errno;
	    return NULL;
	}

	switch (pid = fork()) {
	case -1:
	    saved_errno = errno;

	    if (pgid > 0)
		killpg(pgid, SIGKILL);

	    close(des_left);

	    close(pdes[0]);
	    close(pdes[1]);
	    if (error_fn) {
		close(errdes[0]);
		close(errdes[1]);
	    }

	    errno = saved_errno;

	    return NULL;

	case 0:
	    setpgid(0, pgid > 0 ? pgid : 0);

	    if (des_left != STDIN_FILENO) {
		dup2(des_left, STDIN_FILENO);
		close(des_left);
	    }

	    if (pdes[1] != STDOUT_FILENO) {
		dup2(pdes[1], STDOUT_FILENO);
		close(pdes[1]);
	    }
	    close(pdes[0]);
	    if (error_fn) {
		if (errdes[1] != STDERR_FILENO) {
		    dup2(errdes[1], STDERR_FILENO);
		    close(errdes[1]);
		}
		close(errdes[0]);
	    }

	    VA_LIST_TO_ARGV(vap, argv);
	    execv(cmd, argv);

	    _exit(127); /* it just happens to be what lib/libc/gen/popen.c does */
	}

	if (pgid < 0)
	    pgid = pid;

	/* 
	 * close the reading end of the left side
	 * and the writing end of the right side
	 * since those are owned by the child now
	 */
	close(des_left);
	close(pdes[1]);

	/* shift the cascade */
	des_left=pdes[0];

	/* forward varargs */
	while (va_arg(vap, const char *));
    }
    va_end(vap);

    if (error_fn)
	close(errdes[1]);
    fbp = fbuf_fdopen(des_left, 0);
    if (fbp == NULL) {
	close(des_left);
	if (error_fn)
	    close(errdes[0]);
	return NULL;
    }

    fbp->fb_flags |= FBF_CLOSE;
    if (pgid > 0)
	/* waitpid loops on all children of this process group */
	fbp->fb_pid = -pgid;
    if (error_fn) {
	fbp->fb_stderr = errdes[0];
	fbp->fb_error = error_fn;
	fcntl(fbp->fb_stderr, F_SETFL, O_NONBLOCK);
    }

    return fbp;
}

fbuf_t *
fbuf_popen (const char *cmd, ...)
{
    fbuf_t *fbp;
    int pdes[2];
    pid_t pid;
    va_list vap;
    char **argv;

    if (pipe(pdes) < 0)
	return NULL;

    pid = fork();
    switch (pid) {

    case -1:			/* Error. */
	close(pdes[0]);
	close(pdes[1]);
	return NULL;

    case 0:				/* Child. */
	close(pdes[0]);
	if (pdes[1] != STDOUT_FILENO) {
	    (void)dup2(pdes[1], STDOUT_FILENO);
	    (void)close(pdes[1]);
	}

	va_start(vap, cmd);
	VA_LIST_TO_ARGV(vap, argv);
	va_end(vap);
	execv(cmd, argv);
	_exit(127); /* it just happens to be what lib/libc/gen/popen.c does */
    }

    /* Parent; assume fdopen can't fail. */
    close(pdes[1]);
    fbp = fbuf_fdopen(pdes[0], 0);
    if (fbp == NULL) {
	close(pdes[0]);
	return NULL;
    }

    fbp->fb_flags |= FBF_CLOSE;
    fbp->fb_pid = pid;
    return fbp;
}

/*
 * Create two pipes, one for reading stdout of child; one for reading
 * stderr of child.
 * 'error_fn' must be specified, since there is no point creating the
 * pipe when nobody is interested in the data in the pipe.
 */
fbuf_t *
fbuf_popen2 (fbuf_stderr_fn error_fn, const char *cmd, ...)
{
    fbuf_t *fbp;
    int pdes[4];
    pid_t pid;
    va_list vap;
    char **argv;

    if (error_fn == NULL)
	return NULL;

    if (pipe(pdes) < 0)
	return NULL;

    if (pipe(&pdes[2]) < 0) {
	close(pdes[0]);
	close(pdes[1]);
	return NULL;
    }
    
    pid = fork();
    switch (pid) {

    case -1:			/* Error. */
	close(pdes[0]);
	close(pdes[1]);
	close(pdes[2]);
	close(pdes[3]);
	return NULL;

    case 0:				/* Child. */
	close(pdes[0]);
	close(pdes[2]);
	if (pdes[1] != STDOUT_FILENO) {
	    (void) dup2(pdes[1], STDOUT_FILENO);
	    (void) close(pdes[1]);
	}
	if (pdes[3] != STDERR_FILENO) {
	    (void) dup2(pdes[3], STDERR_FILENO);
	    (void) close(pdes[3]);
	}

	va_start(vap, cmd);
	VA_LIST_TO_ARGV(vap, argv);
	va_end(vap);
	execv(cmd, argv);

	_exit(127); /* it just happens to be what lib/libc/gen/popen.c does */
    }

    /* Parent */
    close(pdes[1]);
    close(pdes[3]);

    fbp = fbuf_fdopen(pdes[0], 0);
    if (fbp == NULL) {
	close(pdes[0]);
	close(pdes[2]);
	return NULL;
    }

    fbp->fb_flags |= FBF_CLOSE;
    fbp->fb_pid = pid;
    fbp->fb_stderr = pdes[2];
    fbp->fb_error = error_fn;
    fcntl(fbp->fb_stderr, F_SETFL, O_NONBLOCK);
    
    return fbp;
}

static void
fbuf_record (fbuf_t *fbp, char *new, size_t new_size)
{
    char *cp, *np, *ep;
    char *rollover_buf;

    cp = new;
    ep = new + new_size;

    /*
     * If fb_rec is not null, that means the previous buffer didn't
     * get recorded in its entirety.  fb_rec contains the portion of
     * the buffer that didn't get recorded.  This special handling
     * append the current buffer with what's in fb_rec before
     * recording the data.
     */
    if (fbp->fb_rec) {
	int len = strlen(fbp->fb_rec);
	if ((rollover_buf = alloca(len + new_size + 1)) != NULL) {
	    memcpy(rollover_buf, fbp->fb_rec, len);
	    memcpy(rollover_buf + len, new, new_size);
	    /* make sure the buffer is null-terminated */
	    rollover_buf[len + new_size] = 0;
	    /* reset the working buffer pointers to use this new buffer */
	    cp = rollover_buf;
	    ep = rollover_buf + len + new_size;
	}
	free(fbp->fb_rec);
	fbp->fb_rec = NULL;
    }

    for (; cp < ep; cp = np) {
	np = index(cp, '\n');
	if (np == NULL) {
	    fbp->fb_rec = strndup(cp, ep-cp);
	    break;
	}
	(*fbp->fb_record)(fbp, cp, np - cp);
	if (np < ep)
	    np += 1;
    }
}

void
fbuf_record_data (fbuf_t *fbp, fbuf_trace_t func)
{
    fbp->fb_record = func;

    if (func) {
	fbuf_record(fbp, fbp->fb_ptr, fbp->fb_left);
    } else {
	if (fbp->fb_rec) {
	    free(fbp->fb_rec);
	    fbp->fb_rec = NULL;
	}
    }
}

static inline int
fbuf_get_bytes_outstanding (fbuf_t *fbp UNUSED)
{
#ifdef FIONREAD
    int readable;
    if (ioctl(fbp->fb_fd, FIONREAD, &readable) < 0)
	return -1;
    return readable;

#else /* FIONREAD */
    return 1024;		/* XXX Pathetic fake */
#endif /* FIONREAD */
}

static size_t
fbuf_get_input (fbuf_t *fbp, char **workp, int tries, slax_boolean_t look_ahead)
{
    int rc;
    char *cp = *workp;
    char *ep = fbp->fb_buf + fbp->fb_size;
    fd_set readfds;
    int max_fd, save;

    if (fbp->fb_flags & FBF_DATA) {
	char *start, *cur;
        
	if (fbp->fb_flags & FBF_UNGETTED) {
	    /* get more data, or else only the ungetted data will be returned */
	    fbuf_unset_data(fbp);
	    if (! fbuf_get_input(fbp, &fbp->fb_ptr, tries, look_ahead)) {
		*workp = NULL;
		return 0;
	    }
	    /* data is now at beginning of buffer */
	    fbp->fb_ptr = fbp->fb_buf;
	    fbuf_set_data(fbp);
	    fbuf_unset_ungetted(fbp);
	}
	if (fbp->fb_left == 0) {
	    /* no more data, get some more */
	    fbuf_unset_data(fbp);
	    /* place data at beginning of buffer */
	    if (! fbuf_get_input(fbp, &fbp->fb_buf, tries, look_ahead)) {
		*workp = NULL;
		return 0;
	    }
	    /* move the current pointer back to the beginning of buffer */
	    fbp->fb_ptr = fbp->fb_buf;
	    fbuf_set_data(fbp);
	}
	start = fbp->fb_ptr;
	for (cur = fbp->fb_ptr; *cur; cur++) {
	    if (*cur == '<') {
		fbuf_unset_data(fbp);
		break;
	    }
	}
	/* adjust the buffer properties */
	fbp->fb_ptr = cur;
	fbp->fb_left -= (cur - start);
	*workp = start;
	return (cur - start);
    }
    if (fbp->fb_flags & FBF_EOF_PENDING) {
	if (look_ahead)
	    return 0;
	fbp->fb_flags |= FBF_EOF;
	fbp->fb_flags ^= FBF_EOF_PENDING;
    }
    
    if (fbp->fb_flags & FBF_EOF)
	return 0;

    if (fbp->fb_left == 0 && cp == fbp->fb_ptr) {
	cp = fbp->fb_ptr = fbp->fb_buf;
    }
    if (fbp->fb_stderr)
	max_fd = MAX(fbp->fb_fd, fbp->fb_stderr);
    else
	max_fd = fbp->fb_fd;

    /*
     * We better stay within the limits of readfds - otherwise
     * we have will corrupt the stack when we do a FD_SET(*, max_fd)
     * or select(max_fd + 1, ...)
     */
    assert(max_fd < (int) FD_SETSIZE);

    /*
     * Nothing left in the buffer; go read some more input. If
     * we've got no more room, pack it down, or get more space.
     * To accomodate TNP/RDP socket, we always try to read at
     * least BUFSIZ.
     */
    for (;;) {
	if (ep - cp >= BUFSIZ) {
	    /*
	     * We got some room; read some data.
	     * Read the stderr pipe first if there is data there.
	     */
	    struct timeval timeout;
	    int readable;

	    FD_ZERO(&readfds);

	    FD_SET(fbp->fb_fd, &readfds);
	    if (fbp->fb_stderr)
		FD_SET(fbp->fb_stderr, &readfds);
	    
	    timeout.tv_sec = fbp->fb_timeout;
	    timeout.tv_usec = 0;

	    rc = select(max_fd + 1, &readfds, NULL, NULL,
			timeout.tv_sec > 0 ? &timeout : NULL);
	    switch (rc) {
		case 0:
		    /* timeout */
		    return 0;

		case -1:
		    if (errno == EINTR)
			continue;
		    save = errno;
		    slaxLog("Unable to select input stream "
			   "for command '%s': %m", "fbuf pipe");
		    errno = save;
		    return 0;

		default:
		    if (fbp->fb_stderr && FD_ISSET(fbp->fb_stderr, &readfds)) {
			if (fbp->fb_error)
			    fbp->fb_error(fbp->fb_stderr);
			else {
			    /* read and discard, what else can we do */
			    char buf[BUFSIZ];
			    int rc2 = read(fbp->fb_stderr, buf, sizeof buf);
			    if (rc2 < 0)
				break;
			}
		    }
		    break;
	    }

	    if (!FD_ISSET(fbp->fb_fd, &readfds))
		continue;

	    /*
	     * attempt to make any write from peer processed atomically
	     * as otherwise callers of the fbuf family of functions can
	     * run into border-cases not processing the response properly
	     */
	    readable = fbuf_get_bytes_outstanding(fbp);
	    if (readable >= 0 && readable > ep - cp) {
		/* more space is required */

		if (fbuf_realloc(fbp, readable - (ep - cp))) {
		    *workp = cp;
		    return 0;
		}

		cp = fbp->fb_ptr + fbp->fb_left;
		ep = fbp->fb_buf + fbp->fb_size;
	    }

	    rc = read(fbp->fb_fd, cp, ep - cp);
	    if (rc > 0) {
		cp[ rc ] = 0; /* Guarantee null termination */
		if (fbp->fb_record)
		    fbuf_record(fbp, cp, rc);
		if (fbp->fb_trace)
		    (*fbp->fb_trace)(fbp, cp, rc);
		fbp->fb_left += rc;
		*workp = cp;
		return rc;

	    } else if (rc == 0 || !(errno == EWOULDBLOCK || errno == EINTR)) {
		fbp->fb_flags |= look_ahead ? FBF_EOF_PENDING : FBF_EOF;
		*workp = cp;
		return 0;
	    } else if (errno == EWOULDBLOCK && tries > 0) {
		if (!(--tries))
		    return -1;
	    }

	} else if (fbp->fb_ptr != fbp->fb_buf) {
	    /* We got room at the front; pack it down */
	    char *start = fbp->fb_ptr;

	    memmove(fbp->fb_buf, start, fbp->fb_left);
	    fbp->fb_ptr = fbp->fb_buf;
	    cp = fbp->fb_ptr + fbp->fb_left;

	} else {
	    /* Completely out of room; grow the buffer */
	    if (fbuf_realloc(fbp, FBUF_INCR_SIZE)) {
		*workp = cp;
		return 0;
	    }

	    cp = fbp->fb_ptr + fbp->fb_left;
	    ep = fbp->fb_buf + fbp->fb_size;
	}
    }
}

/*
 * fbuf_get_data_line: returns the next line of huge data
 * returns NULL if error occured
 * bytes_read is the number of bytes read or 0 if already seen '<'
 */
char *
fbuf_get_data_line (fbuf_t *fbp, size_t *bytes_read)
{
    char *cp = fbp->fb_ptr;

    if (!bytes_read)
        return NULL;

    if (!(fbp->fb_flags & FBF_DATA)) { /* seen '<' */
        *bytes_read = 0;
        return cp;
    }
    *bytes_read = fbuf_get_input(fbp, &cp, 0, FALSE);
    if (!*cp) {
	return NULL;
    }
    if ((*bytes_read == 0) && (fbp->fb_flags & FBF_DATA)) {
        /* got 0 bytes before seeing '<', read more from input stream */
        return fbuf_get_data_line(fbp, bytes_read);
    }
    return cp;
}

/*
 * fbuf_ungets: unget n bytes in the fbuf buffer
 * return number of bytes ungetted
 * return -1 on error
 */
int
fbuf_ungets (fbuf_t *fbp, int n)
{
    if ((fbp->fb_ptr - n) < fbp->fb_buf)
        return -1;

    fbp->fb_ptr -= n;
    fbp->fb_left += n;
    fbuf_set_ungetted(fbp);
    return n;
}

/*
 * fbuf_gets: return the next line of inline as it sets inside the
 * buffer. Null-terminated, with no newline.
 *
 * This operation temporarily sets the descriptor to blocking
 * state so that we do not spin inside get_input.
 */

char *
fbuf_gets (fbuf_t *fbp)
{
    char* data;
    int o;

    /* set the socket to blocking */
    o = fcntl(fbp->fb_fd, F_GETFL);
    fcntl(fbp->fb_fd, F_SETFL, (o & ~(O_NONBLOCK)));

    /* get the string */
    data = fbuf_gets_ex(fbp, FBUF_GETS_TRY_FOREVER);

    /* restore the blocking-ness */
    fcntl(fbp->fb_fd, F_SETFL, o);
    
    return data;
}

/*
 * fbuf_gets_ex: return the next line of input as it sets inside the
 * buffer. Null-terminated, with no newline.
 *
 * if tries is 0, we spin forever waiting for a newline.  Otherwise
 * we try N times and return NULL.
 */

char *
fbuf_gets_ex (fbuf_t *fbp, const int tries)
{
    int left = fbp->fb_left;
    char *cp = fbp->fb_ptr, *np = NULL;
    errno = 0;

    for (;;) {
	if (left == 0) {
	    left = fbuf_get_input(fbp, &cp, tries, FALSE);
	    if (left == 0) {
		if (fbp->fb_flags & FBF_EOF) { /* Real EOF */
		    if (cp != fbp->fb_ptr) { /* Lingering data */
			np = cp;
			*np++ = 0;
			cp = fbp->fb_ptr;
			fbp->fb_ptr = np;
			fbp->fb_left = 0;
			return cp;
		    }
		}

		/* Error; set EOF and bail */
#if 0
		fbp->fb_flags |= FBF_EOF;
#endif
		return NULL;
	    }
	}

	if ((!cp || !*cp) && left < 0) { /* don't spin */
	    return NULL;
	}

        if (cp)
	    np = memchr(cp, '\n', left);
	if (np) {
	    if(*(np - 1) == '\r')
		*(np - 1) = 0;
	    *np++ = 0;
	    cp = fbp->fb_ptr;
	    fbp->fb_ptr = np;
	    fbp->fb_left -= np - cp;
	    if (fbp->fb_flags & FBF_LINECNT)
		fbp->fb_left += 1;
	    return cp;
	} 

	cp += left;
	left = 0;
    }
}

/*
 * fbuf_xml_type_names[]: define strings for the types returned by
 * fbuf_get_xml().
 */
const char *fbuf_xml_type_names[ NUM_XML_TYPE + 1 ] = {
    "unknown",			/* Unknown or unparsable */
    "error",			/* Error parsing tag/data/etc */
    "proc-insn",		/* Processing instruction */
    "open",			/* <foo> */
    "data",			/* foo-de-foo-de-foo */
    "close",			/* </foo> */
    "comment",			/* <!-- foo --> */
    "eof",			/* EOF */
    "empty-tag",		/* <foo/> */
    "abort",			/* <abort/> */
    "no-op",			/* bogus tag to allow harmless return */
    "]]>]]>",			/* ]]>]]> - tag forcing resync */
    NULL
};

/*
 * fbuf_xml_type: translate a type returned by fbuf_get_xml() into
 * a string
 */
const char *
fbuf_xml_type (int type)
{
    if (0 <= type && type < NUM_XML_TYPE)
	return fbuf_xml_type_names[ type ];
    return "strange";
}

/*
 * split_tag: split the input tag, "ns:elm", into namespace and
 * name.  If there is no namespace, *name is the same as the input tag.
 */
static inline void
split_tag (char *input_tag,
	   char **namespace, char **name)
{
    char *s = input_tag;
    
    while (isalnum((int) *s) || *s == '_' || *s == '-')
	s++;

    if (*s == ':') {
	*namespace = input_tag;
	*s = '\0';
	*name = s + 1;
    } else {
	*namespace = NULL;
	*name = input_tag;
    }
}

/*
 * fbuf_get_xml: get an xml tag, comment, or data from a fbuf input
 * pipe. The string (tag, comment, or data) is returned, but the
 * function will also return an indication of what the input means
 * (processing instruction, open tag, data, close tag, comments, errors,
 * or eof) as well as the attribute portion of the tag. Setting the
 * 'complete' flag allows the function to return multi-line data
 * strings, instead of returning one XML_TYPE_DATA value per line.
 */
#ifndef FBUF_GET_XML_TRIES
/*
 * We want fbuf_get_xml() to be able to return XML_TYPE_NOOP when
 * there is no input available.  If the caller spins (calling us again
 * straight away), we end up doing a lot of needless function calls.
 * Spinning FBUF_GET_XML_TRIES within fbuf_get_input() reduces that
 * overhead but should not be so big a number as to noticably delay
 * return from fbuf_get_xml().
 */
# define FBUF_GET_XML_TRIES 10
#endif
char *
fbuf_get_xml_namespace (fbuf_t *fbp, int *typep, char **namespacep,
			char **restp, unsigned flags)
{
    int left = fbp->fb_left;
    char *cp = fbp->fb_ptr, *np, *sp, *ns = NULL;
    char endchar = 0;
    slax_boolean_t in_xml_open;
    int type;
    slax_boolean_t in_xml_comment;

    /*
     * If we were in an open tag already, initialize ourselves
     */
    in_xml_open = (fbp->fb_flags & FBF_XMLOPEN) ? TRUE : FALSE; /* Save flag */
    if (in_xml_open) {
	fbp->fb_flags &= ~FBF_XMLOPEN; /* Clear flag */
	endchar = '>';
    }

    fbp->fb_start = fbp->fb_line; /* Reset current line pointer */

    /*
     * Main loop: get input, look for end-of-condition, break out.
     */
    in_xml_comment = FALSE;
    for (;;) {
	if (left == 0) {
	    left = fbuf_get_input(fbp, &cp, FBUF_GET_XML_TRIES, FALSE);
	    if (left == 0) {
		if (fbp->fb_flags & FBF_EOF) { /* Real EOF */
		    if (cp != fbp->fb_ptr) {
			np = cp;
			break;
		    }
		} else {
		    /* Error; set EOF and bail */
		    fbp->fb_flags |= FBF_EOF;
		}

		if (typep)
		    *typep = XML_TYPE_EOF;
		if (namespacep)
		    *namespacep = NULL;
		if (restp)
		    *restp = NULL;
		return NULL;

	    } else if (left < 0 && errno == EWOULDBLOCK) {
		static char empty = 0;
		
		if (typep)
		    *typep = XML_TYPE_NOOP;
		if (namespacep)
		    *namespacep = NULL;
		if (restp)
		    *restp = NULL;

		return &empty;
	    }

	    if (fbp->fb_flags & FBF_CHOMPCR) {
		if (*fbp->fb_ptr == '\r') {
		    fbp->fb_ptr += 1;
		    fbp->fb_left -= 1;
		    cp += 1;
		    left -= 1;
		}
		fbp->fb_flags &= ~FBF_CHOMPCR;
		if (left == 0)
		    continue;
	    }
	
	    if (fbp->fb_flags & FBF_CHOMPLF) {
		if (*fbp->fb_ptr == '\n') {
		    fbp->fb_ptr += 1;
		    fbp->fb_left -= 1;
		    cp += 1;
		    left -= 1;
		    if (fbp->fb_flags & FBF_LINECNT)
			fbp->fb_line += 1;
		}
		fbp->fb_flags &= ~FBF_CHOMPLF;
		if (left == 0)
		    continue;
	    }

	    if (iscntrl((int) *fbp->fb_ptr) && !isspace((int) *fbp->fb_ptr)) {
		/*
		 * Control chars are not valid in XML.
		 * Treat \004 as EOF
		 * and bail on the rest.
		 */
		if (*fbp->fb_ptr != '\004')
		    slaxLog("Invalid XML data '\\%#o'", *fbp->fb_ptr);
		fbp->fb_flags |= FBF_EOF;
		if (typep)
		    *typep = XML_TYPE_EOF;
		if (namespacep)
		    *namespacep = NULL;
		if (restp)
		    *restp = NULL;

		return NULL;
	    }
	}

	/* Take a guess at where we are and what we're looking for */
	if (flags & FXF_PRESERVE)
	    sp = fbp->fb_ptr;
	else sp = strtrimws(fbp->fb_ptr); /* Trim leading whitespace */

	if (!in_xml_open) {
	    if (*sp == '<' && !in_xml_comment) {
		if (strncmp(sp, "<!--", 4) == 0)
		    in_xml_comment = TRUE;
		endchar = '>';
	    } else {
		char *ep1, *ep2;
		size_t len;

		/*
		 * Search for ]]>]]> locally.
		 */
		ep1 = memchr(cp, '\n', left);
		ep2 = memchr(cp, '<', left);
		np = ep1 ? ((ep2 && ep2 < ep1) ? ep2 : ep1) : ep2;

		len = (size_t) (np ? np - cp : left);
#ifdef XML_PARSER_RESET
		if (len >= sizeof(XML_PARSER_RESET) - 1 &&
		    (np = strnstr(cp, XML_PARSER_RESET, len))) {
		    type = XML_TYPE_RESYNC;
		    /* Update the fbuf struct with the current point */
		    np += sizeof(XML_PARSER_RESET) - 1;
		    fbp->fb_left -= (np - fbp->fb_ptr);
		    fbp->fb_ptr = np;
		    goto end;
		}
#endif /* XML_PARSER_RESET */
		endchar = '<';
	    }
	}
	/* If we're hit it, break out */
	if (in_xml_comment) {
	    np = strstr(sp + 4, "-->");
	    if (np)
		np += 2; /* let np point to '>' */
	} else
	    np = memchr(cp, endchar, left);
	if (np)
	    break;

	/*
	 * If we're looking for data and aren't after the complete string,
	 * we check for newlines and do the right thing.
	 */
	if (!(flags & FXF_COMPLETE) && !in_xml_open && endchar == '<') {
	    np = memchr(cp, '\n', left);
	    if (np)
		break;
	}

	/* Move along; no match means we need more input */
	cp += left;
	left = 0;
    }

    /* Start at the start */
    cp = fbp->fb_ptr;
    left = fbp->fb_left;

    /*
     * If we're at the end of the tag but we're not in complete
     * mode, we need to see if there's a newline available to us....
     */
    if (endchar == '<' && !(flags & FXF_COMPLETE) && *np == '<') {
	char *zp = memchr(cp, '\n', left);
	if (zp && zp < np)
	    np = zp;
    }

    if (endchar == '>' && (flags & FXF_PRESERVE) && *cp == '\n')
	np = cp;

    /*
     * If the endchar is a '>', we make it the end of the string.
     * If the endchar is a '<', we need to make it the end of string
     * and remember what we were looking at for next time.
     */
    if (*np == '<')
	fbp->fb_flags |= FBF_XMLOPEN;

    if (fbp->fb_flags & FBF_LINECNT) {
	char *lp, *mp;

	for (mp = cp; mp <= np; mp = lp + 1) {
	    lp = memchr(mp, '\n', np - mp + 1);
	    if (lp == NULL)
		break;
	    fbp->fb_line += 1;
	}
    }

    /* NUL terminate the end of the string */
    *np++ = 0;

    /* Update the fbuf struct with the current point */
    fbp->fb_ptr = np;
    fbp->fb_left -= np - cp;
    if (fbp->fb_left < 0)
	fbp->fb_left = 0;

    if (fbp->fb_flags & FBF_LINECNT) {
	char *zp;

	for (zp = cp; zp < np; fbp->fb_left += 1) {
	    zp = memchr(zp, '\n', np - zp);
	    if (zp == NULL)
		break;
	}
    }

    /* Make a better guess at what we are and what we're looking for */
    if (in_xml_open)
	sp = strtrimws(cp);
    else
	sp = strtrimwsc(cp, &fbp->fb_start);

    if (in_xml_open)
	cp = sp;
    else if ((flags & FXF_PRESERVE) && cp == np)
	/*nothing*/;
    else if (*sp == '<') {
	cp = strtrimws(sp + 1);
	in_xml_open = TRUE;
    }

    if (in_xml_open) {		/* We've seen a '<' and want to see more */
	if (*cp == '?') { /* Processing instruction */
	    cp = strtrimws(cp + 1);
	    type = XML_TYPE_ERROR; /* Pessimistic */

	    if (cp <= np - 2) {
		np = strtrimtailws(np - 2, cp);
		if (*np == '?') {
		    *np = 0;
		    type = XML_TYPE_PROC;
		}
	    }

	} else if (*cp == '/') { /* Close tag */
	    cp = strtrimws(cp + 1);
	    if ((flags & FXF_LEAVE_NS) == 0)
		split_tag(cp, &ns, &cp);
	    if (namespacep)
		*namespacep = ns;
	    type = XML_TYPE_CLOSE;

	} else if (*cp == '!') { /* Comment */
	    type = XML_TYPE_ERROR;

	    if (cp[ 1 ] == '-' && cp[ 2 ] == '-') {
		cp = strtrimws(cp + 3);
		if (cp <= np - 3) {
		    if (np[ -3 ] == '-' && np[ -2 ] == '-') {
			np = strtrimtailws(np - 4, cp);
			if (isspace((int) np[ 1 ]))
			    np[ 1 ] = 0;
			else
			    *++np = 0;

			type = XML_TYPE_COMMENT;
		    }
		}
	    }

	    if (restp) {	/* Comments don't use restp */
		*restp = NULL;
		restp = NULL;
	    }

	} else {	/* Open tag */
	    type = XML_TYPE_OPEN;

	    if (cp <= np - 2) {
		np = strtrimtailws(np - 2, cp);
		if ((flags & FXF_LEAVE_NS) == 0)
		    split_tag(cp, &ns, &cp);
		if (namespacep)
		    *namespacep = ns;
		if (*np == '/') {
		    *np = 0;
		    type = XML_TYPE_EMPTY;
#ifdef XMLRPC_ABORT
		    if (*cp == 'a') {
			static const char abort_token[] = XMLRPC_ABORT;
			if (strncmp(cp, abort_token,
				    sizeof(abort_token) - 1) == 0
			    && (cp[ sizeof(abort_token) - 1 ] == 0
				|| isspace((int) cp[sizeof(abort_token) - 1]))) {
			    type = XML_TYPE_ABORT;
			}
		    }
#endif /* XMLRPC_ABORT */
		}
	    }
	}

	/*
	 * Record the 'rest' (attribute portion) of the tag.
	 */
	if (restp) {
	    char *wsp = strnextws(cp);
	    if (*wsp) {
		*wsp++ = 0;
		*restp = *wsp ? wsp : NULL;
	    } else *restp = NULL;
	}

	/*
	 * Sanity hack: ignore one newline following the '>'
	 */
	if (fbp->fb_left == 0)
	    fbp->fb_flags |= FBF_CHOMPCR;
	else if (*fbp->fb_ptr == '\r') {
	    fbp->fb_ptr += 1;
	    fbp->fb_left -= 1;
	}
	
	if (fbp->fb_left == 0)
	    fbp->fb_flags |= FBF_CHOMPLF;
	else if (*fbp->fb_ptr == '\n') {
	    fbp->fb_ptr += 1;
	    fbp->fb_left -= 1;
	    if (fbp->fb_flags & FBF_LINECNT)
		fbp->fb_line += 1;
	}

    } else {		/* Data */
	type = XML_TYPE_DATA;
	if (namespacep)
	    *namespacep = NULL;
	if (restp)
	    *restp = NULL;

	sp = strtrimws(cp);
	if (*sp == 0 && fbp->fb_flags & FBF_EOF) {
	    type = XML_TYPE_EOF;
	    cp = NULL;
	} else {
	    for (np = np - 2; np >= cp; np--)
		if (*np == '\n' || *np == '\r')
		    *np = 0;
		else break;
	}
    }

#ifdef XMLRPC_ABORT
 end:
#endif /* XMLRPC_ABORT */
    if (typep)
	*typep = type;	/* Pass the type back to the caller */
    return cp;
}

/*
 * Use fbuf_get_xml but do not return the namespace information
 */
char *
fbuf_get_xml (fbuf_t *fbp, int *typep, char **restp, unsigned flags)
{
    return fbuf_get_xml_namespace(fbp, typep, NULL, restp, flags);
}

void
fbuf_close (fbuf_t *fbp)
{
    fbuf_pclose(fbp, NULL);
}

/*
 * Similar to fbuf_close, but returns exit status of child process.
 */
void
fbuf_pclose (fbuf_t *fbp, int *status)
{
    if (fbp) {
	if (fbp->fb_trace)
	    (*fbp->fb_trace)(fbp, NULL, 0);
	if (fbp->fb_flags & FBF_CLOSE)
	    close(fbp->fb_fd);

	if (fbp->fb_pid) {
	    int pstat = 0;
	    pid_t pid;

	    do {
		pid = waitpid(fbp->fb_pid, &pstat, 0);
	    } while (pid != -1 || errno == EINTR);

	    if (status) {
		*status = pstat;
	    }
	    if (fbp->fb_stderr)
		close(fbp->fb_stderr);
	}
	
	if (fbp->fb_rec)
	    free(fbp->fb_rec);
	free(fbp->fb_buf);
	free(fbp);
    }
}

static slax_boolean_t
fbuf_has_unread_input(fbuf_t *fbp)
{
    struct timeval timeout = { 0, 0 };
    fd_set who;
    int rc;

    if (fbp->fb_flags & FBF_EOF)
	return FALSE;

    FD_ZERO(&who);
    FD_SET(fbp->fb_fd, &who);

    rc = select(fbp->fb_fd + 1, &who, NULL, &who, &timeout);
    return (rc > 0);
}

/*
 * fbuf_has_pending: return TRUE if the fbuf has input pending
 */
slax_boolean_t
fbuf_has_pending (fbuf_t *fbp)
{
    return fbuf_has_buffered(fbp) || fbuf_has_unread_input(fbp);
}

/*
 * fbuf_is_leading: return TRUE if the fbuf has input pending that
 * matches the given string.
 */
slax_boolean_t
fbuf_is_leading (fbuf_t *fbp, const char *leading)
{
    ssize_t len;

    if (fbp->fb_left == 0)
	fbp->fb_left = fbuf_get_input(fbp, &fbp->fb_ptr, 0, FALSE);

    if (fbp->fb_left == 0)
	return FALSE;

    len = strlen(leading);
    if (len > fbp->fb_left)
	return FALSE;

    return (strncmp(leading, fbp->fb_ptr, len) == 0);
}

#ifdef XMLRPC_ABORT
/*
 * fbuf_has_abort: looks for an abort tag. Return code is:
 *   -1 == no abort (done looking)
 *    0 == no abort in sight (still looking)
 *    1 == abort seen (stop looking)
 */
static int
fbuf_has_abort (const char *cp, const char *ep)
{
    static const char abort_tag[] = XMLRPC_ABORT;
    const int asize = sizeof(abort_tag) - 1;

    /* Traverse the entire string */
    for ( ; cp < ep; cp++) {
	/* Only look at tags */
	if (*cp != '<')
	    continue;

	cp += 1;		/* Move over the '<' */

	/* Not enough data? Return indeterminate results */
	if (ep - cp < asize + 1)
	    return 0;

	/* Does it match the abort tag? Return seen-abort */
	if (memcmp(cp, abort_tag, asize) == 0) {
	    if (cp[ asize ] == '/' || isspace((int) cp[ asize ]))
		return 1;
	}

	/* Is it an open tag? Return no-abort */
	if (*cp != '/')
	    return -1;
    }

    return 0;			/* Return indeterminate results */
}

/*
 * fbuf_aborting: determine whether something in the pipe is
 * asking us to abort.
 */
slax_boolean_t
fbuf_is_aborting (fbuf_t *fbp)
{
    int rc = 0;
    char *cp;

    if (fbp->fb_left)
	rc = fbuf_has_abort(fbp->fb_ptr, fbp->fb_ptr + fbp->fb_left);

    while (rc <= 0 && fbuf_has_unread_input(fbp)) {
	cp = fbp->fb_ptr + fbp->fb_left;

	if (fbuf_get_input(fbp, &cp, 0, (fbp->fb_left != 0)) == 0 )
	    break;

	rc = fbuf_has_abort(cp, fbp->fb_ptr + fbp->fb_left);
    } 

    return rc > 0 ? TRUE : FALSE;
}
#endif /* XMLRPC_ABORT */

typedef struct fbuf_trace_tagged_s {
    FILE *ftt_fp;
    const char *ftt_tag;
} fbuf_trace_tagged_t;

static void
fbuf_trace_tagged_callback (FBUF_TRACE_ARGS)
{
    fbuf_trace_tagged_t *fttp = fbuf_get_trace_cookie(fbp);
    const char *cp, *np, *ep;
    
    if (buf == NULL) {		/* NULL is the signal to shutdown/cleanup */
	free(fttp);
	return;
    }

    for (cp = buf, ep = buf + buflen; cp < ep; cp = np) {
	np = index(cp, '\n');
	if (np == NULL)
	    np = ep;
	fprintf(fttp->ftt_fp, "%s: %.*s\n", fttp->ftt_tag, (int)(np - cp), cp);
	if (np < ep && *np == '\n')
	    np += 1;
    }

    fflush(fttp->ftt_fp);
}

slax_boolean_t
fbuf_trace_tagged (fbuf_t *fbp, FILE *fp, const char *tag)
{
    fbuf_trace_tagged_t *fttp;

    fttp = calloc(sizeof(*fttp), 1);
    if (fttp == NULL)
	return TRUE;

    fttp->ftt_fp = fp;
    fttp->ftt_tag = tag;

    fbuf_set_trace_cookie(fbp, fttp);
    fbuf_set_trace_func(fbp, fbuf_trace_tagged_callback);
    return FALSE;
}

fbuf_t *
fbuf_from_string (char *data, int len)
{
    fbuf_t *fbp;

    fbp = calloc(1, sizeof(*fbp));
    if (fbp == NULL)
	return NULL;

    fbp->fb_fd = -1;
    fbp->fb_flags = FBF_EOF | FBF_STRING;

    fbp->fb_buf = fbp->fb_ptr = data;
    fbp->fb_size = fbp->fb_size_limit = len;

    return fbp;
}

fbuf_t *
fbuf_from_const_string (const char *const_data, int len)
{
    char *data = malloc(len + 1);
    fbuf_t *fbp;

    if (data == NULL)
	return NULL;

    memcpy(data, const_data, len);
    data[len] = '\0';

    fbp = fbuf_from_string(data, len);

    if (fbp == NULL)
	free(data);

    return fbp;
}

#ifdef UNIT_TEST
#include <termios.h>

int
main (int argc, char **argv)
{
    char *cp;
    fbuf_t *fbp = fbuf_fdopen(0, 0);
    int type;
    char *rp;
    slax_boolean_t xml_style = TRUE, full = FALSE, line = FALSE;
    int opt, flags = 0;
    char *trace_tag = NULL;
    struct termios save, raw;

    save.c_iflag = 0;
    
    while ((opt = getopt(argc, argv, "cflnt:rR")) != EOF) {
        switch (opt) {
	case 'c':
	    flags ^= FXF_COMPLETE;
	    break;
	case 'f':
	    full = !full;
	    break;
	case 'l':
	    line = !line;
	    break;
	case 'n':
	    xml_style = !xml_style;
	    break;
	case 't':
	    trace_tag = optarg;
	    break;
	case 'r':
	    flags ^= FXF_PRESERVE;
	    break;
	case 'R':
	    if (isatty(STDIN_FILENO)) {
		tcgetattr(STDIN_FILENO, &save);
		raw = save;
		raw.c_iflag &= ~(INLCR|IGNCR|ICRNL);
		raw.c_lflag &= ~(ECHOKE|ECHOE|ECHO|ECHONL|ICANON);

		tcsetattr(STDIN_FILENO, TCSANOW, &raw);
	    }
	    break;
	}
    }

    if (trace_tag)
	fbuf_trace_tagged(fbp, stderr, trace_tag);

    if (line)
	fbuf_reset_linecnt(fbp);

    while (!fbuf_eof(fbp)) {
	if (xml_style) {
	    cp = fbuf_get_xml(fbp, &type, &rp, flags);
	    if (full) {
		printf("[%s] type %d/%s [%s] %x (line %d/%d)\n",
		       cp ?: "NULL", type, fbuf_xml_type(type), rp ?: "NULL",
		       fbp->fb_flags, fbp->fb_start, fbp->fb_line);
	    } else {
		printf("[%s]\n", cp ?: "(NULL)");
	    }
	} else {
	    cp = fbuf_gets(fbp);
	    printf("[%s]\n", cp ?: "(NULL)");
	}
    }

    fbuf_close(fbp);

    if (save.c_iflag)
	tcsetattr(STDIN_FILENO, TCSANOW, &save);

    return 0;
}
#endif
