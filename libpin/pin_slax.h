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
 * Selection-only compiler: SLAX/XSLT xmlDoc -> pin filter + rulebook.
 *
 * Walks every <xsl:template match="..."> in the document, allocates a
 * pin_rule_t for each one (tagged with pr_mode), and calls
 * pin_filter_add_with_action so the streaming parser can dispatch
 * directly to the rule when the pattern matches.  The parser consults
 * the execution context (pin_context_t) for the current mode and only
 * fires rules whose pr_mode matches.  Template bodies, named templates,
 * variables, and apply-templates are not handled here (see
 * missing-in-pin.md items 14+).
 */

#ifndef LIBSLAX_PIN_SLAX_H
#define LIBSLAX_PIN_SLAX_H

#include <libxml/tree.h>
#include <libpin/pin_common.h>
#include <libpin/pin_rules.h>
#include <libpin/pin_filter.h>
#include <libpin/pin_workspace.h>

/*
 * Compile ALL match-pattern templates from a SLAX/XSLT xmlDoc into a
 * single filter+rulebook.  Every template, regardless of its mode=
 * attribute, is compiled.  Each rule's pr_mode field is set to the
 * namepool atom of its mode string (PA_NULL_ATOM for default-mode
 * templates).  The parser's execution context (pin_parse_set_mode)
 * determines which rules fire at runtime.
 *
 * Templates with name= but no match= are always skipped.
 *
 * Returns the total number of patterns compiled (>= 0) or -1 on error.
 */
int
pin_slax_compile (xmlDocPtr docp, xo_filter_t *xfp, pin_rulebook_t *rb,
		  pin_action_type_t action);

/*
 * Callback type for pin_slax_for_each_mode.
 * 'mode'  is NULL for the default (no-mode) template group.
 * 'count' is the number of match-pattern templates with that mode.
 */
typedef void (*pin_slax_mode_fn)(void *opaque, const char *mode, int count);

/*
 * Enumerate distinct mode names in a SLAX/XSLT document.
 * Calls fn(opaque, mode, count) once per distinct mode found among
 * match-pattern templates (mode==NULL for the default mode).
 * Order is first-occurrence order.
 * Returns the number of distinct modes (>= 0) or -1 on error.
 * If fn is NULL, just counts modes without invoking any callback.
 */
int
pin_slax_for_each_mode (xmlDocPtr docp, pin_slax_mode_fn fn, void *opaque);

#endif /* LIBSLAX_PIN_SLAX_H */
