/*
 * $Id$
 *
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer (phil@) August 2016
 *
 * Node set are ordered collections of nodes, and must be elastic to
 * allow for growth.  We make them in "chunks", which are pages of
 * node atom numbers, with a small overhead to tell us when to add the
 * next node.  A nodeset has in "info" section located in the memory
 * mapped segement that describes the node set's type and list of
 * chunks, which we wrap in a user-space data structure that keeps our
 * API sane.
 */

#ifndef LIBSLAX_XI_NODESET_H
#define LIBSLAX_XI_NODESET_H

typedef uint8_t xi_nodeset_type_t;
typedef uint8_t xi_nodeset_flags_t;
typedef pa_atom_t xi_nodeset_chunk_id_t;

/*
 * Describes a node set, as viewed from the mmap.  first/last work
 * as a tail-queue to allow easy addition of new nodes.
 */
typedef struct xi_nodeset_info_s {
    xi_nodeset_type_t xnsi_type;   /* Node set type */
    xi_nodeset_flags_t xnsi_flags; /* Flags */
    uint16_t xnsi_count;	   /* Size of nodeset */
    xi_nodeset_chunk_id_t xnsi_first; /* Start of chain of chunks */
    xi_nodeset_chunk_id_t xnsi_last; /* End of chain of chunks */
} xi_nodeset_info_t;

#define XI_NSTYPE_NORMAL 0	/* Normal node set */
#define XI_NSTYPE_RTF	1	/* Result tree fragment */
#define XI_NSTYPE_VAR	2	/* Normal variable */
#define XI_NSTYPE_MVAR	3	/* Mutable variable */

/*
 * The chunk is a page of nodes within a listed list.
 */
typedef struct xi_nodeset_chunk_s {
    xi_nodeset_chunk_id_t xnsc_next;		/* Next chunk in the chain */
    uint32_t xnsc_count;	/* Number of used nodes in this chunk */
    pa_atom_t xnsc_nodes[0];	/* Member nodes */
} xi_nodeset_chunk_t;

#define XI_NODESET_CHUNK_SHIFT	10
#define XI_NODESET_CHUNK_SIZE	(1 << XI_NODESET_CHUNK_SHIFT)

/*
 * Number of nodes in a single chunk, meaning a page of node records,
 * minus the "overhead" (xi_nodeset_chunk_t).
 */
#define XI_NODESET_NUM_PER_CHUNK \
    ((XI_NODESET_CHUNK_SIZE - sizeof(xi_nodeset_chunk_t)) / sizeof(pa_atom_t))

typedef pa_atom_t xi_nodeset_info_atom_t; /* Our atom type */

typedef struct xi_nodeset_s {
    xi_workspace_t *xns_workspace; /* Current workspace */
    xi_nodeset_info_atom_t xns_info_atom;  /* Our info block's atom number */
    xi_nodeset_info_t *xns_infop;  /* Our info block pointer */
} xi_nodeset_t;

#define xns_type xns_infop->xnsi_type
#define xns_flags xns_infop->xnsi_flags
#define xns_count xns_infop->xnsi_count
#define xns_first xns_infop->xnsi_first
#define xns_last xns_infop->xnsi_last

PA_FIXED_FUNCTIONS(xi_nodeset_chunk_id_t, xi_nodeset_chunk_t, xi_nodeset_t,
		   xns_workspace->xw_nodeset_chunks,
		   xi_nodeset_chunk_alloc, xi_nodeset_chunk_free,
		   xi_nodeset_chunk_addr);

typedef pa_atom_t xi_nodeset_info_id_t;
PA_FIXED_FUNCTIONS(xi_nodeset_info_id_t, xi_nodeset_info_t, xi_workspace_t,
		   xw_nodeset_info, xi_nodeset_info_alloc,
		   xi_nodeset_info_free, xi_nodeset_info_addr);

/*
 * Add a node to a nodeset
 */
static inline void
xi_nodeset_add (xi_nodeset_t *nodeset, pa_atom_t node)
{
    xi_nodeset_chunk_t *chunkp;
    pa_atom_t atom;

    if (nodeset->xns_first == PA_NULL_ATOM) {
	/* Empty; make the first chunk */
	chunkp = xi_nodeset_chunk_alloc(nodeset, &atom);
	if (chunkp == NULL)
	    return;

	nodeset->xns_first = nodeset->xns_last = atom;

    } else {
	/* Not empty; determing if the last chunk is full */
	chunkp = xi_nodeset_chunk_addr(nodeset, nodeset->xns_last);
	if (chunkp == NULL)
	    return;		/* Should not occur */

	if (chunkp->xnsc_count == XI_NODESET_CHUNK_SIZE) {
	    /* Full house; make a new chunk */
	    xi_nodeset_chunk_t *newp = xi_nodeset_chunk_alloc(nodeset, &atom);
	    if (newp == NULL)
		return;		/* Out of memory */

	    chunkp->xnsc_next = atom; /* Add to linked list */
	    chunkp = newp;	/* Use newp as our chunk */
	}
    }

    /* Finally, add the node to the end of the last chunk */
    chunkp->xnsc_nodes[chunkp->xnsc_count++] = node;
}

static inline xi_nodeset_t *
xi_nodeset_alloc (xi_workspace_t *xwp, xi_nodeset_type_t type,
		  xi_nodeset_flags_t flags)
{
    xi_nodeset_info_atom_t info_atom;
    xi_nodeset_info_t *infop = xi_nodeset_info_alloc(xwp, &info_atom);
    if (infop == NULL)
	return NULL;

    xi_nodeset_t *nodeset = calloc(1, sizeof(*nodeset));
    if (nodeset == NULL) {
	xi_nodeset_info_free(xwp, info_atom);
	return NULL;
    }

    /* Fill in the structures */
    nodeset->xns_workspace = xwp;
    nodeset->xns_infop = infop;
    nodeset->xns_info_atom = info_atom;
    infop->xnsi_type = type;
    infop->xnsi_flags = flags;

    return nodeset;
}

static inline void
xi_nodeset_free (xi_nodeset_t *nodeset)
{
    if (nodeset == NULL)
	return;

    xi_workspace_t *xwp = nodeset->xns_workspace;
    xi_nodeset_chunk_t *chunkp;
    xi_nodeset_chunk_id_t id, last_id;

    id = nodeset->xns_first;
    nodeset->xns_first = PA_NULL_ATOM;

    /* Free all the chunks inside this nodeset */
    for (chunkp = xi_nodeset_chunk_addr(nodeset, id); chunkp;
	 chunkp = xi_nodeset_chunk_addr(nodeset, id)) {
	last_id = id;
	id = chunkp->xnsc_next; /* Fetch before free */
	xi_nodeset_chunk_free(nodeset, last_id);
	if (id == PA_NULL_ATOM)
	    break;
    }

    /* Free the info and user-space pieces */
    xi_nodeset_info_free(xwp, nodeset->xns_info_atom);
    free(nodeset);
}

#endif /* LIBSLAX_XI_NODESET_H */
