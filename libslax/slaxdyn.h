/*
 * $Id$
 *
 * Copyright (c) 2006-2010, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#ifndef LIBSLAX_SLAXDYN_H
#define LIBSLAX_SLAXDYN_H

#include <libxslt/xsltutils.h>	/* For xsltHandleDebuggerCallback, etc */
#include <libxslt/extensions.h>
#include <libslax/slax.h>

/*
 * This structure defines the mapping between function (or element)
 * names and the functions that implement them.  It is also used by
 * the table-based function registration functions.
 */
typedef struct slax_function_table_s {
    const char *ft_name;	/* Name of the function */
    xmlXPathFunction ft_func;	/* Function pointer */
} slax_function_table_t;

typedef struct slax_element_table_s {
    const char *et_name;	/* Name of the element */
    xsltPreComputeFunction et_fcompile;
    xsltTransformFunction et_felement;
} slax_element_table_t;

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
    unsigned version UNUSED, \
	struct slax_dyn_arg_s *arg UNUSED

#define SLAX_DYN_INIT_NAME	"slaxDynLibInit"
#define SLAX_DYN_CLEAN_NAME	"slaxDynLibClean"

#define SLAX_DYN_FUNC(_n) \
    unsigned _n (SLAX_DYN_ARGS); unsigned _n (SLAX_DYN_ARGS)

typedef unsigned (*slax_dyn_init_func_t)(SLAX_DYN_ARGS);

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

