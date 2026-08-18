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
 * pin_rule_t for each one, and calls pin_filter_add_with_action so the
 * streaming parser can dispatch directly to the rule when the pattern
 * matches.  Template bodies, modes, named templates, variables, and
 * apply-templates are not handled here (see missing-in-pin.md items 12+).
 */

#ifndef LIBSLAX_PIN_SLAX_H
#define LIBSLAX_PIN_SLAX_H

#include <libxml/tree.h>
#include <libpin/pin_common.h>
#include <libpin/pin_rules.h>
#include <libpin/pin_filter.h>

/*
 * Compile match-pattern templates from a SLAX/XSLT xmlDoc into a filter.
 *
 * For each <xsl:template match="pattern"> found as a direct child of the
 * document root, a pin_rule_t with the given 'action' is allocated in 'rb'
 * and registered in 'xfp' via pin_filter_add_with_action.  Templates that
 * carry a name= attribute but no match= attribute are skipped.
 *
 * Returns the number of patterns compiled (>= 0) or -1 on error.
 */
int
pin_slax_compile (xmlDocPtr docp, xo_filter_t *xfp, pin_rulebook_t *rb,
		  pin_action_type_t action);

#endif /* LIBSLAX_PIN_SLAX_H */
