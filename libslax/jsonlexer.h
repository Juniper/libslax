/*
 * $Id$
 *
 * Copyright (c) 2012, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

int
slaxJsonTagging (int tagging);

void
slaxJsonAddTypeInfo (slax_data_t *sdp, const char *value);

void
slaxJsonClearMember (slax_data_t *sdp);

int
slaxJsonIsTaggedNode (xmlNodePtr nodep);

int
slaxJsonIsTagged (slax_data_t *sdp);

void
slaxJsonTag (slax_data_t *sdp);

void
slaxJsonTagContent (slax_data_t *sdp, int content);

/**
 * Make a child node and assign it proper file/line number info.
 */
xmlNodePtr
slaxJsonAddChildLineNo (xmlParserCtxtPtr ctxt, xmlNodePtr parent, xmlNodePtr cur);

void
slaxJsonElementOpen (slax_data_t *sdp, const char *name);

void
slaxJsonElementOpenName (slax_data_t *sdp, char *name);

/* ----------------------------------------------------------------------
 * Functions exposed in jsonparser.y (no better place than here)
 */

/*
 * The bison-based parser's main function
 */
int
slaxJsonYyParse (slax_data_t *);

/*
 * Return a human-readable name for a given token type
 */
const char *
slaxJsonTokenName (int ttype);

/*
 * Expose YYTRANSLATE outside the bison file
 */
int
slaxJsonTokenTranslate (int ttype);

xmlDocPtr
slaxJsonDataToXml (const char *data, const char *root_name,
		       unsigned flags);

xmlDocPtr
slaxJsonFileToXml (const char *fname, const char *root_name,
		       unsigned flags);

void
slaxJsonElementValue (slax_data_t *sdp, slax_string_t *value);
