/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer <phil@>, May 2016
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <assert.h>
#include <stddef.h>
#include <ctype.h>
#include <limits.h>

#include <parrotdb/pacommon.h>
#include <parrotdb/paconfig.h>
#include <parrotdb/pammap.h>
#include <parrotdb/paarb.h>
#include <parrotdb/pafixed.h>
#include <parrotdb/paistr.h>
#include <parrotdb/papat.h>

#define NEED_T_PAT
#define NEED_KEY
#define NEED_FULL_DUMP
#include "pamain.h"

pa_mmap_t *pmp;
pa_istr_t *pip;
pa_pat_t *ppp;

void
test_init (void)
{
#if 0
    opt_clean = 1;
    opt_quiet = 1;
    opt_input = "/tmp/2";
    opt_count = 1000;
    opt_filename = "/tmp/foo.db";
#endif
}

static const uint8_t *
test_key_func (pa_pat_t *root, pa_pat_data_atom_t datom)
{
    /* Need to "convert" the data atom to an istr data */
    pa_istr_atom_t atom = pa_istr_atom(pa_pat_data_atom_of(datom));
    return (const uint8_t *) pa_istr_atom_string(root->pp_data, atom);
}

void
test_open (void)
{
    pmp = pa_mmap_open(opt_filename, "pa05", 0, 0644);
    assert(pmp);

    pip = pa_istr_open(pmp, "istr", opt_shift, 2, opt_max_atoms);
    assert(pip);

    ppp = pa_pat_open(pmp, "pat", pip, test_key_func,
		      PA_PAT_MAXKEY, opt_shift, opt_max_atoms);
}

void
test_alloc (unsigned slot UNUSED, unsigned this_size UNUSED)
{
    return;
}

void
test_key (unsigned slot, const char *key)
{
    size_t len = key ? strlen(key) : 0;
    pa_istr_atom_t atom = pa_istr_null_atom();

    if (len == 0)
	return;

    test_t *tp = calloc(1, sizeof(*tp) + len + 1);

    trec[slot] = tp;
    if (tp) {
	tp->t_magic = opt_magic;
	tp->t_slot = slot;
	memcpy(tp->t_val, key, len + 1);

	atom = pa_istr_string(ppp->pp_data, key);
	tp->t_id = pa_istr_atom_of(atom);

	if (!pa_pat_add(ppp, pa_pat_data_atom(pa_istr_atom_of(atom)), len + 1))
	    pa_warning(0, "duplicate key: %s", key);
    }

    if (!opt_quiet)
	printf("in %u (%lu) : %s -> (%#x)\n",
	       slot, len, key, pa_istr_atom_of(atom));
}

void
test_list (const char *key)
{
    uint16_t plen = strlen(key) * NBBY;
    pa_pat_node_t *node = NULL;
    pa_pat_data_atom_t atom;
    pa_istr_atom_t iatom;
    const char *data;

    node = pa_pat_subtree_match(ppp, plen, key);
    while (node != NULL) {
	atom = pa_pat_node_data(ppp, node);
	iatom = pa_istr_atom(pa_pat_data_atom_of(atom));
	data = pa_istr_atom_string(pip, iatom);
	printf("  %p %#x -> %p [%s]\n",
	       node, pa_istr_atom_of(iatom), data, data ?: "");
	node = pa_pat_subtree_next(ppp, node, plen);
    }
}

void
test_dump (void)
{
    test_t *tp;
    unsigned slot;
    const char *data;
    pa_istr_atom_t iatom;

    for (slot = 0; slot < opt_count; slot++) {
	tp = trec[slot];
	if (tp) {
	    iatom = pa_istr_atom(trec[slot]->t_id);

	    data = pa_istr_atom_string(pip, iatom);

	    printf("%u : %#x -> %p [%s]\n",
		   slot, pa_istr_atom_of(iatom), data, data);
	}
    }

    pa_pat_node_t *node = NULL;
    pa_pat_data_atom_t atom;

    while ((node = pa_pat_find_next(ppp, node)) != NULL) {
	atom = pa_pat_node_data(ppp, node);
	iatom = pa_istr_atom(pa_pat_data_atom_of(atom));
	data = pa_istr_atom_string(pip, iatom);
	printf("  %p %#x -> %p [%s]\n",
	       node, pa_istr_atom_of(iatom), data, data ?: "");
    }
}

void
test_free (unsigned slot UNUSED)
{
#if 0
    pa_atom_t atom;
    test_t *tp = trec[slot];
    if (tp) {
	atom = tp->t_id;
	if (!opt_quiet)
	    printf("free %u : %#x -> %p\n",
		   slot, atom, tp);

	if (!pa_pat_delete(ppp, NULL))
	    pa_warning(0, "delete failed for key: %u", slot);
	else free(tp);

	trec[slot] = NULL;
    } else {
	printf("%u : free\n", slot);
    }
#endif
}

void
test_print (unsigned slot)
{
    test_t *tp = trec[slot];

    if (tp) {
	pa_istr_atom_t iatom = pa_istr_atom(trec[slot]->t_id);
	const char *data = pa_istr_atom_string(pip, iatom);

	if (!opt_quiet)
	    printf("%u : %#x -> %p [%s]\n",
		   slot, pa_istr_atom_of(iatom), data, data);
    } else {
	printf("%u : free\n", slot);
    }
}

void
test_close (void)
{
    return;
}

void
test_full_dump (psu_boolean_t full)
{
    pa_mmap_dump(pmp, full);
    pa_istr_dump(pip, full);
}
