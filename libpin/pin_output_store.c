/*
 * Copyright (c) 2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer, November 2026
 *
 * pin_output_store -- the store sink.  Each event becomes a node,
 * appended at the end of the innermost open element, the same way
 * input is inserted.  The writer (pin_output_writer_t) guarantees
 * that attributes come before an element's other children.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slaxconfig.h"
#include <libpsu/psucommon.h>
#include <libpsu/psulog.h>
#include <parrotdb/pacommon.h>
#include <parrotdb/paconfig.h>
#include <parrotdb/pammap.h>
#include <parrotdb/pafixed.h>
#include <parrotdb/paarb.h>
#include <parrotdb/paistr.h>
#include <parrotdb/papat.h>
#include <libpin/pin_common.h>
#include <libpin/pin_rules.h>
#include <libpin/pin_tree.h>
#include <libpin/pin_workspace.h>
#include <libpin/pin_output_store.h>

/* One open element: its node and its last child so far */
typedef struct pin_output_store_frame_s {
    pin_node_id_t psf_atom;	/* The element (or the root) */
    pin_node_id_t psf_last;	/* Its last child, or null */
} pin_output_store_frame_t;

typedef struct pin_output_store_s {
    pin_workspace_t *pst_workspace; /* Where the nodes go */
    pin_output_store_frame_t *pst_stack; /* Open elements; [0] is the root */
    unsigned pst_depth;		/* Frames in use */
    unsigned pst_size;		/* Frames allocated */
} pin_output_store_t;

pin_node_id_t
pin_output_store_root (pin_workspace_t *pwp)
{
    pin_node_id_t atom;
    pin_node_t *nodep = pin_node_alloc(pwp, &atom);
    if (nodep == NULL)
	return pin_node_id_null_atom();

    nodep->pn_type = PIN_TYPE_ROOT;
    nodep->pn_flags = 0;
    nodep->pn_depth = 0;
    nodep->pn_ns_map = PA_NULL_ATOM;
    nodep->pn_name = pin_name_id_null_atom();
    nodep->pn_parent = pin_node_id_null_atom();
    nodep->pn_next = pin_node_id_null_atom();
    nodep->pn_contents = PA_NULL_ATOM;

    return atom;
}

void
pin_output_store_free_root (pin_workspace_t *pwp, pin_node_id_t root)
{
    pin_tree_free_subtree(pwp, root);
}

static int
pin_output_store_push (pin_output_store_t *pstp, pin_node_id_t atom,
		       pin_node_id_t last)
{
    if (pstp->pst_depth + 1 > pstp->pst_size) {
	unsigned newsize = pstp->pst_size ? pstp->pst_size * 2 : 16;
	pin_output_store_frame_t *np = realloc(pstp->pst_stack,
					       newsize * sizeof(*np));
	if (np == NULL)
	    return -1;

	pstp->pst_stack = np;
	pstp->pst_size = newsize;
    }

    pin_output_store_frame_t *frp = &pstp->pst_stack[pstp->pst_depth];
    frp->psf_atom = atom;
    frp->psf_last = last;
    pstp->pst_depth += 1;

    return 0;
}

/*
 * Add a node as the last child of the innermost open element.
 * "contents" is its text (a textpool atom) or null.
 */
static pin_node_t *
pin_output_store_add (pin_output_store_t *pstp, pin_node_type_t type,
		      pin_name_id_t name_id, pa_atom_t contents,
		      pin_node_id_t *atomp)
{
    pin_workspace_t *pwp = pstp->pst_workspace;
    pin_output_store_frame_t *frp = &pstp->pst_stack[pstp->pst_depth - 1];
    pin_node_t *parentp = pin_node_addr(pwp, frp->psf_atom);
    if (parentp == NULL)
	return NULL;

    pin_node_t *nodep = pin_node_alloc(pwp, atomp);
    if (nodep == NULL)
	return NULL;

    nodep->pn_type = type;
    nodep->pn_flags = 0;
    nodep->pn_depth = parentp->pn_depth + 1;
    nodep->pn_ns_map = PA_NULL_ATOM;
    nodep->pn_name = name_id;
    nodep->pn_contents = contents;

    pin_tree_append_child(pwp, frp->psf_atom, frp->psf_last, *atomp);
    frp->psf_last = *atomp;

    return nodep;
}

/* Copy a string into the textpool; returns the null atom on failure */
static pa_atom_t
pin_output_store_text_atom (pin_workspace_t *pwp, const char *data,
			    size_t len)
{
    pa_arb_t *prp = pwp->pw_textpool;
    pa_arb_atom_t atom = pa_arb_alloc(prp, len + 1);
    char *cp = pa_arb_atom_addr(prp, atom);
    if (cp == NULL)
	return PA_NULL_ATOM;

    memcpy(cp, data, len);
    cp[len] = '\0';

    return pa_arb_atom_of(atom);
}

static int
pin_output_store_open (PIN_OUTPUT_OPEN_ARGS)
{
    pin_output_store_t *pstp = opaque;
    pin_workspace_t *pwp = pstp->pst_workspace;
    pin_name_id_t name_id = pin_namepool_atom(pwp, name, TRUE);
    pin_node_id_t atom;

    pin_node_t *nodep = pin_output_store_add(pstp, PIN_TYPE_ELT, name_id,
					     PA_NULL_ATOM, &atom);
    if (nodep == NULL)
	return -1;

    if (uri)
	pin_node_set_ns_map(nodep, pin_ns_find(pwp, prefix, uri, TRUE));

    return pin_output_store_push(pstp, atom, pin_node_id_null_atom());
}

static int
pin_output_store_attrib (PIN_OUTPUT_ATTRIB_ARGS)
{
    pin_output_store_t *pstp = opaque;
    pin_workspace_t *pwp = pstp->pst_workspace;
    pin_name_id_t name_id = pin_namepool_atom(pwp, name, TRUE);
    pa_atom_t text = pin_output_store_text_atom(pwp, value, strlen(value));
    pin_node_id_t atom;

    pin_node_t *nodep = pin_output_store_add(pstp, PIN_TYPE_ATTRIB, name_id,
					     text, &atom);
    if (nodep == NULL)
	return -1;

    if (uri)
	pin_node_set_ns_map(nodep, pin_ns_find(pwp, prefix, uri, TRUE));

    pin_output_store_frame_t *frp = &pstp->pst_stack[pstp->pst_depth - 1];
    pin_node_t *eltp = pin_node_addr(pwp, frp->psf_atom);
    if (eltp)
	eltp->pn_flags |= PNF_ATTRIBS_PRESENT;

    return 0;
}

static int
pin_output_store_attrib_raw (PIN_OUTPUT_ATTRIB_RAW_ARGS)
{
    pin_output_store_t *pstp = opaque;
    pin_workspace_t *pwp = pstp->pst_workspace;
    pa_atom_t data = pin_output_store_text_atom(pwp, text, strlen(text));
    pin_node_id_t atom;

    if (pin_output_store_add(pstp, PIN_TYPE_ATSTR, pin_name_id_null_atom(),
			     data, &atom) == NULL)
	return -1;

    return 0;
}

static int
pin_output_store_text (PIN_OUTPUT_TEXT_ARGS)
{
    pin_output_store_t *pstp = opaque;
    pin_node_type_t type = (flags & PIN_OUTPUT_TEXT_UNESC)
	? PIN_TYPE_UNESC : PIN_TYPE_TEXT;
    pa_atom_t text = pin_output_store_text_atom(pstp->pst_workspace,
						data, len);
    pin_node_id_t atom;

    if (pin_output_store_add(pstp, type, pin_name_id_null_atom(),
			     text, &atom) == NULL)
	return -1;

    return 0;
}

static int
pin_output_store_comment (PIN_OUTPUT_COMMENT_ARGS)
{
    pin_output_store_t *pstp = opaque;
    pa_atom_t text = pin_output_store_text_atom(pstp->pst_workspace,
						data, strlen(data));
    pin_node_id_t atom;

    if (pin_output_store_add(pstp, PIN_TYPE_COMMENT, pin_name_id_null_atom(),
			     text, &atom) == NULL)
	return -1;

    return 0;
}

/* A PI is kept in the store's "target\ncontent" form */
static int
pin_output_store_pi (PIN_OUTPUT_PI_ARGS)
{
    pin_output_store_t *pstp = opaque;
    size_t tlen = strlen(target);
    size_t dlen = data ? strlen(data) : 0;
    char *buf = malloc(tlen + 1 + dlen + 1);
    if (buf == NULL)
	return -1;

    memcpy(buf, target, tlen);
    buf[tlen] = '\n';
    if (dlen)
	memcpy(buf + tlen + 1, data, dlen);
    buf[tlen + 1 + dlen] = '\0';

    pa_atom_t text = pin_output_store_text_atom(pstp->pst_workspace,
						buf, tlen + 1 + dlen);
    free(buf);

    pin_node_id_t atom;
    if (pin_output_store_add(pstp, PIN_TYPE_PI, pin_name_id_null_atom(),
			     text, &atom) == NULL)
	return -1;

    return 0;
}

static int
pin_output_store_close (PIN_OUTPUT_CLOSE_ARGS)
{
    pin_output_store_t *pstp = opaque;

    /* The root (frame 0) stays open */
    if (pstp->pst_depth > 1)
	pstp->pst_depth -= 1;

    return 0;
}

static void
pin_output_store_free (PIN_OUTPUT_FREE_ARGS)
{
    pin_output_store_t *pstp = opaque;

    if (pstp == NULL)
	return;

    free(pstp->pst_stack);
    free(pstp);
}

static const pin_output_funcs_t pin_output_store_funcs = {
    .pof_version = PIN_OUTPUT_FUNCS_VERSION,
    .pof_name = "store",
    .pof_open = pin_output_store_open,
    .pof_attrib = pin_output_store_attrib,
    .pof_attrib_raw = pin_output_store_attrib_raw,
    .pof_text = pin_output_store_text,
    .pof_comment = pin_output_store_comment,
    .pof_pi = pin_output_store_pi,
    .pof_close = pin_output_store_close,
    .pof_free = pin_output_store_free,
};

pin_node_id_t
pin_output_store_root_of (const pin_output_t *sinkp)
{
    const pin_output_store_t *pstp = sinkp->po_opaque;

    return pstp->pst_stack[0].psf_atom;
}

int
pin_output_store_init (pin_output_t *sinkp, pin_workspace_t *pwp,
		       pin_node_id_t root)
{
    pin_node_t *rootp = pin_node_addr(pwp, root);
    if (rootp == NULL)
	return -1;

    pin_output_store_t *pstp = calloc(1, sizeof(*pstp));
    if (pstp == NULL)
	return -1;

    pstp->pst_workspace = pwp;

    /* Find the root's last child, so new nodes follow the old ones */
    pin_node_id_t last = pin_node_id_null_atom();
    for (pin_node_id_t cid = pin_node_child(rootp);
	     !pin_node_id_is_null(cid); ) {
	pin_node_t *childp = pin_node_addr(pwp, cid);
	if (childp == NULL || childp->pn_depth <= rootp->pn_depth)
	    break;

	last = cid;
	cid = childp->pn_next;
    }

    if (pin_output_store_push(pstp, root, last) < 0) {
	free(pstp);
	return -1;
    }

    sinkp->po_funcs = &pin_output_store_funcs;
    sinkp->po_opaque = pstp;

    return 0;
}
