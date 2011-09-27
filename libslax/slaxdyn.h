/*
 * $Id$
 *
 * Copyright (c) 2006-2010, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 */

#ifndef LIBSLAX_SLAXDYN_H
#define LIBSLAX_SLAXDYN_H

/*
 * This structure is an interface between the libslax main code
 * and the code in the extension library.  This structure should
 * only be extended by additions to the end.
 */
struct slaxDynInitArg {
    int da_version;
};

#define SLAX_DYN_VERSION 1	/* Current version */
#define SLAX_DYN_INIT_ARGS  \
    int version __unused, struct slaxDynInitArg *arg __unused
#define SLAX_DYN_INIT_NAME	"slaxDynLibInit"

#define SLAX_DYN_INIT_FUNC(_n) \
    int _n (SLAX_DYN_INIT_ARGS); int _n (SLAX_DYN_INIT_ARGS)

typedef int (*slaxDynInitFunc_t)(SLAX_DYN_INIT_ARGS);

void
slaxDynAdd (const char *dir);

void
slaxDynLoad (xmlDocPtr docp);

/*
 * Initialize the entire dynamic extension loading mechanism.
 */
void
slaxDynInit (void);

/*
 * De-initialize the entire dynamic extension loading mechanism.
 */
void
slaxDynClean (void);

#endif /* LIBSLAX_SLAXDYN_H */

