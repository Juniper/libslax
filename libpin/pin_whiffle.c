/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer <phil@>, September 2016
 */

typedef pin_node_type_t (*pin_whiffle_source_next_token_func_t)
	(void *opaque_state, char **datap, char **restp);

typedef pin_node_type_t (*pin_whiffle_dest_next_token_func_t)
	(void *opaque_state, pin_node_type_t type);

typedef struct pin_whiffle_s {
    void *xwf_source_state;
    pin_whiffle_source_next_token_func_t xwf_source_func;
    void *xwf_dest_state;
    pin_whiffle_dest_next_token_func_t xwf_dest_func;
} pin_whiffle_t;

static inline void
pin_whiffle_set_source (pin_whiffle_t *xwfp,
		       pin_whiffle_source_next_token_func_t func, void *data)
{
    xwfp->xwf_source_func = func;
    xwfp->xwf_source_state = data;
}

static inline void
pin_whiffle_set_dest (pin_whiffle_t *xwfp,
		     pin_whiffle_dest_next_token_func_t func, void *data)
{
    xwfp->xwf_dest_func = func;
    xwfp->xwf_dest_state = data;
}

static void
pin_whiffle_process_token (pin_whiffle_t *xwfp, pin_node_type_t type)
{
    ;
}

static inline pin_node_type_t
pin_whiffle_next_token (pin_whiffle_t *xwfp)
{
    return xwfp->xwf_source_func(xwfp->xwf_source_state, xwfp);
}

static void
pin_whiffle_process (pin_whiffle_t *xwfp)
{
    pin_node_type_t type;

    for (type = pin_whiffle_next_token(xwfp); type != XI_TYPE_EOF;
	 type = pin_whiffle_next_token(xwfp)) {
	pin_whiffle_process_token(xwfp, type);
    }
}

static inline pin_boolean_t
pin_whiffle_ignore_child (pin_node_t *nodep)
{
    switch (nodep->xn_type) {
    case XI_TYPE_ROOT:
    case XI_TYPE_ELT:
	return FALSE;
    }

    return TRUE;
}

typedef struct pin_whiffle_parse_as_source_s {
    pa_atom_t xwps_first_atom;	/* First atom returned */
    pa_atom_t xwps_last_atom;	/* Last atom returned */
    pin_depth_t xwps_last_depth;	/* Depth of last node */
    uint8_t xwps_direction;	/* Direction to proceed */
    pin_node_t *xwps_last_nodep;	/* Last results */
} pin_whiffle_parse_as_source_t;

/* Values for xwps_direction */
#define XI_DIR_INIT	0	/* Fresh source */
#define XI_DIR_SELF	1	/* Return self */
#define XI_DIR_CHILD	2	/* Return child atom */
#define XI_DIR_NEXT	3	/* Return next atom */
#define XI_DIR_EOF	4	/* Return EOF */

static pin_node_type_t
pin_whiffle_parse_as_source (void *opaque, pin_whiffle_t *xwfp,
			    pin_workspace_t *xwp)
{
    pin_whiffle_parse_as_source_t *xwpsp = opaque;

    /* If we're at EOF, then we're done (and boring) */
    if (xwpsp->xwps_direction = XI_TYPE_EOF) 
	return XI_TYPE_EOF;

    pin_node_type_t type = XI_TYPE_EOF;
    uint8_t new_dir = XI_DIR_CHILD;
    pa_atom_t atom = xwpsp->xwps_last_atom, new_atom = PA_NULL_ATOM;
    pin_depth_t new_depth = xwpsp->xwps_last_depth;
    pin_node_t *nodep = xwpsp->xwps_last_nodep;
    pin_node_t *new_nodep = NULL;

    if (nodep == NULL) {
	/* Should not occur, but if it does ... */
	new_dir = XI_DIR_EOF;
	return XI_DIR_EOF;
    }

    /* The direction tells us our state and where we are heading next */
    switch (xwpsp->xwps_direction) {
    case XI_DIR_INIT:
    case XI_DIR_SELF:
	new_atom = xwpsp->xwps_first_atom;
	break;

    case XI_DIR_CHILD:
	if (nodep->xn_contents != PA_NULL_ATOM) {
	    new_atom = nodep->xn_contents;
	    new_dir = XI_DIR_CHILD;
	    type = XI_TYPE_OPEN;
	    break;
	}
	/* fallthru */

    case XI_DIR_NEXT:
	if (nodep->xn_next == PA_NULL_ATOM
	    || nodep->xn_next == xwpsp->xwps_last_atom) {
	    new_atom = PA_NULL_ATOM;
	    break;
	}

	new_atom = nodep->xn_next;
	break;
    }

    new_nodep = pin_node_addr(xwp, new_atom);
    if (new_nodep == NULL)
	new_depth = 0;
    else {
	if (new_dir == XI_DIR_CHILD && pin_whiffle_ignore_child(new_nodep))
	    new_dir = XI_DIR_NEXT;

	new_depth = new_nodep->xn_depth;
	if (new_depth < xwpsp->xwps_last_depth) {
	    /*
	     * We're done with our parent's children; send a close event and
	     * move to the next item;
	     */
	    new_dir = XI_DIR_NEXT;
	    type = XI_TYPE_CLOSE;
	} else {
	    type = new_nodep->xn_type;
	}
    }

    slaxLog("parse-as-source: last: %d/%d/%p(%d) -> new: %d/%d/%p(%d) -> %d",
	    xwpsp->xwps_direction, atom, nodep, xwpsp->xwps_last_depth,
	    new_dir, new_atom, new_nodep, new_depth,
	    type);

    /* Record results */
    xwpsp->xwps_direction = new_dir;
    xwpsp->xwps_last_atom = new_atom;
    xwpsp->xwps_last_nodep = new_nodep;
    xwpsp->xwps_last_depth = new_depth;
    return type;
}

typedef struct pin_whiffle_parse_as_dest_s {
} pin_whiffle_parse_as_dest_t;

void
pin_whiffle_test (pin_parse_t *parsep)
{
    pin_whiffle_t xwf;
    pin_whiffle_parse_as_source_t source;
    pin_whiffle_parse_as_dest_t dest;

    bzero(&xwf, sizeof(xwf));
    bzero(&source, sizeof(source));
    bzero(&dest, sizeof(dest));

    pin_whiffle_set_source(&xwf, pin_whiffle_parse_as_source, &source);
    pin_whiffle_set_dest(&xwf, pin_whiffle_parse_as_dest, &dest);

    pin_whiffle_process(&xwf);
}
