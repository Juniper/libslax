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
#include <libslax/pa_mmap.h>
#include <libslax/pa_istr.h>

#define NEED_T_ATOM
#define NEED_KEY
#include "pa_main.h"

pa_mmap_t *pmp;
pa_istr_info_t *piip;
pa_istr_t *pip;

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

void
test_open (void)
{
    pmp = pa_mmap_open(opt_filename, 0, 0644);
    assert(pmp);

    piip = pa_mmap_header(pmp, "istr", sizeof(*piip));
    assert(piip);

    pip = pa_istr_setup(pmp, piip, opt_shift, 2, opt_max_atoms);
    assert(pip);
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

    unsigned len = key ? strlen(key) : 0;
    pa_atom_t atom = pa_istr_string(pip, key);
    if (atom == PA_NULL_ATOM)
	return;

    test_t *tp = calloc(1, sizeof(*tp));

    trec[slot] = tp;
    if (tp) {
	tp->t_magic = opt_magic;
	tp->t_id = id++;
	tp->t_slot = slot;
	tp->t_atom = atom;
    }

    const char *str = pa_istr_atom_string(pip, atom);

    if (!opt_quiet)
	printf("in %u (%u) : %s -> %p (%#x) -> %p/%s\n",
	       slot, len, key, tp, atom, str, str);
}

void
test_dump (void)
{
    test_t *tp;
    unsigned slot;
    pa_atom_t atom;

    printf("dumping: (%u) len:%lu\n", opt_count, pip->pi_mmap->pm_len);
    for (slot = 0; slot < opt_count; slot++) {
	tp = trec[slot];
	if (tp == NULL)
	    continue;

	atom = trec[slot]->t_atom;
	const char *str;
	str = (atom == PA_NULL_ATOM) ? "" : pa_istr_atom_string(pip, atom);

	printf("%u : %#x -> %p [%s]\n", slot, atom, str, str);
    }
}

void
test_free (unsigned slot UNUSED)
{
    return;
}

void
test_print (unsigned slot)
{
    test_t *tp = trec[slot];

    if (tp) {
	pa_atom_t atom = trec[slot]->t_id;
	const char *str;
	str = (atom == PA_NULL_ATOM) ? "" : pa_istr_atom_string(pip, atom);

	if (!opt_quiet)
	    printf("%u : %#x -> %p [%s]\n", slot, atom, str, str);
    } else {
	printf("%u : free\n", slot);
    }
}

void
test_close (void)
{
    return;
}
