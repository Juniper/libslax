/*
 * Copyright (c) 2016-2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer <phil@>, July 2026
 *
 * xi04.c -- test pa_istr_open_intern: string interning / deduplication
 *
 * Commands (one per line):
 *   # text      print [text] and continue (also sets test args via run-tests.sh)
 *   i <string>  intern string; report atom and whether it was a dedup hit
 *   d           dump all unique interned strings
 *   q           quit
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <assert.h>
#include <ctype.h>

#include <libpsu/psualloc.h>
#include <libpsu/psulog.h>
#include <parrotdb/pacommon.h>
#include <parrotdb/paconfig.h>
#include <parrotdb/pammap.h>
#include <parrotdb/pafixed.h>
#include <parrotdb/paistr.h>

#define XI04_MAX_ENTRIES  16384
#define XI04_SHIFT        6
#define XI04_ATOM_SHIFT   2
#define XI04_MAX_ATOMS    (1 << 14)

static pa_mmap_t *pmp;
static pa_istr_t *pip;

/* Remembered unique entries: raw atom value + original string */
static struct {
    pa_atom_t atom;
    char      str[128];
} entries[XI04_MAX_ENTRIES];
static unsigned n_entries;

/* Return index of atom in entries[], -1 if unseen */
static int
find_atom (pa_atom_t atom)
{
    unsigned i;
    for (i = 0; i < n_entries; i++)
        if (entries[i].atom == atom)
            return (int) i;
    return -1;
}

static void
do_intern (const char *str)
{
    pa_istr_atom_t iatom = pa_istr_intern_string(pip, str);
    pa_atom_t atom = pa_istr_atom_of(iatom);
    int idx = find_atom(atom);

    if (idx >= 0) {
        printf("intern [%s] atom 0x%x (dedup)\n", str, atom);
    } else {
        printf("intern [%s] atom 0x%x\n", str, atom);
        if (n_entries < XI04_MAX_ENTRIES) {
            entries[n_entries].atom = atom;
            snprintf(entries[n_entries].str,
                     sizeof(entries[0].str), "%s", str);
            n_entries++;
        }
    }
}

static void
do_dump (void)
{
    unsigned i;
    printf("dump: %u unique strings\n", n_entries);
    for (i = 0; i < n_entries; i++) {
        pa_istr_atom_t iatom = pa_istr_atom(entries[i].atom);
        const char *str = pa_istr_atom_string(pip, iatom);
        printf("  [%u] atom 0x%x : %s\n", i, entries[i].atom,
               str ? str : "(null)");
    }
}

int
main (int argc, char **argv)
{
    const char *opt_filename = "/tmp/xi04.db";
    const char *opt_input = NULL;
    int opt_clean = 0;

    psu_log_enable(TRUE);

    for (argc = 1; argv[argc]; argc++) {
        if (strcmp(argv[argc], "file") == 0) {
            if (argv[argc + 1]) opt_filename = argv[++argc];
        } else if (strcmp(argv[argc], "input") == 0) {
            if (argv[argc + 1]) opt_input = argv[++argc];
        } else if (strcmp(argv[argc], "clean") == 0) {
            opt_clean = 1;
        }
    }

    if (opt_clean && opt_filename)
        unlink(opt_filename);

    pmp = pa_mmap_open(opt_filename, "xi04", 0, 0644);
    assert(pmp);

    pip = pa_istr_open_intern(pmp, "istr", XI04_SHIFT, XI04_ATOM_SHIFT,
                              XI04_MAX_ATOMS);
    assert(pip);

    FILE *infile = opt_input ? fopen(opt_input, "r") : stdin;
    assert(infile);

    char buf[256];
    while (fgets(buf, sizeof(buf), infile)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
            buf[--len] = '\0';

        char *cp = buf;
        switch (*cp++) {
        case '#':
        case ';':
            printf("[%s]\n", cp);
            continue;

        case 'i':
            while (isspace((int) *cp)) cp++;
            if (*cp)
                do_intern(cp);
            break;

        case 'd':
            do_dump();
            break;

        case 'q':
            goto done;
        }
    }

done:
    pa_istr_close(pip);
    pa_mmap_close(pmp);

    return 0;
}
