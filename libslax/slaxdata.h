/*
 * $Id$
 *
 * Copyright (c) 2010-2011, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#ifndef LIBSLAX_SLAXDATA_H
#define LIBSLAX_SLAXDATA_H

#include <libslax/xmlsoft.h>

typedef struct slax_data_node_s {
    TAILQ_ENTRY(slax_data_node_s) dn_link; /* Next session */
    int dn_len; 		/* Length of the chunk of data */
    char dn_data[0];		/* Data follows this header */
} slax_data_node_t;

typedef TAILQ_HEAD(slax_data_list_s, slax_data_node_s) slax_data_list_t;

static inline void
slaxDataListInit (slax_data_list_t *listp)
{
    TAILQ_INIT(listp);
}

static inline slax_data_list_t *
slaxDataListCheckInit (slax_data_list_t *listp)
{
    if (listp->tqh_last == NULL)
	TAILQ_INIT(listp);
    return listp;
}

static inline slax_data_node_t *
slaxDataListAddLen (slax_data_list_t *listp, const char *buf, size_t len)
{
    slax_data_node_t *dnp;

    if (listp == NULL)
	return NULL;

    slaxDataListCheckInit(listp);

    /*
     * We allocate an extra byte to allow us to NUL terminate it.  The
     * data is opaque to us, but will likely be a string, so we want
     * to allow this option.
     */
    dnp = xmlMalloc(sizeof(*dnp) + len + 1);
    if (dnp) {
	dnp->dn_len = len;
	memcpy(dnp->dn_data, buf, len);
	dnp->dn_data[len] = '\0';
	TAILQ_INSERT_TAIL(listp, dnp, dn_link);
    }

    return dnp;
}

static inline slax_data_node_t *
slaxDataListAddLenNul (slax_data_list_t *listp, const char *data, size_t len)
{
    slax_data_node_t *dnp;

    dnp = slaxDataListAddLen(listp, data, len + 1);
    if (dnp)
	dnp->dn_data[len] = '\0';

    return dnp;
}

static inline slax_data_node_t *
slaxDataListAddNul (slax_data_list_t *listp, const char *data)
{
    return slaxDataListAddLenNul(listp, data, strlen(data));
}

static inline slax_data_node_t *
slaxDataListAdd (slax_data_list_t *listp, const char *data)
{
    return slaxDataListAddLen(listp, data, strlen(data) + 1);
}

static inline void
slaxDataListCopy (slax_data_list_t *top, slax_data_list_t *fromp)
{
    slax_data_node_t *dnp, *newp;

    slaxDataListCheckInit(top);
    slaxDataListCheckInit(fromp);

    TAILQ_FOREACH(dnp, fromp, dn_link) {
	newp  = xmlMalloc(sizeof(*dnp) + dnp->dn_len + 1);
	if (newp == NULL)
	    break;

	bzero(newp, sizeof(*newp));
	newp->dn_len = dnp->dn_len;
	memcpy(newp->dn_data, dnp->dn_data, dnp->dn_len + 1);

	TAILQ_INSERT_TAIL(top, dnp, dn_link);
    }
}

static inline void
slaxDataListClean (slax_data_list_t *listp)
{
    slax_data_node_t *dnp;

    slaxDataListCheckInit(listp);

    for (;;) {
	dnp = TAILQ_FIRST(listp);
        if (dnp == NULL)
            break;
        TAILQ_REMOVE(listp, dnp, dn_link);
	xmlFree(dnp);
    }
}

/* Cannot put this inside a "do { .. } while (0)" */
#define SLAXDATALIST_FOREACH(_dnp, _listp) \
    slaxDataListCheckInit(_listp); \
    TAILQ_FOREACH(_dnp, _listp, dn_link)

#define SLAXDATALIST_EMPTY(_listp) TAILQ_EMPTY(slaxDataListCheckInit(_listp))

static inline size_t
slaxDataListAsCharLen (slax_data_list_t *listp, const char *sep)
{
    size_t slen = sep ? strlen(sep) : 0;
    slax_data_node_t *dnp;
    size_t result = 1;

    SLAXDATALIST_FOREACH(dnp, listp) {
	result += dnp->dn_len + slen;
    }

    return result;
}

static inline char *
slaxDataListAsChar (char *buf, size_t bufsiz,
		    slax_data_list_t *listp, const char *sep)
{
    size_t slen = sep ? strlen(sep) : 0;
    slax_data_node_t *dnp;
    char *cp = buf;
    size_t left = bufsiz;

    if (left <= 0)
	return NULL;

    SLAXDATALIST_FOREACH(dnp, listp) {
	if (dnp->dn_len + slen >= left)
	    break;

	if (cp != buf) {
	    memcpy(cp, sep, slen);
	    cp += slen;
	    left -= slen;
	}

	memcpy(cp, dnp->dn_data, dnp->dn_len);
	cp += dnp->dn_len;
	left -= dnp->dn_len;
    }

    *cp = '\0';
    return buf;
}

/*
 * Add a directory to the list of directories searched for files
 */
static inline void
slaxDataListAddDir (slax_data_list_t *where, int *inited, const char *dir)
{
    if (!*inited) {
	*inited = TRUE;
	slaxDataListInit(where);
    }

    slaxDataListAddNul(where, dir);
}

/*
 * Add a set of directories to the list of directories searched for files
 */
static inline void
slaxDataListAddPath (slax_data_list_t *where, int *inited, const char *dir)
{
    char *buf = NULL;
    int buflen = 0;
    const char *cp;

    while (dir && *dir) {
	cp = strchr(dir, ':');
	if (cp == NULL) {
	    slaxIncludeAdd(dir);
	    break;
	}

	if (cp - dir > 1) {
	    if (buflen < cp - dir + 1) {
		buflen = cp - dir + 1 + BUFSIZ;
		buf = alloca(buflen);
	    }

	    memcpy(buf, dir, cp - dir);
	    buf[cp - dir] = '\0';

	    slaxDataListAddDir(where, inited, buf);
	}

	if (*cp == '\0')
	    break;
	dir = cp + 1;
    }
}

#endif /* LIBSLAX_SLAXDATA_H */
