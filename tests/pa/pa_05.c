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

#include "slaxconfig.h"
#include <libslax/slax.h>
#include <libslax/pa_common.h>
#include <libslax/pa_config.h>
#include <libslax/pa_mmap.h>
#include <libslax/pa_arb.h>
#include <libslax/pa_fixed.h>
#include <libslax/pa_istr.h>
#include <libslax/pa_pat.h>

#define NEED_T_PAT
#define NEED_T_ATOM
#define NEED_KEY
#include "pa_main.h"

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
test_key_func (pa_pat_t *root, pa_pat_node_t *node)
{
    return (const uint8_t *) pa_istr_atom_string(root->pp_data, node->ppn_data);
}

void
test_open (void)
{
    pmp = pa_mmap_open(opt_filename, 0, 0644);
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
    static unsigned id;
    size_t len = key ? strlen(key) : 0;
    pa_atom_t atom = PA_NULL_ATOM;

    if (len == 0)
	return;

    test_t *tp = calloc(1, sizeof(*tp) + len + 1);

    trec[slot] = tp;
    if (tp) {
	tp->t_magic = opt_magic;
	tp->t_id = id++;
	tp->t_slot = slot;
	memcpy(tp->t_val, key, len + 1);

	atom = pa_istr_string(ppp->pp_data, key);
	tp->t_atom = atom;

	if (!pa_pat_add(ppp, atom, len + 1))
	    pa_warning(0, "duplicate key: %s", key);
    }

    if (!opt_quiet)
	printf("in %u (%lu) : %s -> (%#x)\n", slot, len, key, atom);
}

void
test_dump (void)
{
    test_t *tp;
    unsigned slot;
    pa_atom_t atom;
    const char *data;

    for (slot = 0; slot < opt_count; slot++) {
	tp = trec[slot];
	if (tp) {
	    atom = trec[slot]->t_atom;

	    data = pa_istr_atom_string(pip, atom);

	    printf("%u : %#x -> %p [%s]\n",
		   slot, atom, trec[slot], data);
	}
    }

    pa_pat_node_t *node = NULL;
    while ((node = pa_pat_find_next(ppp, node)) != NULL) {
	atom = pa_pat_node_data(ppp, node);
	data = pa_istr_atom_string(pip, atom);
	printf("  %p %#x -> %p [%s]\n", node, atom, data, data ?: "");
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
	pa_atom_t atom = trec[slot]->t_id;
	if (!opt_quiet)
	    printf("%u : %#x -> %p\n",
		   slot, atom, trec[slot]);
    } else {
	printf("%u : free\n", slot);
    }
}

void
test_close (void)
{
    return;
}
