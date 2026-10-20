/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer, October 2026
 *
 * Callback hook by which a dynamic-loading provider (libslax, via
 * slaxEnable()) gives libpin a way to resolve "prefix:local(...)" XPath
 * function calls that libpin's own compiled-evaluator built-in table
 * does not recognize, without libpin linking against libslax or
 * libxslt itself. See build/plans/slax-funcs.rst, Part 1.
 */

#ifndef LIBSLAX_PIN_DYN_H
#define LIBSLAX_PIN_DYN_H

#include "xo_filter.h"

typedef struct pin_dyn_ops_s {
    unsigned pdo_version;

    /*
     * pdo_resolve_prefix: resolve a namespace prefix with no in-scope
     * xmlns declaration (libpin's own namespace-scope lookup already
     * missed) to a URI string. Returns NULL if the prefix is unknown.
     */
    const char *(*pdo_resolve_prefix)(const char *prefix);

    /*
     * pdo_find_function: load and look up 'local' under 'uri', wrapped
     * in a calling-convention bridge, ready to call as an
     * xo_eval_ext_fn_t. Returns NULL if nothing provides local@uri.
     *
     * descp/arghelpp are optional (pass NULL if not wanted); on a
     * successful lookup they're filled with a short description and
     * an argument-list hint for the function (either may still come
     * back NULL, if the provider has none on file for it), for use in
     * a diagnostic "consider" hint. Untouched when the lookup fails.
     */
    xo_eval_ext_fn_t (*pdo_find_function)(const char *local,
                                           const char *uri,
                                           int *nargsp, int *min_argsp,
                                           const char **descp,
                                           const char **arghelpp);
} pin_dyn_ops_t;

#define PIN_DYN_OPS_VERSION 2

/*
 * Register the dynamic-loading provider's callbacks. Call once, at
 * application startup (e.g. from slaxEnable()). 'ops' is retained by
 * pointer, not copied; the caller must keep it valid (e.g. static
 * storage) for the life of the process. Passing NULL clears any
 * previously registered provider, making the xfdo_find_function hook
 * always return NULL for any name not in the built-in evaluator table.
 */
void
pin_set_dyn_ops (const pin_dyn_ops_t *ops);

#endif /* LIBSLAX_PIN_DYN_H */
