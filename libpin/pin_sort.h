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
 * pin_stree_t — parrotdb-backed sort/index tree for xsl:for-each, xsl:sort,
 * and future slax:build-index support.
 *
 * One pin_stree_t holds a patricia tree of elements ordered by their sort keys.
 * The tree is backed by an anonymous pa_mmap (transient use) or a named SID
 * file pa_mmap (persistent use).  All data lives in parrotdb atoms so that
 * the tree can survive across query invocations when persisted.
 *
 * Key encoding:
 *   Keys are NUL-separated multi-field byte strings, one field per xsl:sort.
 *   Each field is encoded for case order before the NUL separator:
 *     lower-first (default): each byte encoded as tolower(c), 0x00/0x01
 *     upper-first (^ flag):  raw bytes (ASCII uppercase < lowercase naturally)
 *
 * Sort direction is handled at walk time: ascending = pa_pat_find_next,
 * descending = pa_pat_find_prev.  The primary sort direction is detected
 * from the spec's first flag character so pin_op_for_each knows which walk
 * to use when pushing frames in reverse LIFO order.
 */

#ifndef LIBSLAX_PIN_SORT_H
#define LIBSLAX_PIN_SORT_H

#include <stdint.h>
#include <stdbool.h>
#include <parrotdb/pacommon.h>
#include <parrotdb/pammap.h>
#include <parrotdb/pafixed.h>
#include <parrotdb/paarb.h>
#include <parrotdb/papat.h>
#include <libpin/pin_node.h>

/*
 * One entry in a sort/index tree.  Lives in pst_entries (pa_fixed_t).
 *
 * pse_pat is embedded at offset 0 so the same pa_fixed_t pool serves as
 * both the patricia node pool (pp_nodes) and the data pool.  The atom of
 * an entry IS the atom of its patricia node (same raw integer, different
 * typed wrappers).  PATNODE_TO_STRUCT() or a direct cast works because
 * offsetof(pin_stree_entry_t, pse_pat) == 0.
 *
 * pse_key is an atom into pst_keys (pa_arb_t); pin_stree_key_func resolves it.
 */
typedef struct pin_stree_entry_s {
    pa_pat_node_t  pse_pat;    /* MUST be at offset 0: patricia node */
    pin_node_id_t  pse_node;   /* retained element atom in the parse tree */
    pa_arb_atom_t  pse_key;    /* key bytes atom in pst_keys */
    uint16_t       pse_klen;   /* key length in bytes */
    uint16_t       pse_pad;    /* alignment */
} pin_stree_entry_t;

/*
 * Key-function context stored in pa_pat_t::pp_data.
 * Allows pin_stree_key_func to reach the entry and key pools without globals.
 */
typedef struct pin_stree_ctx_s {
    pa_fixed_t *psc_entries;   /* same pool as pp_nodes */
    pa_arb_t   *psc_keys;      /* key byte strings */
} pin_stree_ctx_t;

/*
 * The sort/index tree handle.
 */
typedef struct pin_stree_s {
    pa_mmap_t       *pst_mmap;      /* backing mmap (anon for transient) */
    pa_fixed_t      *pst_entries;   /* pool of pin_stree_entry_t */
    pa_arb_t        *pst_keys;      /* pool for variable-length key bytes */
    pa_pat_t        *pst_pat;       /* patricia tree (pp_nodes = pst_entries) */
    pin_stree_ctx_t  pst_ctx;       /* key_func context */
} pin_stree_t;

/*
 * Create a transient sort tree backed by an anonymous mmap.
 * Returns NULL on allocation failure.
 */
pin_stree_t *pin_stree_create (void);

/* Free a transient sort tree and its backing mmap. */
void pin_stree_free (pin_stree_t *pst);

/*
 * Insert element node_id with the given key into the tree.
 * key[0..klen-1] is the encoded, NUL-separated sort key.
 * Returns 0 on success, -1 on failure.
 */
int pin_stree_insert (pin_stree_t *pst, const char *key, uint16_t klen,
                      pin_node_id_t node_id);

/* Sorted walk — ascending (smallest key first). */
pin_stree_entry_t *pin_stree_first (pin_stree_t *pst);
pin_stree_entry_t *pin_stree_next (pin_stree_t *pst, pin_stree_entry_t *e);

/* Sorted walk — descending (largest key first). */
pin_stree_entry_t *pin_stree_last (pin_stree_t *pst);
pin_stree_entry_t *pin_stree_prev (pin_stree_t *pst, pin_stree_entry_t *e);

/* Exact-match lookup (for slax:build-index). */
pin_stree_entry_t *pin_stree_lookup (pin_stree_t *pst,
                                     const char *key, uint16_t klen);

/*
 * Encode one sort-key field into keybuf starting at byte offset pos.
 * If upper == false (lower-first): each byte becomes (tolower(c), 0x00/0x01).
 * If upper == true (upper-first):  raw bytes.
 * Returns the new pos after writing.
 */
size_t pin_stree_encode_key (char *keybuf, size_t keycap, size_t pos,
                             const char *val, bool upper);

#endif /* LIBSLAX_PIN_SORT_H */
