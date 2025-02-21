/*
 * Copyright (c) 2016-2025, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer <phil@>, April 2016
 */

#include <libpsu/psulog.h>
#include <libpsu/psualloc.h>

#define MAX_VAL	100

typedef struct test_s {
    uint32_t t_magic;
    pa_atom_t t_id;
    uint32_t t_slot;

#ifdef NEED_T_SIZE
    unsigned t_size;
#endif /* NEED_T_SIZE */

#ifdef NEED_T_PAT
    pa_pat_node_t t_pat;
#endif /* NEED_T_PAT */

#ifdef NEED_T_ATOM
    pa_atom_t t_atom;
#endif /* NEED_T_ATOM */

    int t_val[0];
} test_t;

test_t **trec;

unsigned opt_max_atoms = 1 << 14;
unsigned opt_shift = 6;
unsigned opt_count = 100;
unsigned opt_magic = 0x5e5e5e5e;
const char *opt_filename;
const char *opt_input;
const char *opt_config;
int opt_clean, opt_quiet, opt_dump, opt_top_dump;
uint32_t opt_size = 8;
int opt_value = -1;
int opt_value_index = 2;
int opt_bad_value_test = 1;

FILE *infile;

void test_init(void);
void test_open(void);
void test_close(void);
void test_alloc(unsigned slot, unsigned size);
void test_key(unsigned slot, const char *key);
void test_list(const char *key);
void test_free(unsigned slot);
void test_print(unsigned slot);
void test_dump(void);
void test_full_dump(psu_boolean_t);
void test_other(char *buf);

static char *
scan_uint32 (char *cp, uint32_t *valp)
{
    if (cp == NULL)
	return NULL;

    unsigned long val;

    while (isspace((int) *cp))
	cp += 1;

    if (*cp == '\0')
	return NULL;

    val = strtoul(cp, &cp, 0);
    *valp = val;
    return (val == ULONG_MAX) ? NULL : cp;
}

int
main (int argc UNUSED, char **argv UNUSED)
{
    psu_log_enable(TRUE);

    for (argc = 1; argv[argc]; argc++) {
	if (argv[argc][0] == '#') /* Ignore comments */
	    continue;

	if (strcmp(argv[argc], "max") == 0) {
	    if (argv[argc + 1]) 
		opt_max_atoms = atoi(argv[++argc]);
	} else if (strcmp(argv[argc], "count") == 0) {
	    if (argv[argc + 1]) 
		opt_count = atoi(argv[++argc]);
	} else if (strcmp(argv[argc], "config") == 0) {
	    if (argv[argc + 1]) 
		opt_config = argv[++argc];
	} else if (strcmp(argv[argc], "size") == 0) {
	    if (argv[argc + 1]) 
		opt_size = atoi(argv[++argc]);
	} else if (strcmp(argv[argc], "shift") == 0) {
	    if (argv[argc + 1]) 
		opt_shift = atoi(argv[++argc]);
	} else if (strcmp(argv[argc], "value") == 0) {
	    if (argv[argc + 1]) 
		opt_value = atoi(argv[++argc]);
	} else if (strcmp(argv[argc], "value-indexn") == 0) {
	    if (argv[argc + 1]) 
		opt_value_index = atoi(argv[++argc]);
	} else if (strcmp(argv[argc], "no-value-test") == 0) {
	    opt_bad_value_test = 0;
	} else if (strcmp(argv[argc], "file") == 0) {
	    if (argv[argc + 1]) 
		opt_filename = argv[++argc];
	} else if (strcmp(argv[argc], "clean") == 0) {
	    opt_clean = 1;
	} else if (strcmp(argv[argc], "quiet") == 0) {
	    opt_quiet = 1;
	} else if (strcmp(argv[argc], "dump") == 0) {
	    opt_dump = 1;
	} else if (strcmp(argv[argc], "top-dump") == 0) {
	    opt_top_dump = 1;
	} else if (strcmp(argv[argc], "input") == 0) {
	    if (argv[argc + 1]) 
		opt_input = argv[++argc];
	}
    }

    test_init();

    if (opt_clean && opt_filename)
	unlink(opt_filename);

    infile = opt_input ? fopen(opt_input, "r") : stdin;
    assert(infile);

    trec = psu_calloc(opt_count * sizeof(test_t));
    if (trec == NULL)
	return -1;

    if (opt_bad_value_test) {
	test_t *tp;
	if (opt_size - sizeof(*tp) <= opt_value_index * sizeof(tp->t_val[0]))
	    opt_bad_value_test = 0;
    }

    if (opt_size < sizeof(test_t))
	opt_size = sizeof(test_t);

    test_open();

    char buf[128];
    char *cp;
    uint32_t slot, this_size;

    for (;;) {
	if (opt_top_dump)
	    test_dump();

	cp = fgets(buf, sizeof(buf), infile);
	if (cp == NULL)
	    break;

	/* chomp(cp) */
	size_t len = strlen(cp);
	if (len > 1 && cp[len - 1] == '\n') {
	    cp[len - 1] = '\0';
	    if (len > 2 && cp[len - 2] == '\r')
		cp[len - 2] = '\0';
	}

	switch (*cp++) {
	case '#':
	    printf("[%s]\n", cp);
	    continue;

	case 'a':
	    cp = scan_uint32(cp, &slot);
	    if (cp == NULL)
		break;

	    cp = scan_uint32(cp, &this_size);
	    if (cp == NULL)
		this_size = opt_size;

	    if (slot >= opt_count) {
		printf("slot %u > count %u\n", slot, opt_count);
		break;
	    }

	    test_alloc(slot, this_size);
	    break;

	case 'd':
	    if (opt_quiet)
		break;

	    test_dump();
	    break;

#ifdef NEED_FULL_DUMP
	case 'D':
	    if (opt_quiet)
		break;

	    test_full_dump(FALSE);
	    break;
#endif /* NEED_FULL_DUMP */

	case 'f':
	    cp = scan_uint32(cp, &slot);
	    if (cp == NULL)
		break;

	    if (slot >= opt_count) {
		printf("slot %u > count %u\n", slot, opt_count);
		break;
	    }

	    test_free(slot);
	    break;

#ifdef NEED_KEY
	case 'k':
	    cp = scan_uint32(cp, &slot);
	    if (cp == NULL)
		break;

	    if (slot >= opt_count) {
		printf("slot %u > count %u\n", slot, opt_count);
		break;
	    }

	    while (isspace((int) *cp))
		cp += 1;

	    test_key(slot, cp);
	    break;

	case 'l':
	    while (isspace((int) *cp))
		cp += 1;

	    if (cp[0] == '\0') {
		printf("missing key\n");
		break;
	    }

	    test_list(cp);
	    break;
#endif /* NEED_KEY */

	case 'p':
	    cp = scan_uint32(cp, &slot);
	    if (cp == NULL)
		break;

	    if (slot >= opt_count) {
		printf("slot %u > count %u\n", slot, opt_count);
		break;
	    }

	    test_print(slot);
	    break;

	case 'q':
	    goto done;
	}
    }

 done:
    if (opt_dump)
	test_dump();

    test_close();

    return 0;
}

#ifdef TEST_PRINT_DULL
void
test_print (unsigned slot)
{
#ifdef NEED_T_SIZE
#define TSIZE(_t) _t->t_size
#else /* NEED_T_SIZE */
#define TSIZE(_t) 0
#endif /* NEED_T_SIZE */
    test_t *tp = trec[slot];
    if (tp) {
	pa_atom_t atom = trec[slot]->t_id;
	
	printf("%u : %u -> %p %s%s%s [%d]\n",
	       slot, atom, trec[slot],
	       (tp->t_magic != opt_magic) ? " bad-magic" : "",
	       (tp->t_slot != slot) ? " bad-slot" : "",
	       (opt_bad_value_test && tp->t_val[opt_value_index] != opt_value)
	           ? " bad-value" : "",
	       TSIZE(tp));
    }
}

void
test_dump (void)
{
    unsigned slot;
    printf("dumping: (%u)\n", opt_count);

    for (slot = 0; slot < opt_count; slot++)
	test_print(slot);
}
#endif /* TEST_PRINT_DULL */
