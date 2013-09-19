/*
 * $Id$
 *
 * Copyright (c) 2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * jsonwriter.h -- turn json-oriented XML into json text
 */

int
slaxJsonWriteNode (slaxWriterFunc_t func, void *data, xmlNodePtr nodep,
		       unsigned flags);

int
slaxJsonWriteDoc (slaxWriterFunc_t func, void *data, xmlDocPtr docp,
		      unsigned flags);

#define JWF_ROOT	(1<<0)	/* Root node */
#define JWF_ARRAY	(1<<1)	/* Inside array */
#define JWF_NODESET	(1<<2)	/* Top of a nodeset */
#define JWF_PRETTY	(1<<3)	/* Pretty print (newlines) */

#define JWF_OPTIONAL_QUOTES (1<<4)	/* Don't use quotes unless needed */
