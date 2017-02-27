/*
 * Copyright (c) 2016-2017, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer (phil@) June 2016
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>

#include <parrotdb/pacommon.h>
#include <parrotdb/paconfig.h>
#include <parrotdb/pammap.h>
#include <parrotdb/pafixed.h>
#include <parrotdb/paarb.h>
#include <parrotdb/paistr.h>
#include <parrotdb/papat.h>
#include <libpsu/psulog.h>
#include <libpsu/psualloc.h>

typedef struct pa_config_variable_s {
    struct pa_config_variable_s *xcv_next;
    char *xcv_name;
    char *xcv_value;
} pa_config_variable_t;

/* Tail-queue list of config variables */
pa_config_variable_t *pa_config_vars;
pa_config_variable_t **pa_config_endp = &pa_config_vars;

/*
 * Massively simplistic function to extract name/value pairs
 */
static int
pa_config_extract (char *bp, char **namep, char **valp)
{
    char *ep, *vp;

    /* ^ name = value ; */
    for ( ; *bp; bp++)
	if (!isspace((int) *bp))
	    break;
    if (*bp == '\0')
	return -1;

    for (ep = bp; *ep; ep++)
	if (isspace((int) *ep))
	    break;
    if (*ep == '\0')
	return -1;

    *ep++ = '\0';
	
    /* name ^ = value ; */
    for ( ; *ep; ep++)
	if (!isspace((int) *ep) && *ep != '=')
	    break;
    if (*ep == '\0')
	return -1;

    /* name = ^ value ; */
    vp = ep;
    for (vp = ep; *ep; ep++)
	if (isspace((int) *ep) || *ep == ';')
	    break;
    if (*ep == '\0')
	return -1;

    /* name = value ^ ; */
    *ep++ = '\0';

    *namep = bp;
    *valp = vp;
    return 0;
}

void
pa_config_read_file (FILE *file)
{
    char buf[BUFSIZ];
    char *name, *value, *cp;
    pa_config_variable_t *xcvp;

    for (;;) {
	if (fgets(buf, sizeof(buf), file) == NULL)
	    break;

	if (buf[0] == '#' || buf[0] == ';')
	    continue;

	if (pa_config_extract(buf, &name, &value) < 0)
	    continue;

	size_t nlen = strlen(name) + 1;
	size_t vlen = strlen(value) + 1;
	xcvp = psu_realloc(NULL, sizeof(*xcvp) + nlen + vlen);
	if (xcvp == NULL)
	    continue;

	cp = (char *) &xcvp[1];
	xcvp->xcv_name = memcpy(cp, name, nlen);
	cp += nlen;
	xcvp->xcv_value = memcpy(cp, value, vlen);

	*pa_config_endp = xcvp;
	pa_config_endp = &xcvp->xcv_next;
    }
}

void
pa_config_read (const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (file) {
	pa_config_read_file(file);
	fclose(file);
    }
}

/*
 * Return a string value from the config file
 */
static const char *
pa_config_value_internal (const char *base, const char *name,
			  int show_def, uint32_t def)
{
    size_t blen = strlen(base), nlen = strlen(name);
    char namebuf[blen + nlen + 2];
    pa_config_variable_t *xcvp;

    /* Build the name */
    memcpy(namebuf, base, blen);
    namebuf[blen] = '.';
    memcpy(namebuf + blen + 1, name, nlen + 1);

    if (show_def)
	psu_log("config: looking for '%s' (default %u)", namebuf, def);
    else psu_log("config: looking for '%s'", namebuf);

    for (xcvp = pa_config_vars; xcvp; xcvp = xcvp->xcv_next) {
	if (strcmp(xcvp->xcv_name, namebuf) != 0)
	    continue;

	psu_log("config: found for '%s' -> '%s'",
		namebuf, xcvp->xcv_value);
	return xcvp->xcv_value;
    }

    return NULL;
}

const char *
pa_config_value (const char *base, const char *name)
{
    return pa_config_value_internal(base, name, 0, 0);
}

/*
 * Return an integer value from the config file, or the default value.
 */
uint32_t
pa_config_value32 (const char *base, const char *name, uint32_t def)
{
    unsigned long ival;
    const char *value = pa_config_value_internal(base, name, 1, def);
    if (value == NULL)
	return def;

    if (strncmp(value, "1<<", 3) == 0) {
	ival = strtoul(value + 3, NULL, 0);
	return (ival == ULONG_MAX || ival > 32 ) ? def : (1UL << ival);
    }

    if (strncmp(value, "1 <<", 4) == 0) {
	ival = strtoul(value + 4, NULL, 0);
	return (ival == ULONG_MAX || ival > 32 ) ? def : (1UL << ival);
    }

    ival = strtoul(value, NULL, 0);
    return (ival == ULONG_MAX) ? def : ival;
}
