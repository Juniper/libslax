/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer (phil@) June 2016
 *
 * Definitions common to the parser, storage, and processing of nodes.
 */

#ifndef LIBSLAX_PIN_COMMON_H
#define LIBSLAX_PIN_COMMON_H

typedef uint32_t pin_source_flags_t; /* Flags for parser */
typedef uint8_t pin_depth_t;	/* Depth in the hierarchy */

#define PIN_DEPTH_MIN	1	/* Depth of top of tree (origin 1) */
#define PIN_DEPTH_MAX	254	/* Max depth of tree */

typedef uint16_t pin_node_flags_t; /* Flags for a node (PNF_*) */

/* Flags for pin_node_flags_t */
#define PNF_ATTRIBS_PRESENT	(1<<0) /* Attributes available */
#define PNF_ATTRIBS_EXTRACTED	(1<<1) /* Attributes aleady extracted */

typedef uint8_t pin_node_type_t;	/* Type of node (PIN_TYPE_*) */
/* Type of XML nodes (for pin_node_type_t) */
#define PIN_TYPE_NONE	0	/* Unknown type */
#define PIN_TYPE_EOF	1	/* End of file */
#define PIN_TYPE_SKIP	2	/* Skip/ignored input */
#define PIN_TYPE_FAIL	3	/* Failure mode */
#define PIN_TYPE_ROOT	4	/* Root node (container); not "root element" */
#define PIN_TYPE_TEXT	5	/* Escaped text content */
#define PIN_TYPE_UNESC	6	/* Unescaped text content */
#define PIN_TYPE_OPEN	7	/* Open tag */
#define PIN_TYPE_CLOSE	8	/* Close tag */
#define PIN_TYPE_EMPTY	9	/* Empty tag */
#define PIN_TYPE_PI	10	/* Processing instruction */
#define PIN_TYPE_DTD	11	/* <!DTD> nonsense */
#define PIN_TYPE_COMMENT	12	/* Comment */
#define PIN_TYPE_ATSTR	13	/* A string of all unparsed XML attributes */
#define PIN_TYPE_ATTRIB	14	/* A single, parsed, unescaped XML attribute */
#define PIN_TYPE_EOL_ATTRIB 15	/* Pseudo-type: end-of-list for attributes */
#define PIN_TYPE_EOL_EMPTY 16	/* PT: end-of-attributes on empty tag */
#define PIN_TYPE_NS	17	/* XML namespace */
#define PIN_TYPE_NSPREF	18	/* XML namespace */

#define PIN_TYPE_ELT	PIN_TYPE_OPEN
#define PIN_TYPE_CDATA	PIN_TYPE_UNESC	/* Cdata (<![CDATA[ ]]>) */

#define PIN_XMLNS_LEADER "xmlns"	/* String that starts namespace attributes */

typedef uint8_t pin_boolean_t;	/* Base boolean type */
typedef off_t pin_offset_t;	/* Offset in file or buffer */

/* Define opaque types for function prototypes */
struct pin_source_s; typedef struct pin_source_s pin_source_t;
struct pin_parse_s; typedef struct pin_parse_s pin_parse_t;
struct pin_insert_s; typedef struct pin_insert_s pin_insert_t;
struct pin_rstate_s; typedef struct pin_rstate_s pin_rstate_t;
struct pin_rule_s; typedef struct pin_rule_s pin_rule_t;
struct pin_node_s; typedef struct pin_node_s pin_node_t;
struct pin_workspace_s; typedef struct pin_workspace_s pin_workspace_t;

/* Used to test whether a byte is white space */
extern char pin_space_test[256];

/*
 * Whitespace in XML has a small, specific definition:
 *     (#x20 | #x9 | #xD | #xA)
 * We burn 256 bytes to make this a simple quick test because
 * we make this test a _huge_ number of times.
 */
static inline int
pin_isspace (int ch)
{
    return pin_space_test[ch & 0xff];
}

/*
 * Skip over whitespace.  This is an ambidextrous function, in
 * that it can move forward or backward, based on the "dir" parameter.
 * It returns a pointer to the first non-whitespace character.  If
 * the string is entirely whitespace, NULL is returned.
 */
static inline char *
pin_skipws (char *cp, unsigned len, int dir)
{
    char ch;

    for (ch = *cp; len-- > 0; ch = *cp) {
	if (!pin_isspace(ch) || ch == '\0')
	    return cp;
	cp += dir;
    }

    return NULL;
}

#endif /* LIBSLAX_PIN_COMMON_H */
