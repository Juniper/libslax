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
#include <libxi/xicommon.h>
#include <libxi/xirules.h>
#include <libxi/xitree.h>
#include <libxi/xiworkspace.h>

int xi_dead_code;

/*
 * Append new_node_atom as the last child of parent_atom.
 *
 * If last_hint is non-null and its xn_next still points to parent_atom
 * (i.e. it is still the last child), use it directly — O(1).  Otherwise
 * scan the sibling chain from xn_contents to find the last child — O(n).
 *
 * The caller must have already set all fields of the new node except
 * xn_next; this function wires xn_next and updates xn_contents if needed.
 */
pa_atom_t
xi_tree_append_child (xi_workspace_t *xwp,
		      pa_atom_t parent_atom, pa_atom_t last_hint,
		      pa_atom_t new_node_atom)
{
    xi_node_t *parentp = xi_node_addr(xwp, parent_atom);
    xi_node_t *newp    = xi_node_addr(xwp, new_node_atom);
    if (parentp == NULL || newp == NULL)
	return PA_NULL_ATOM;

    /* New node is always the last child: its xn_next points up to parent */
    newp->xn_next = parent_atom;

    if (parentp->xn_contents == PA_NULL_ATOM) {
	/* No children yet — new node is the first and last */
	parentp->xn_contents = new_node_atom;
	return new_node_atom;
    }

    /* Locate the current last child */
    pa_atom_t last_atom = PA_NULL_ATOM;
    xi_node_t *lastp = NULL;

    if (last_hint != PA_NULL_ATOM) {
	xi_node_t *hintp = xi_node_addr(xwp, last_hint);
	/* A last child's xn_next is the parent atom — O(1) validation */
	if (hintp != NULL && hintp->xn_next == parent_atom) {
	    lastp = hintp;
	    last_atom = last_hint;
	}
    }

    if (lastp == NULL) {
	/* Scan: walk xn_next until we reach the entry pointing to parent */
	last_atom = parentp->xn_contents;
	for (;;) {
	    xi_node_t *childp = xi_node_addr(xwp, last_atom);
	    if (childp == NULL || childp->xn_next == parent_atom)
		break;
	    last_atom = childp->xn_next;
	}
	lastp = xi_node_addr(xwp, last_atom);
    }

    if (lastp)
	lastp->xn_next = new_node_atom;

    return new_node_atom;
}
