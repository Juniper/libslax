/*
 * $Id$
 *
 * Copyright (c) 2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#ifndef LIBSLAX_SLAXDATADIR_H
#define LIBSLAX_SLAXDATADIR_H

typedef struct slax_data_dirlist_s {
    int sdd_inited;		/**< List has been initialized */
    slax_data_list_t sdd_list;	/**< Directory list */
} slax_data_dirlist_t;

/*
 * Add a directory to the list of directories searched for files
 */
static inline void
slaxDataDirAdd (slax_data_dirlist_t *sddp, const char *dir)
{
    if (!sddp->sdd_inited) {
	sddp->sdd_inited = TRUE;
	slaxDataListInit(&sddp->sdd_list);
    }

    slaxDataListAddNul(&sddp->sdd_list, dir);
}

/*
 * Add a set of directories to the list of directories searched for files
 */
static inline void
slaxDataDirAddPath (slax_data_dirlist_t *sddp, const char *dir)
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

	    slaxDataDirAdd(sddp, buf);
	}

	if (*cp == '\0')
	    break;
	dir = cp + 1;
    }
}

static inline FILE *
slaxDataDirFind (slax_data_dirlist_t *sddp, const char **extensions,
		 const char *scriptname, char *full_name, int full_size)
{
    char const **cpp;
    FILE *fp;
    slax_data_node_t *dnp;

    if (!sddp->sdd_inited)
	return NULL;

    SLAXDATALIST_FOREACH(dnp, &sddp->sdd_list) {
	char *dir = dnp->dn_data;

	for (cpp = extensions; *cpp; cpp++) {
	    snprintf(full_name, full_size, "%s/%s%s%s",
		     dir, scriptname, **cpp ? "." : "", *cpp);
	    fp = fopen(full_name, "r+");
	    if (fp)
		return fp;
	}
    }

    return NULL;
}

static inline void
slaxDataDirLog (slax_data_dirlist_t *sddp, const char *prefix)
{
    slax_data_node_t *dnp;

    SLAXDATALIST_FOREACH(dnp, &sddp->sdd_list) {
	slaxLog("%s%s", prefix, dnp->dn_data);
    }
}

static inline void
slaxDataDirClean (slax_data_dirlist_t *sddp)
{
    if (sddp->sdd_inited)
	slaxDataListClean(&sddp->sdd_list);
}

#endif /* LIBSLAX_SLAXDATADIR_H */
