/*
 * Copyright (c) 2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * yamlwriter.h -- turn yaml-oriented XML into yaml text
 */

int
slaxYamlWriteNode (slaxWriterFunc_t func, void *data, xmlNodePtr nodep,
		       unsigned flags);

int
slaxYamlWriteDoc (slaxWriterFunc_t func, void *data, xmlDocPtr docp,
		      unsigned flags);

#define YWF_ROOT	(1<<0)	/* Root node */
#define YWF_ARRAY	(1<<1)	/* Inside array */
#define YWF_NODESET	(1<<2)	/* Top of a nodeset */
#define YWF_PRETTY	(1<<3)	/* Pretty print (newlines) */

#define YWF_OPTIONAL_QUOTES (1<<4)	/* Don't use quotes unless needed */
#define YWF_IDENT       (1<<5)  /* First/indentifying field/child */
#define YWF_NOHEADER    (1<<6)  /* Don't make a "---" header */
