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

#include "slaxconfig.h"
#include <libpsu/psulog.h>
#include <parrotdb/pacommon.h>
#include <parrotdb/paconfig.h>
#include <parrotdb/pammap.h>
#include <parrotdb/pafixed.h>
#include <parrotdb/paarb.h>
#include <parrotdb/paistr.h>
#include <parrotdb/papat.h>
#include <parrotdb/pabitmap.h>
#include <libpin/pin_common.h>
#include <libpin/pin_rules.h>
#include <libpin/pin_tree.h>
#include <libpin/pin_workspace.h>

int pin_dead_code;

/*
 * Append new_node_atom as the last child of parent_atom.
 *
 * If last_hint is non-null and its pn_next still points to parent_atom
 * (i.e. it is still the last child), use it directly -- O(1).  Otherwise
 * scan the sibling chain from pn_contents to find the last child -- O(n).
 *
 * The caller must have already set all fields of the new node except
 * pn_next; this function wires pn_next and updates pn_contents if needed.
 */
pin_node_id_t
pin_tree_append_child (pin_workspace_t *pwp,
		      pin_node_id_t parent_atom, pin_node_id_t last_hint,
		      pin_node_id_t new_node_atom)
{
    pin_node_t *parentp = pin_node_addr(pwp, parent_atom);
    pin_node_t *newp    = pin_node_addr(pwp, new_node_atom);
    if (parentp == NULL || newp == NULL)
	return pin_node_id_null_atom();

    /* New node is always the last child: its pn_next points up to parent */
    newp->pn_next = parent_atom;
    newp->pn_parent = parent_atom;

    if (parentp->pn_contents == PA_NULL_ATOM) {
	/* No children yet -- new node is the first and last */
	pin_node_set_child(parentp, new_node_atom);
	return new_node_atom;
    }

    /* Locate the current last child */
    pin_node_id_t last_atom = pin_node_id_null_atom();
    pin_node_t *lastp = NULL;

    if (!pin_node_id_is_null(last_hint)) {
	pin_node_t *hintp = pin_node_addr(pwp, last_hint);
	/* A last child's pn_next is the parent atom -- O(1) validation */
	if (hintp != NULL && pin_node_id_equal(hintp->pn_next, parent_atom)) {
	    lastp = hintp;
	    last_atom = last_hint;
	}
    }

    if (lastp == NULL) {
	/* Scan: walk pn_next until we reach the entry pointing to parent */
	last_atom = pin_node_child(parentp);
	for (;;) {
	    pin_node_t *childp = pin_node_addr(pwp, last_atom);
	    if (childp == NULL || pin_node_id_equal(childp->pn_next, parent_atom))
		break;
	    last_atom = childp->pn_next;
	}
	lastp = pin_node_addr(pwp, last_atom);
    }

    if (lastp)
	lastp->pn_next = new_node_atom;

    return new_node_atom;
}

/*
 * Free the subtree at top_atom, with the text its nodes hold, in one
 * pass that needs no stack: free a leaf, and when it was its parent's
 * last child, the parent becomes a leaf.  The caller has already
 * unlinked top_atom from its parent, if it has one.  Returns the
 * number of nodes freed.
 */
uint32_t
pin_tree_free_subtree (pin_workspace_t *pwp, pin_node_id_t top_atom)
{
    pin_node_id_t cur = top_atom;
    uint32_t count = 0;

    for (;;) {
	pin_node_t *nodep = pin_node_addr(pwp, cur);
	if (nodep == NULL)
	    return count;

	pin_node_id_t child = pin_node_child(nodep);
	if (!pin_node_id_is_null(child)) {
	    cur = child;
	    continue;
	}

	pin_node_id_t next = nodep->pn_next;
	pin_node_id_t parent = nodep->pn_parent;

	if (nodep->pn_type == PIN_TYPE_COMMENT || nodep->pn_type == PIN_TYPE_PI)
	    pa_arb_free_atom(pwp->pw_textpool, pa_arb_atom(nodep->pn_contents));
	else
	    pa_arb_free_atom(pwp->pw_textpool, pin_node_text(nodep));
	pin_node_free(pwp, cur);
	count += 1;

	if (pin_node_id_equal(cur, top_atom))
	    return count;

	if (pin_node_id_equal(next, parent)) {
	    /* That was the last child, so the parent is now a leaf */
	    pin_node_t *parentp = pin_node_addr(pwp, parent);
	    if (parentp == NULL)
		return count;

	    pin_node_set_child(parentp, pin_node_id_null_atom());
	    cur = parent;
	} else {
	    cur = next;
	}
    }
}
