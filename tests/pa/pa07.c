/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer <phil@>, July 2016
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>

#include <parrotdb/pacommon.h>
#include <parrotdb/paconfig.h>
#include <parrotdb/pammap.h>
#include <parrotdb/pafixed.h>
#include <parrotdb/pabitmap.h>

#include "pamain.h"

pa_mmap_t *pmp;
pa_bitmap_t *pbp;
pa_bitmap_id_t bitmap;

void
test_init (void)
{
    printf("PA_BITMAP_BITS_PER_CHUNK %u\n", PA_BITMAP_BITS_PER_CHUNK);
    printf("PA_BITMAP_BITS_PER_UNIT %lu\n", PA_BITMAP_BITS_PER_UNIT);
    printf("PA_BITMAP_UNITS_PER_CHUNK %lu\n", PA_BITMAP_UNITS_PER_CHUNK);
    printf("PA_BITMAP_BLOCK_SHIFT %u\n", PA_BITMAP_BLOCK_SHIFT);
    printf("PA_BITMAP_BLOCK_SIZE %u\n", PA_BITMAP_BLOCK_SIZE);
    printf("PA_BITMAP_CHUNK_SHIFT %u\n", PA_BITMAP_CHUNK_SHIFT);
    printf("PA_BITMAP_CHUNK_SIZE %u\n", PA_BITMAP_CHUNK_SIZE);
    printf("PA_BITMAP_MAX_BIT %u\n", PA_BITMAP_MAX_BIT);
    printf("PA_BITMAP_MAX_ATOMS %u\n", PA_BITMAP_MAX_ATOMS);

    if (opt_count > PA_BITMAP_MAX_BIT) {
	printf("count > PA_BITMAP_MAX_BIT (%d.vs.%d); reduced\n",
	       opt_count, PA_BITMAP_MAX_BIT);
	opt_count = PA_BITMAP_MAX_BIT;
    }
}

void
test_open (void)
{
    pmp = pa_mmap_open(opt_filename, "pa07", 0, 0644);
    assert(pmp != NULL);

    pbp = pa_bitmap_open(pmp, "pa07.bitmap");
    assert(pbp != NULL);

    bitmap = pa_bitmap_alloc(pbp);
}

void
test_alloc (unsigned slot, unsigned size UNUSED)
{
    int was = pa_bitmap_test(pbp, bitmap, slot);

    pa_bitmap_set(pbp, bitmap, slot);

    if (!opt_quiet)
	printf("set %u%s%s\n", slot,
	       was ? " was-on" : "",
	       pa_bitmap_test(pbp, bitmap, slot) ? "" : " still-off" );
}

void
test_free (unsigned slot)
{
    int was = pa_bitmap_test(pbp, bitmap, slot);

    pa_bitmap_clear(pbp, bitmap, slot);

    if (!opt_quiet)
	printf("clr %u%s%s\n", slot,
	       was ? "" : " was-off",
	       pa_bitmap_test(pbp, bitmap, slot) ? " still-on" : "" );
}

void
test_close (void)
{
    pa_bitmap_close(pbp);
}

void
test_print (unsigned slot)
{
    if (!opt_quiet)
	printf("tst %u %s\n", slot,
	       pa_bitmap_test(pbp, bitmap, slot) ? "on" : "off" );
}

void test_dump (void)
{
    pa_bitnumber_t num = PA_BITMAP_FIND_START;

    for (;;) {
	num = pa_bitmap_find_next(pbp, bitmap, num);

	if (num == PA_BITMAP_FIND_DONE)
	    break;

	printf("dump: %u\n", num);
    }
}
