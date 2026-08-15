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

/*
 * An XML tokenizer for a subset of XML.  Yes, it's subset.  It
 * doesn't parse any of the things I don't like, including the DTD
 * crap, includes, ignores, entities, CDATA, etc.  XML is about
 * concise, predictable, precise, extensible, machine-to-machine
 * communications.  These features are short-cuts for humans.  DTDs
 * were a mistake and should be rendered "obsolete".  The spec even
 * admits that "These simple rules may have complex interactions".
 * A pox on "<!"!!
 *
 * The benefit of ignoring those historical oddities is that it allows
 * us to build a small, high-speed parser.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>

#include <libpsu/psucommon.h>
#include <parrotdb/pacommon.h>
#include <libpin/pin_common.h>
#include <libpin/pin_source.h>

#define PIN_PI	"processing instruction"

#define PIN_BUFSIZ	8192	/* Default buffer size */
#define PIN_BUFSIZ_COPY_WHEN (PIN_BUFSIZ - 1024)
#define PIN_BUFSIZ_MIN	4096	/* Minimum space for reading data */
#define PIN_BUFSIZ_FAIL	512	/* Absolute minimum space for reading data */

/* This array is used by pin_isspace to find writespace bytes */
char pin_space_test[256]	= { [0x20] = 1, [0x09] = 1, [0x0d] = 1, [0x0a] = 1 };

void
pin_source_failure (pin_source_t *srcp, int errnum, const char *fmt, ...)
{
    va_list vap;

    va_start(vap, fmt);

    if (srcp) {
	if (srcp->pps_flags & PPSF_LINE_NO) {
	    fprintf(stderr, "%s:%u:(%u): ",
		    srcp->pps_filename ?: "input",
		    srcp->pps_lineno, srcp->pps_offset);
	} else {
	    fprintf(stderr, "%s:(%u): ",
		    srcp->pps_filename ?: "input", srcp->pps_offset);
	}
    }

    fprintf(stderr, "warning: ");
    vfprintf(stderr, fmt, vap);
    fprintf(stderr, "%s%s\n", (errnum > 0) ? ": " : "",
	    (errnum > 0) ? strerror(errnum) : "");
    fflush(stderr);

    va_end(vap);
}

/*
 * Open an pin_source_t for the given file descriptor.
 */
pin_source_t *
pin_source_create (int fd, pin_source_flags_t flags)
{
    pin_source_t *srcp;

    srcp = calloc(1, sizeof(*srcp));
    if (srcp != NULL) {
	srcp->pps_fd = fd;
	srcp->pps_flags = flags & ~PPSF_MMAP_INPUT;
	srcp->pps_lineno = 1;	/* Start on line 1 */

	/*
	 * The mmap flag asks us to try to mmap the file; if it fails,
	 * we fall back to normal behavior.
	 */
	if (flags & PPSF_MMAP_INPUT) {
	    struct stat st;

	    if (fstat(fd, &st) >= 0 && st.st_size > 0) {
		void *addr = mmap(NULL, st.st_size, PROT_READ, 0, fd, 0);
		if (addr) {
		    srcp->pps_flags |= PPSF_MMAP_INPUT | PPSF_NO_READ;
		    srcp->pps_bufp = srcp->pps_curp = addr;
		    srcp->pps_size = st.st_size;
		}
	    }
	}

	/* If needed, allocate an initial buffer */
	if (srcp->pps_bufp == NULL) {
	    srcp->pps_bufp = srcp->pps_curp = calloc(1, PIN_BUFSIZ);
	    if (srcp->pps_bufp != NULL)
		srcp->pps_size = PIN_BUFSIZ;
	}
    }

    return srcp;
}

/*
 * Open an pin_source_t for the given file.
 */
pin_source_t *
pin_source_open (const char *filename, pin_source_flags_t flags)
{
    int fd = open(filename, O_RDONLY);
    if (fd < 0)
	return NULL;

    pin_source_t *srcp;
    srcp = pin_source_create(fd, flags | PPSF_CLOSE_FD);
    if (srcp)
	srcp->pps_filename = strdup(filename);

    return srcp;
}

/*
 * Destroy an pin_source_t, releasing all resource, including the
 * file descriptor if PPSF_CLOSE_FD is set.  Any further referencing
 * of the pin_source_t is prohibited by law.
 */
void
pin_source_destroy (pin_source_t *srcp)
{
    if (srcp->pps_filename != NULL)
	free(srcp->pps_filename);

    if (srcp->pps_flags & PPSF_MMAP_INPUT) {
	if (srcp->pps_bufp != NULL)
	    munmap(srcp->pps_bufp, srcp->pps_size);
    } else {
	if (srcp->pps_bufp != NULL)
	    free(srcp->pps_bufp);
    }

    if (srcp->pps_flags & PPSF_CLOSE_FD)
	close(srcp->pps_fd);

    free(srcp);
}

/*
 * Unescape XML text data.  This is not done automatically since
 * if the caller is just copying data from input to output, there's
 * no reason to unescape data that will need escaping.
 */
size_t
pin_source_unescape (pin_source_t *srcp, char *start, unsigned len)
{
    /* First byte is the unescaped form; the rest is the entity form */
    static const char *entities[] = {
	"&amp;", "<lt;", ">gt;", "'&apos;", "\"quot;", NULL
    };

    size_t rc = len, elen = 0;
    char *cur = start, *ins = NULL, *from = cur;
    const char **ep;

    while (len > 0) {
	cur = psu_memchr(cur, '&', len);
	if (cur == NULL)
	    break;

	len -= cur - from;
	for (ep = entities; *ep; ep++) {
	    elen = strlen(*ep + 1);
	    if (memcmp(*ep + 1, cur + 1, elen) == 0)
		break;
	}

	if (*ep == NULL) {
	    /* We didn't find the entity; bummer */
	    pin_source_failure(srcp, 0, "could not decode entity");

	    char *xp = psu_memchr(cur + 1, ';', len);
	    if (xp != NULL) {
		/*
		 * Okay, the entity is trash, but at least we found a
		 * semi-colon.  Move along, discarding the broken entity.
		 */
		ins = cur;
		cur = from = xp;
	    } else {
		len -= 1;	/* Skip single character ('&') */
		cur += 1;
	    }
	    continue;

	}

	if (ins == NULL) {
	    *cur++ = **ep;	/* Replace '&' with unencoded form */
	    ins = cur;		/* Point where we'll copy data (next time) */

	} else {
	    size_t plen = cur - from; /* Length of uncopied data */
	    memcpy(ins, from, plen);
	    ins += plen;
	    *ins++ = **ep;	/* Insert unencoded form */
	    cur += 1;		/* Skip '&' */
	}

	/* Pointer maintenance */
	cur += elen;		/* Skip over the rest of  */
	from = cur;		/* Point where we'll copy from */
	len -= elen + 1;
	rc -= elen;
    }

    /* If there's anything left over, copy it */
    if (ins != NULL && len > 0)
	memcpy(ins, from, len);

    return rc;
}

static void
pin_source_move_curp (pin_source_t *srcp, char *newp)
{
    char *cp = srcp->pps_curp;

    srcp->pps_offset += newp - cp;
    if (srcp->pps_flags & PPSF_LINE_NO) {
	while (cp < newp) {
	    cp = psu_memchr(cp, '\n', newp - cp);
	    if (cp == NULL)
		break;
	    srcp->pps_lineno += 1;
	    cp += 1;
	}
    }

    srcp->pps_curp = newp;
}

/*
 * Read some input data from the source.  If min is non-zero, it's the
 * minimum number of bytes we'd like to see.
 */
static int
pin_source_read (pin_source_t *srcp, int min)
{
    if (srcp->pps_flags & (PPSF_NO_READ | PPSF_EOF_SEEN))
	return -1;

    unsigned seen = srcp->pps_curp - srcp->pps_bufp;
    unsigned left = srcp->pps_len - seen;

    if (left == 0) {
	/* If we've consumed all data, reset it to initial state */
	srcp->pps_curp = srcp->pps_bufp; /* Reset curp */
	srcp->pps_len = 0;

    } else if (left > 0 && seen > PIN_BUFSIZ_COPY_WHEN) {
	/*
	 * If we've got data left to copy and we're close to the end,
	 * copy it.
	 */
	memcpy(srcp->pps_bufp, srcp->pps_curp, left);
	srcp->pps_len = left;
	srcp->pps_curp = srcp->pps_bufp;
    }

    seen = srcp->pps_curp - srcp->pps_bufp; /* Refresh 'seen' */

    /* If there's not enough room, expand the buffer */
    pin_offset_t space = srcp->pps_size - srcp->pps_len;
    if (space < PIN_BUFSIZ_MIN) {
	unsigned size = srcp->pps_size << 1; /* Double the buffer size */
	char *cp = realloc(srcp->pps_bufp, size);
	if (cp != NULL) {
	    /* Record new buffer pointer values */
	    srcp->pps_size = size;
	    srcp->pps_curp = cp + seen;
	    srcp->pps_bufp = cp;

	} else if (srcp->pps_size - seen < PIN_BUFSIZ_FAIL)
	    return -1;		/* Not enough room to bother reading */
    }

    /*
     * Read as much data as we can, remembering that we may have existing
     * data already in the buffer.  The first 'pps_len' bytes are precious.
     */
    int rc = read(srcp->pps_fd, srcp->pps_bufp + srcp->pps_len,
		  srcp->pps_size - srcp->pps_len);
    if (rc <= 0) {
	srcp->pps_flags |= PPSF_EOF_SEEN;
	return -1;
    }

    srcp->pps_len += rc;

    return (rc >= min);
}

static inline pin_offset_t
pin_source_offset (pin_source_t *srcp)
{
    return srcp->pps_curp - srcp->pps_bufp;
}

static inline pin_offset_t
pin_source_left (pin_source_t *srcp)
{
    pin_offset_t seen = srcp->pps_curp - srcp->pps_bufp;
    return srcp->pps_len - seen;
}

static inline pin_offset_t
pin_source_avail (pin_source_t *srcp, pin_offset_t min)
{
    pin_offset_t left = pin_source_left(srcp);

    if (left >= min)
	return left;

    return pin_source_read(srcp, min);
}

static pin_offset_t
pin_source_find (pin_source_t *srcp, int ch, pin_offset_t offset)
{
    char *cur;

    for (;;) {
	if (offset >= srcp->pps_len) {
	    if (pin_source_read(srcp, 0) < 0)
		return -1;
	    offset = pin_source_offset(srcp); /* Recalculate our offset */
	}

	cur = psu_memchr(srcp->pps_bufp + offset, ch, srcp->pps_len - offset);
	if (cur != NULL) {
	    /* We've found it; return the offset */
	    return cur - srcp->pps_bufp;
	}

	offset = srcp->pps_len;
    }
}

/*
 * Deal with comments.
 *
 * XXX We don't enforce the XML prohibition on the use of "--" within
 * comments.  It makes no sense and is really just a CLR (crummy
 * little rule).  But we should check this if PPSF_VALIDATE is set.
 */
static pin_node_type_t
pin_source_token_comment (pin_source_t *srcp, char **datap,
			char **restp UNUSED)
{
    const int SKIP_LEN = 4;	/* strlen("<!--") */
    char *dp, *cp;
    pin_offset_t off;

    if (pin_source_avail(srcp, SKIP_LEN) <= 0) {
	/* Failure; premature EOF */
	pin_source_failure(srcp, 0, "premature end-of-file: comment");
	return PIN_TYPE_FAIL;
    }

    off = pin_source_offset(srcp) + SKIP_LEN; /* Skip "<!--" */

    for (;;) {
	off = pin_source_find(srcp, '>', off);
	if (off < 0) {
	    pin_source_failure(srcp, 0, "missing termination of comment");
	    return PIN_TYPE_FAIL;
	}

	cp = &srcp->pps_bufp[off];
	if (cp[-1] == '-' && cp[-2] == '-')
	    break;

	off += 1;
    }

    dp = srcp->pps_curp + SKIP_LEN;
    cp[-2] = '\0';		/* 2 for "--" */
    pin_source_move_curp(srcp, cp + 1);

    if (srcp->pps_flags & PPSF_IGNORE_COMMENTS)
	return PIN_TYPE_SKIP;

    if (srcp->pps_flags & PPSF_TRIM_WS) {
	cp -= 2;			/* 2 for "--" */
	dp = pin_skipws(dp, cp - dp, 1); /* Trim leading ws */
	if (dp != NULL) {
	    cp -= 1;
	    cp = pin_skipws(cp, cp - dp, -1); /* Trim trailing ws */
	    if (cp != NULL)
		*++cp = '\0';
	}
    }

    *datap = dp;

    return PIN_TYPE_COMMENT;
}

/*
 * Look for the terminating bit of a "<!DOCTYPE name [ ]>".  Returns a
 * pointer to the tailing '>' character.  Note that the spec allows
 * whitespace between the "]" and the ">", which makes no sense.
 */
static char *
pin_source_find_brklt1 (pin_source_t *srcp, pin_offset_t off)
{
    char *cp, *wp;

    for (;;) {
	off = pin_source_find(srcp, '>', off);
	if (off < 0)
	    return NULL;

	cp = &srcp->pps_bufp[off];
	wp = cp - 1;
	wp = pin_skipws(wp, wp - srcp->pps_bufp, -1); /* Trim trailing ws */
	if (wp != NULL)
	    cp = wp + 1;


	if (cp[-1] == ']')
	    return cp;

	off += 1;
    }
}

/*
 * Look for the terminating bit of a "<![CDATA[ ]]>".  Returns a pointer
 * to the tailing '>' character.  Note that the spec does not allow
 * any whitespace between the "]]>" characters, nor between the opening
 * characters.
 */
static char *
pin_source_find_brklt2 (pin_source_t *srcp, pin_offset_t off)
{
    char *cp;

    for (;;) {
	off = pin_source_find(srcp, '>', off);
	if (off < 0)
	    return NULL;

	cp = &srcp->pps_bufp[off];
	if (cp[-1] == ']' && cp[-2] == ']')
	    return cp;

	off += 1;
    }
}

/*
 * DTDs are evil.  We don't support them.  But we need to parse them
 * enough to ignore them, which is not as easy as one would hope.  The
 * twisty little rules are oddly different for each little featurette.
 * You are likely to be eaten by a grue.
 */
static pin_node_type_t
pin_source_token_dtd (pin_source_t *srcp, char **datap, char **restp)
{
    pin_offset_t off = pin_source_find(srcp, '>', pin_source_offset(srcp));
    if (off < 0) {
	pin_source_failure(srcp, 0, "missing termination of dtd tag");
	return PIN_TYPE_FAIL;
    }

    char *dp = srcp->pps_curp + 2; /* 2 for "<!" */
    char *cp = &srcp->pps_bufp[off];

    /*
     * Find the attributes, but don't bother parsing them.  Trim whitespace.
     */
    char *rp = psu_memchr(dp, ' ', cp + 1 - dp);
    if (rp != NULL) {
	*rp++ = '\0';
	rp = pin_skipws(rp, cp + 1 - rp, 1);
	if (rp != NULL && *rp == '\0')
	    rp = NULL;
	else if (strcmp(dp, "DOCTYPE") == 0) {
	    /*
	     * <!DOCTYPE> is it's own little bit of hell.  We need to handle
	     * the case where an internal DTD appears as a chunk of XML
	     * with the <!DOCTYPE> tag.  Nested tags.  How wonderful.
	     */
	    char *xp = psu_memchr(rp, ' ', cp + 1 - rp);
	    if (xp != NULL) {
		xp = pin_skipws(xp, strlen(xp), 1);
		if (xp) {
		    char *zp = psu_memchr(xp, ' ', cp + 1 - xp);
		    if (xp[0] == '[' || (zp != NULL && zp[1] == '[')) {
			/*
			 * Bad news!  The input has an internal DTD, which
			 * means finding the ">" wasn't enough.  We need to
			 * find the terminating "]>".  For details:
			 * https://www.w3.org/TR/xml/#NT-intSubset
			 */
			cp = pin_source_find_brklt1(srcp, xp - srcp->pps_bufp);
		    }
		}
	    }
	}
    }

    *cp++ = '\0';		/* Whack the '>' */
    pin_source_move_curp(srcp, cp); /* Save as next starting point */

    if (srcp->pps_flags & PPSF_IGNORE_DTD)
	return PIN_TYPE_SKIP;

    *datap = dp;
    *restp = rp;

    return PIN_TYPE_DTD;
}

/*
 * Handle the case where we see "<![".  Don't panic.  We're mostly
 * ignoring these bits of fluff, but we do handle CDATA, because
 * it's fairly trivial.
 */
static pin_node_type_t
pin_source_token_bracket (pin_source_t *srcp, char **datap UNUSED,
			char **restp UNUSED)
{
    if (pin_source_avail(srcp, 4) <= 0) {
	/* Failure; premature EOF */
	pin_source_failure(srcp, 0, "premature end-of-file: bracket");
	return PIN_TYPE_FAIL;
    }

    char *dp = srcp->pps_curp;
    int cdata = (strncmp(dp, "<![CDATA[", 9) == 0);
    dp += 9;

    pin_offset_t off = pin_source_offset(srcp) + 3; /* Skip "<![" */
    char *cp = pin_source_find_brklt2(srcp, off);
    if (cp == NULL) {
	pin_source_failure(srcp, 0, "premature end-of-file: bracket");
	return PIN_TYPE_FAIL;
    }

    cp[-2] = '\0';
    pin_source_move_curp(srcp, cp + 1);

    if (cdata) {
	*datap = dp;
	*restp = cp - 2;
	return PIN_TYPE_CDATA;
    }

    return PIN_TYPE_SKIP;
}

/*
 * XML magical happenings!!  Unfortunately, the XML spec create an
 * annoyingly complex set of features.  The most common is the comment
 * (<!-- comment -->) but there are also <!DOCTYPE>, <!ELEMENT>,
 * <!ATTLIST>, <![IGNORE[]]>, <![INCLUDE[]]>, and everyone's favorite,
 * <!ENTITY>.  Don't get me started....
 *
 * Fortunately, most of these can be (read: will be) ignored (for now).
 * And since they are chiefly targeted as human writers and IMHO
 * humans should never need to write XML content, we're really
 * not losing anything.
 */
static pin_node_type_t
pin_source_token_magic (pin_source_t *srcp, char **datap,
		      char **restp)
{
    if (pin_source_avail(srcp, 4) <= 0) {
	/* Failure; premature EOF */
	pin_source_failure(srcp, 0, "premature end-of-file: xml");
	return PIN_TYPE_FAIL;

    } else if (srcp->pps_curp[2] == '-' && srcp->pps_curp[3] == '-') {
	/* Comment tag */
	return pin_source_token_comment(srcp, datap, restp);

    } else if (srcp->pps_curp[2] == '[') {
	/* 'bracket' == '<![xxx]]>'.  We ignore the conents */
	return pin_source_token_bracket(srcp, datap, restp);

    } else if (isalpha((int) srcp->pps_curp[2])) {
	/* <!XXX> tag */
	return pin_source_token_dtd(srcp, datap, restp);

    } else {
	pin_source_failure(srcp, 0, "unhandled xml magic");
	return PIN_TYPE_FAIL;
    }
}

static pin_node_type_t
pin_source_token_pi (pin_source_t *srcp, char **datap,
		   char **restp)
{
    if (pin_source_avail(srcp, 4) <= 0) {
	/* Failure; premature EOF */
	pin_source_failure(srcp, 0, "premature end-of-file: " PIN_PI);
	return PIN_TYPE_FAIL;
    }

    pin_offset_t off = pin_source_find(srcp, '>', pin_source_offset(srcp));
    if (off < 0) {
	pin_source_failure(srcp, 0, "missing termination of " PIN_PI);
	return PIN_TYPE_FAIL;
    }

    char *dp = srcp->pps_curp + 2;
    char *cp = &srcp->pps_bufp[off];
    char *ep = cp;

    if (ep < dp + 2 || ep[-1] != '?') {
	pin_source_failure(srcp, 0, "invalid termination of " PIN_PI);
	return PIN_TYPE_FAIL;
    }

    *--ep = '\0';		/* Whach the '?' */
    *cp++ = '\0';		/* Whack the '>' */
    pin_source_move_curp(srcp, cp); /* Save as next starting point */

    /* Should not be any sort of whitespace before the target, but .. */
    dp = pin_skipws(dp, ep - dp, 1);
    if (dp == NULL) {
	pin_source_failure(srcp, 0, "invalid target of " PIN_PI);
	return PIN_TYPE_FAIL;
    }

    /*
     * Find the attributes, but don't bother parsing them.  Trim whitespace.
     */
    char *rp = psu_memchr(dp, ' ', ep - dp);
    if (rp != NULL) {
	*rp++ = '\0';
	rp = pin_skipws(rp, ep - rp, 1);
	if (rp != NULL && *rp == '\0')
	    rp = NULL;
    }

    *datap = dp;
    *restp = rp;

    return PIN_TYPE_PI;
}

static pin_node_type_t
pin_source_token_open (pin_source_t *srcp, char **datap, char **restp)
{
    pin_node_type_t token = PIN_TYPE_OPEN;

    pin_offset_t off = pin_source_find(srcp, '>', pin_source_offset(srcp));
    if (off < 0) {
	pin_source_failure(srcp, 0, "missing termination of open tag");
	return PIN_TYPE_FAIL;
    }

    char *dp = srcp->pps_curp + 1;
    char *cp = &srcp->pps_bufp[off];

    if (dp < cp && cp[-1] == '/') { /* Spec says no space between "/>" */
	token = PIN_TYPE_EMPTY;
	cp[-1] = '\0';		/* Back up over '/' */
    }

    *cp++ = '\0';		/* Whack the '>' */
    pin_source_move_curp(srcp, cp); /* Save as next starting point */

    /*
     * Find the attributes, but don't bother parsing them.  Trim whitespace.
     */
    char *rp = psu_memchr(dp, ' ', cp - dp);
    if (rp != NULL) {
	*rp++ = '\0';
	rp = pin_skipws(rp, cp - rp, 1);
	if (rp != NULL && *rp == '\0')
	    rp = NULL;
    }

    *datap = dp;
    *restp = rp;

    return token;
}

static pin_node_type_t
pin_source_token_close (pin_source_t *srcp, char **datap)
{
    pin_offset_t off = pin_source_find(srcp, '>', pin_source_offset(srcp));
    if (off < 0) {
	pin_source_failure(srcp, 0, "missing termination of close tag");
	return PIN_TYPE_FAIL;
    }

    char *dp = srcp->pps_curp + 2; /* Skip "</" */
    char *cp = &srcp->pps_bufp[off];
    *cp++ = '\0';		/* Whack the '>' */
    pin_source_move_curp(srcp, cp); /* Save as next starting point */

    *datap = dp;

    return PIN_TYPE_CLOSE;
}

static pin_node_type_t
pin_source_token_text (pin_source_t *srcp, char **datap, char **restp)
{
    pin_offset_t off = pin_source_find(srcp, '<', pin_source_offset(srcp));
    if (off < 0) {
	pin_offset_t left = pin_source_left(srcp);
	if (left == 0) {
	    pin_source_failure(srcp, 0, "missing termination of text");
	    return PIN_TYPE_FAIL;
	}

	off = srcp->pps_len;
    }

    /*
     * We can't NUL-terminate our strings, so we use 'restp' to return
     * the end-of-string marker.
     */
    char *dp = srcp->pps_curp;
    char *cp = &srcp->pps_bufp[off];
    pin_source_move_curp(srcp, cp); /* Save as next starting point */

    if (srcp->pps_flags & PPSF_TRIM_WS) {
	dp = pin_skipws(dp, cp - dp, 1); /* Trim leading ws */
	if (dp == NULL)			/* Nothing but ws */
	    cp = NULL;
	else {
	    char *wp = cp - 1;
	    wp = pin_skipws(wp, wp - dp, -1); /* Trim trailing ws */
	    if (wp != NULL)
		cp = wp + 1;
	}
    }

    *datap = dp;
    *restp = cp;		/* Restp is the end of text */

    return PIN_TYPE_TEXT;
}

static void
pin_source_ignorews (pin_source_t *srcp)
{
    pin_offset_t off = pin_source_offset(srcp); /* Starting point */
    char *cp;

    for (cp = &srcp->pps_bufp[off]; pin_isspace(*cp); cp++, off++) {
	if (off >= srcp->pps_len) {
	    if (pin_source_read(srcp, 0) < 0)
		return;
	    cp = &srcp->pps_bufp[off]; /* Refresh */
	}
    }

    if (*cp != '<')
	return;

    pin_source_move_curp(srcp, cp);	/* Skip over whitespace */
}


/*
 * Parse the next token.  This is really the main entry point of the
 * parsing functions, functioning as a "pull" parser.
 */
pin_node_type_t
pin_source_next_token (pin_source_t *srcp, char **datap, char **restp)
{
    pin_node_type_t token;

    for (;;) {
	*datap = *restp = NULL;	/* Clear pointers */

	if (srcp->pps_last != PIN_TYPE_TEXT
	    && (srcp->pps_flags & PPSF_IGNORE_WS)) {
	    /* Look for the next non-space character; if it's '<', we skip */
	    pin_source_ignorews(srcp);
	}

	/* If we don't have data, go get some data */
	if (pin_source_left(srcp) == 0) {
	    if (pin_source_read(srcp, 0) < 0)
		return PIN_TYPE_EOF;
	}

	if (srcp->pps_curp[0] != '<') {
	    /* Text data */
	    token = pin_source_token_text(srcp, datap, restp);

	    /* If there's no real text data, then we've assumably trimmed it */
	    if (*datap == NULL || *datap == *restp)
		continue;

	} else if (pin_source_avail(srcp, 2) <= 0) {
	    /* Failure; premature EOF */
	    pin_source_failure(srcp, 0, "premature end-of-file: open-tag");
	    token = PIN_TYPE_EOF;

	} else if (srcp->pps_curp[1] == '/') {
	    /* Close tag */
	    token = pin_source_token_close(srcp, datap);

	} else if (srcp->pps_curp[1] == '!') {
	    token = pin_source_token_magic(srcp, datap, restp);
	    
	} else if (srcp->pps_curp[1] == '?') {
	    /* Processing instruction */
	    token = pin_source_token_pi(srcp, datap, restp);
	} else {
	    /* Open tag */
	    token = pin_source_token_open(srcp, datap, restp);
	}

	/*
	 * If the parsing functions returned PIN_TYPE_SKIP then they
	 * properly parsed something that we just don't care about.
	 * Mostly this is from the various PPSF_IGNORE* flags, but
	 * sometimes it's just from apathy.  Regardless, we repeat the
	 * loop, and hoping to find something meaningful.
	 */
	if (token != PIN_TYPE_SKIP)
	    break;
    }

    srcp->pps_last = token;
    return token;
}
