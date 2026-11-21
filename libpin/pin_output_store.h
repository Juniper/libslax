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
 * pin_output_store -- the store sink: builds the output events it is
 * sent as nodes in the store, under a fragment root.  A fragment is
 * content built for a value (a variable, parameter or mvar), not for
 * the result.  Its root is a PIN_TYPE_ROOT node with no parent, so
 * it is never reachable from the input tree.
 */

#ifndef LIBSLAX_PIN_OUTPUT_STORE_H
#define LIBSLAX_PIN_OUTPUT_STORE_H

#include <libpin/pin_common.h>
#include <libpin/pin_node.h>
#include <libpin/pin_output.h>

/* Make an empty fragment root; returns the null atom on failure */
pin_node_id_t
pin_output_store_root (pin_workspace_t *pwp);

/*
 * Set up a sink that appends to the fragment at "root", after the
 * children it already has.  Freeing the sink leaves the fragment.
 */
int
pin_output_store_init (pin_output_t *sinkp, pin_workspace_t *pwp,
		       pin_node_id_t root);

/* The root of the fragment a store sink appends to */
pin_node_id_t
pin_output_store_root_of (const pin_output_t *sinkp);

/* Free a fragment: the root and everything under it */
void
pin_output_store_free_root (pin_workspace_t *pwp, pin_node_id_t root);

#endif /* LIBSLAX_PIN_OUTPUT_STORE_H */
