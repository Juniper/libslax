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
typedef struct slax_dyn_arg_s {
    unsigned da_version;	/* Version of the caller */
    void *da_handle;		/* Handle from dlopen() */
    void *da_custom;		/* Memory hook for the extension */
    char *da_uri;		/* URI */
    slax_function_table_t *da_functions; /* Table of functions */
    slax_element_table_t *da_elements;	 /* Table of elements */
} slax_dyn_arg_t;

#define SLAX_DYN_VERSION 1	/* Current version */
#define SLAX_DYN_ARGS  \
    unsigned version __unused, struct slax_dyn_arg_s *arg __unused
#define SLAX_DYN_INIT_NAME	"slaxDynLibInit"
#define SLAX_DYN_CLEAN_NAME	"slaxDynLibClean"

#define SLAX_DYN_FUNC(_n) \
    unsigned _n (SLAX_DYN_ARGS); unsigned _n (SLAX_DYN_ARGS)

typedef unsigned (*slaxDynInitFunc_t)(SLAX_DYN_ARGS);

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

