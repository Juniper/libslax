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
 * pin_stree_t — parrotdb-backed sort/index tree.
 * See pin_sort.h for design notes.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

#include "slaxconfig.h"
#include <libpsu/psulog.h>
#include <parrotdb/pacommon.h>
#include <parrotdb/pammap.h>
#include <parrotdb/pafixed.h>
#include <parrotdb/paarb.h>
#include <parrotdb/papat.h>

#include <libpin/pin_node.h>
#include <libpin/pin_sort.h>

#define PST_SHIFT    4      /* 16 entries per page in the fixed pool */
#define PST_MAX_ATOMS 1024  /* upper bound on sort entries per tree */

/*
 * Key function: resolve a pa_pat_data_atom_t to the key bytes stored
 * in the pa_arb pool.  Called internally by the patricia tree.
 */
static const psu_byte_t *
pin_stree_key_func (pa_pat_t *pp, pa_pat_data_atom_t datom)
{
    pin_stree_ctx_t *ctx = (pin_stree_ctx_t *) pp->pp_data;
    pa_fixed_atom_t fa = pa_fixed_atom(pa_pat_data_atom_of(datom));
    pin_stree_entry_t *e = pa_fixed_atom_addr(ctx->psc_entries, fa);
    if (e == NULL)
        return NULL;
    return (const psu_byte_t *) pa_arb_atom_addr(ctx->psc_keys, e->pse_key);
}

pin_stree_t *
pin_stree_create (void)
{
    pin_stree_t *pst = calloc(1, sizeof(*pst));
    if (pst == NULL)
        return NULL;

    pst->pst_mmap = pa_mmap_open(NULL, "sort", 0, 0);
    if (pst->pst_mmap == NULL)
        goto fail;

    pst->pst_entries = pa_fixed_open(pst->pst_mmap, "sort.entries",
                                     PST_SHIFT,
                                     (uint16_t) sizeof(pin_stree_entry_t),
                                     PST_MAX_ATOMS);
    if (pst->pst_entries == NULL)
        goto fail;

    pst->pst_keys = pa_arb_open(pst->pst_mmap, "sort.keys");
    if (pst->pst_keys == NULL)
        goto fail;

    pst->pst_ctx.psc_entries = pst->pst_entries;
    pst->pst_ctx.psc_keys = pst->pst_keys;

    /*
     * Use pst_entries as the patricia node pool.  Because pse_pat is at
     * offset 0 in pin_stree_entry_t, the patricia node atom and the entry
     * atom share the same raw value.  klen = PA_PAT_MAXKEY satisfies the
     * assertion in pa_pat_root_init; the actual per-node length is set in
     * pa_pat_node_init_length during pin_stree_insert.
     */
    pst->pst_pat = pa_pat_open_nodes(pst->pst_mmap, "sort.root",
                                     pst->pst_entries,
                                     &pst->pst_ctx,
                                     pin_stree_key_func,
                                     PA_PAT_MAXKEY);
    if (pst->pst_pat == NULL)
        goto fail;

    return pst;

fail:
    pin_stree_free(pst);
    return NULL;
}

void
pin_stree_free (pin_stree_t *pst)
{
    if (pst == NULL)
        return;
    if (pst->pst_pat)
        pa_pat_close(pst->pst_pat);
    if (pst->pst_keys)
        pa_arb_close(pst->pst_keys);
    if (pst->pst_entries)
        pa_fixed_close(pst->pst_entries);
    if (pst->pst_mmap)
        pa_mmap_close(pst->pst_mmap);
    free(pst);
}

int
pin_stree_insert (pin_stree_t *pst, const char *key, uint16_t klen,
                  pin_node_id_t node_id)
{
    if (pst == NULL || key == NULL || klen == 0)
        return -1;

    /* Allocate entry slot; this atom also serves as the patricia node atom */
    pa_pat_atom_t patom = pa_pat_atom_alloc(pst->pst_pat);
    if (pa_pat_is_null(patom))
        return -1;

    /* Store key bytes; do this before deriving the entry pointer in case
     * the arb pool causes the mmap to grow and invalidate raw pointers */
    pa_arb_atom_t katom = pa_arb_alloc(pst->pst_keys, klen);
    if (pa_arb_is_null(katom))
        return -1;
    void *kaddr = pa_arb_atom_addr(pst->pst_keys, katom);
    if (kaddr == NULL)
        return -1;
    memcpy(kaddr, key, klen);

    /* Re-derive entry pointer after potential mmap growth from arb alloc */
    pin_stree_entry_t *e = (pin_stree_entry_t *) pa_pat_node(pst->pst_pat, patom);
    if (e == NULL)
        return -1;

    e->pse_node = node_id;
    e->pse_key = katom;
    e->pse_klen = klen;

    /* Data atom shares the same raw value as the pat/fixed atom */
    pa_pat_data_atom_t datom = pa_pat_data_atom(pa_pat_atom_of(patom));
    pa_pat_node_init_length(&e->pse_pat, klen, datom);
    pa_pat_add_node(pst->pst_pat, patom, &e->pse_pat);
    return 0;
}

pin_stree_entry_t *
pin_stree_first (pin_stree_t *pst)
{
    if (pst == NULL)
        return NULL;
    return (pin_stree_entry_t *) pa_pat_find_next(pst->pst_pat, NULL);
}

pin_stree_entry_t *
pin_stree_next (pin_stree_t *pst, pin_stree_entry_t *e)
{
    if (pst == NULL || e == NULL)
        return NULL;
    return (pin_stree_entry_t *) pa_pat_find_next(pst->pst_pat, &e->pse_pat);
}

pin_stree_entry_t *
pin_stree_last (pin_stree_t *pst)
{
    if (pst == NULL)
        return NULL;
    return (pin_stree_entry_t *) pa_pat_find_prev(pst->pst_pat, NULL);
}

pin_stree_entry_t *
pin_stree_prev (pin_stree_t *pst, pin_stree_entry_t *e)
{
    if (pst == NULL || e == NULL)
        return NULL;
    return (pin_stree_entry_t *) pa_pat_find_prev(pst->pst_pat, &e->pse_pat);
}

pin_stree_entry_t *
pin_stree_lookup (pin_stree_t *pst, const char *key, uint16_t klen)
{
    if (pst == NULL || key == NULL || klen == 0)
        return NULL;
    return (pin_stree_entry_t *) pa_pat_get(pst->pst_pat, klen, key);
}

size_t
pin_stree_encode_key (char *keybuf, size_t keycap, size_t pos,
                      const char *val, bool upper)
{
    if (val == NULL || keybuf == NULL)
        return pos;

    if (upper) {
        /* Upper-first: raw bytes. ASCII uppercase (65-90) < lowercase (97-122). */
        for (const char *p = val; *p && pos < keycap; p += 1)
            keybuf[pos++] = *p;
    } else {
        /* Lower-first: encode each byte as (tolower(c), case-discriminator).
         * Lowercase gets 0x00; uppercase gets 0x01.
         * memcmp on encoded bytes gives lowercase-before-uppercase order. */
        for (const char *p = val; *p && pos + 1 < keycap; p += 1) {
            unsigned char c = (unsigned char) *p;
            keybuf[pos++] = (char) tolower((int) c);
            keybuf[pos++] = isupper((int) c) ? '\x01' : '\x00';
        }
    }

    return pos;
}
