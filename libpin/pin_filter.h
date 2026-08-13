/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer, August 2026
 *
 * libpin data backend for xo_filter's trie/FSM.
 *
 * Wraps xo_filter_create_with_data to supply a pin_workspace-backed
 * data context: element names in the compiled trie are stored as paistr atoms,
 * so the hot-path name comparison is an integer equality check rather
 * than a string compare.
 */

#ifndef LIBSLAX_PIN_FILTER_H
#define LIBSLAX_PIN_FILTER_H

#include <libxo/xo.h>
#include <libpin/pin_workspace.h>
#include <libpin/pin_rules.h>

struct xo_filter_s;
typedef struct xo_filter_s xo_filter_t;

/*
 * Create an xo_filter backed by a pin workspace.
 *
 * The caller retains ownership of pwp and xop.  The returned filter
 * and its data context are freed when the filter is destroyed via the normal
 * xo_filter destroy path.
 *
 * XPath expressions are added with xo_filter_add_one(); the filter is
 * driven by calling the outer xo_filter_ops_t functions (open_container,
 * close_container, key, get_status, …) exactly as for any other filter.
 */
xo_filter_t *
pin_filter_create (xo_handle_t *xop, pin_workspace_t *pwp);

/*
 * Add an XPath match expression to a filter returned by pin_filter_create.
 * May be called multiple times to accumulate patterns.  Returns 0 on
 * success, -1 if the expression cannot be parsed or compiled.
 */
int
pin_filter_add (xo_filter_t *xfp, const char *xpath);

/*
 * Like pin_filter_add but records 'rid' in the terminal trie node so that
 * when the filter reports XO_STATUS_FULL, the matching rule can be retrieved
 * directly via xo_filter_walk_get_action without a secondary rulebook lookup.
 */
int
pin_filter_add_with_action (xo_filter_t *xfp, const char *xpath,
			     pin_rule_id_t rid);

/*
 * Set the raw XML attribute string for the element about to be walked open.
 * Must be called immediately before pin_filter_walk_open (or
 * xo_filter_walk_open) so that xfdo_value_of can resolve attribute values
 * for predicate evaluation.  'attribs' may be NULL when there are none.
 */
void
pin_filter_set_attribs (xo_filter_t *xfp, const char *attribs);

#endif /* LIBSLAX_PIN_FILTER_H */
