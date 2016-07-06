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

#ifndef LIBSLAX_XI_COMMON_H
#define LIBSLAX_XI_COMMON_H

typedef uint8_t xi_node_type_t;	/* Type of node (XI_TYPE_*) */
typedef uint32_t xi_node_id_t;	/* Node identifier (index) */
typedef uint16_t xi_name_id_t;	/* Element name identifier (index) */
typedef uint16_t xi_ns_id_t;	/* Namespace identifier (index) */
typedef uint8_t xi_depth_t;	/* Depth in the hierarchy */
typedef off_t xi_offset_t;	/* Offset in file or buffer */
typedef uint32_t xi_source_flags_t; /* Flags for parser */

/* Type of XML nodes */
#define XI_TYPE_NONE	0	/* Unknown type */
#define XI_TYPE_EOF	1	/* End of file */
#define XI_TYPE_SKIP	2	/* Skip/ignored input */
#define XI_TYPE_FAIL	3	/* Failure mode */
#define XI_TYPE_ROOT	4	/* Root node (container); not "root element" */
#define XI_TYPE_TEXT	5	/* Escaped text content */
#define XI_TYPE_UNESC	6	/* Unescaped text content */
#define XI_TYPE_OPEN	7	/* Open tag */
#define XI_TYPE_CLOSE	8	/* Close tag */
#define XI_TYPE_EMPTY	9	/* Empty tag */
#define XI_TYPE_PI	10	/* Processing instruction */
#define XI_TYPE_DTD	11	/* <!DTD> nonsense */
#define XI_TYPE_COMMENT	12	/* Comment */
#define XI_TYPE_ATSTR	13	/* A string of all unparsed XML attributes */
#define XI_TYPE_ATTRIB	14	/* A single, parsed, unescaped XML attribute */
#define XI_TYPE_NS	15	/* XML namespace */
#define XI_TYPE_NSPREF	16	/* XML namespace */
#define XI_TYPE_NSVALUE	17	/* XML namespace */

#define XI_TYPE_ELT	XI_TYPE_OPEN
#define XI_TYPE_CDATA	XI_TYPE_UNESC	/* Cdata (<![CDATA[ ]]>) */

struct xi_source_s;
typedef struct xi_source_s xi_source_t;

struct xi_parse_s;
typedef struct xi_parse_s xi_parse_t;

struct xi_insert_s;
typedef struct xi_insert_s xi_insert_t;

struct xi_node_s;
typedef struct xi_node_s xi_node_t;

extern char xi_space_test[256];

/*
 * Whitespace in XML has a small, specific definition:
 *     (#x20 | #x9 | #xD | #xA)
 * We burn 256 bytes to make this a simple quick test because
 * we make this test a _huge_ number of times.
 */
static inline int
xi_isspace (int ch)
{
    return xi_space_test[ch & 0xff];
}

/*
 * Skip over whitespace.  This is an ambidextrous function, in
 * that it can move forward or backward, based on the "dir" parameter.
 * It returns a pointer to the first non-whitespace character.
 */
static inline char *
xi_skipws (char *cp, unsigned len, int dir)
{
    char ch;

    for (ch = *cp; len-- > 0; ch = *cp) {
	if (!xi_isspace(ch) || ch == '\0')
	    return cp;
	cp += dir;
    }

    return NULL;
}

#endif /* LIBSLAX_XI_COMMON_H */
