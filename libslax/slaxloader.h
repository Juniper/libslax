/*
 * $Id$
 *
 * Copyright (c) 2006-2011, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

/* Stub to handle xmlChar strings in "?:" expressions */
extern const xmlChar slaxNull[];

/**
 * Check the version string.  The only supported versions are "1.0" and "1.1".
 *
 * @param major major version number
 * @param minor minor version number
 */
void
slaxVersionMatch (slax_data_t *sdp, const char *vers);

/*
 * Relocate the most-recent "sort" node, if the parent was a "for"
 * loop.  The "sort" should be at the top of the stack.  If the parent
 * is a "for", then the parser will have built two nested "for-each"
 * loops, and we really want the sort to apply to the parent (outer)
 * loop.  So if we've hitting these conditions, move the sort node.
 */
void
slaxRelocateSort (slax_data_t *sdp);

/*
 * Add an xsl:comment node
 */
xmlNodePtr
slaxCommentAdd (slax_data_t *sdp, slax_string_t *value);

/*
 * If we know we're about to assign a result tree fragment (RTF)
 * to a variable, punt and do The Right Thing.
 *
 * When we're called, the variable statement has ended, so the
 * <xsl:variable> should be the node on the top of the stack.
 * If there's something inside the element (as opposed to a
 * "select" attribute or no content), then this must be building
 * an RTF, which we dislike.  Instead we convert the current variable
 * into a temporary variable, and build the real variable using a
 * call to "ext:node-set()".  We'll need define the "ext" namespace
 * on the same (new) element, so we can ensure that it's been created.
 */
void
slaxAvoidRtf (slax_data_t *sdp);

/**
 * Create namespace alias between the prefix given and the
 * containing (current) prefix.
 *
 * @param sdp main slax data structure
 * @param style stylesheet prefix value (as declared in the "ns" statement)
 * @param results result prefix value
 */
void
slaxNamespaceAlias (slax_data_t *sdp, slax_string_t *style,
		    slax_string_t *results);

/**
 * Add an XPath expression as a statement.  If this is a string,
 * we place it inside an <xsl:text> element.  Otherwise we
 * put the value inside an <xsl:value-of>'s select attribute.
 *
 * @param sdp main slax data structure
 * @param value XPath expression to add
 * @param text_as_elt always use <xsl:text> for text data
 * @param disable_escaping add the "disable-output-escaping" attribute
 */
void
slaxElementXPath (slax_data_t *sdp, slax_string_t *value,
		  int text_as_elt, int disable_escaping);

/*
 * Check to see if an "if" statement had no "else".  If so,
 * we can rewrite it from an "xsl:choose" into an "xsl:if".
 */
void
slaxCheckIf (slax_data_t *sdp, xmlNodePtr choosep);

/*
 * Handle the case where an element is used as a function argument
 */
slax_string_t *
slaxHandleEltArg (slax_data_t *sdp, int is_list);
#define SLAX_ELTARG_PREFIX "slax-temp-arg-"
#define SLAX_ELTARG_FORMAT SLAX_ELTARG_PREFIX "%u"
#define SLAX_ELTARG_WIDTH 10

/*
 * Find an appropriate node to insert (PrevSibling) our internal
 * element-as-argument variable.  We can't insert this in the
 * middle of a <xsl:choose>, etc.
 */
xmlNodePtr
slaxHandleEltArgSafeInsert (xmlNodePtr base);

void 
slaxHandleEltArgPrep (slax_data_t *sdp);


/**
 * Turn a SLAX expression into an XPath one.  Returns a freshly
 * allocated string, or NULL.
 */
char *
slaxSlaxToXpath (const char *filename, int lineno,
		 const char *slax_expr, int *errorsp);

void
slaxMainElement (slax_data_t *sdp);
