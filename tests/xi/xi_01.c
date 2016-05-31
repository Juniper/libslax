/*
 * $Id$
 *
 * Copyright (c) 2016, Juniper Networks, Inc.
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
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <err.h>
#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>

#include "slaxconfig.h"
#include <libslax/slax.h>
#include <libslax/xi_io.h>

int
main (int argc, char **argv)
{
    const char *opt_filename = NULL;
    int opt_read = 0;
    int opt_quiet = 0;
    int fd = 0;
    xi_parse_flags_t flags = 0;

    for (argc = 1; argv[argc]; argc++) {
	if (strcmp(argv[argc], "file") == 0) {
	    if (argv[argc + 1])
		opt_filename = argv[++argc];
	} else if (strcmp(argv[argc], "read") == 0) {
	    opt_read = 1;
	} else if (strcmp(argv[argc], "quiet") == 0) {
	    opt_quiet = 1;
	} else if (strcmp(argv[argc], "trim") == 0) {
	    flags |= XPSF_TRIMWS;
	}
    }

    if (opt_filename != NULL) {
	fd = open(opt_filename, O_RDONLY);
	if (fd < 0)
	    err(1, "could not open file: %s", opt_filename);
    }

    xi_parse_source_t *srcp = xi_parse_create(fd, flags);
    if (srcp == NULL)
	errx(1, "failed to create source");

    char *data, *rest;
    xi_node_type_t type;
    for (;;) {
	type = xi_parse_next_token(srcp, &data, &rest);

	switch (type) {
	case XI_TYPE_NONE:	/* Unknown type */
	    return 1;

	case XI_TYPE_EOF:	/* End of file */
	    return 0;

	case XI_TYPE_FAIL:	/* Failure mode */
	    return -1;

	case XI_TYPE_TEXT:	/* Text content */
	    if (!opt_quiet)
		printf("data [%.*s]\n", (int)(rest - data), data);
	    break;

	case XI_TYPE_OPEN:	/* Open tag */
	    if (!opt_quiet)
		printf("open tag [%s] [%s]\n", data ?: "", rest ?: "");
	    break;

	case XI_TYPE_CLOSE:	/* Close tag */
	    if (!opt_quiet)
		printf("close tag [%s] [%s]\n", data ?: "", rest ?: "");
	    break;

	case XI_TYPE_PI:	/* Processing instruction */
	    if (!opt_quiet)
		printf("pi [%s] [%s]\n", data ?: "", rest ?: "");
	    break;

	case XI_TYPE_DTD:	/* DTD nonsense */
	    break;

	case XI_TYPE_COMMENT:	/* Comment */
	    if (!opt_quiet)
		printf("comment [%s] [%s]\n", data ?: "", rest ?: "");
	    break;

	case XI_TYPE_ATTR:	/* XML attribute */
	    break;

	case XI_TYPE_NS:	/* XML namespace */
	    break;

	}
    }

    xi_parse_destroy(srcp);

    return 0;
}
