/*
 * Copyright (c) 2006-2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

/*
 * Add a namespace to the node on the top of the stack
 */
void
slaxNsAdd (slax_data_t *, const char *prefix, const char *uri);

/*
 * Add an XSL element to the node at the top of the context stack
 */
xmlNodePtr
slaxElementAdd (slax_data_t *, const char *tag,
		const char *attrib, const char *value);
xmlNodePtr
slaxElementAddString (slax_data_t *sdp, const char *tag,
		      const char *attrib, slax_string_t *value);
xmlNodePtr
slaxElementAddEscaped (slax_data_t *, const char *tag,
		       const char *attrib, const char *value);

/*
 * Add an XSL element to the top of the context stack
 */
xmlNodePtr
slaxElementPush (slax_data_t *sdp, const char *tag,
			   const char *attrib, const char *value);

/*
 * Pop an XML element off the context stack
 */
void
slaxElementPop (slax_data_t *sdp);

/*
 * Backup the stack up 'til the given node is seen
 */
slax_string_t *
slaxStackClear (slax_data_t *sdp, slax_string_t **sspp,
				    slax_string_t **top);

/*
 * Backup the stack up 'til the given node is seen; return the given node.
 */
slax_string_t *
slaxStackClear2 (slax_data_t *sdp, slax_string_t **sspp,
		 slax_string_t **top, slax_string_t **retp);

/*
 * Give an error if the axis name is not valid
 */
void
slaxCheckAxisName (slax_data_t *sdp, slax_string_t *axis);

/*
 * 'style' values for slaxAttribAdd*()
 *
 * Style is mainly concerned with how to represent data that can't
 * be directly represented in XSLT.  The principle case is quotes,
 * which are a problem because string in an attribute cannot have both
 * single and double quotes.  SLAX can parse:
 *     expr "quotes are \" or \'";
 * but putting this into the "select" attribute of an <xsl:value-of>
 * is not legal XSLT.  So we need to know how the caller wants us to
 * handle this.  For select attributes on <xsl:*> nodes, values that
 * are strickly strings can be tossed into a text node as the contents
 * of the element:
 *     <xsl:value-of>quotes are " or '</xsl:value-of>
 * If the value is not a static string, then things get uglier:
 *     expr fred("quotes are \" or \'");
 * would need to become:
 *     <xsl:value-of select="fred(concat('quotes are " or ', &quot;'&quot;))"/>
 * If the attribute cannot accept an XPATH value, then this concat scheme
 * will not work, but we can use <xsl:attribute> to construct the
 * proper value:
 *     <node fred="quotes are \" or \'">;
 * would need to become:
 *     <node>
 *       <xsl:attribute name="fred>quotes are " or '</xsl:attribute>
 *     </node>
 * All this is pretty annoying, but it allows the script writer to
 * avoid thinking about quotes and having to understand what's going
 * on underneath.
 */
#define SAS_NONE	0	/* Don't do anything fancy */
#define SAS_XPATH	1	/* Use concat() is you need it */
#define SAS_ATTRIB	2	/* Use <xsl:attribute> if you need it */
#define SAS_SELECT	3	/* Use concat or (non-attribute) text node */

/*
 * Add a simple value attribute.
 */
void slaxAttribAdd (slax_data_t *sdp, int style,
		    const char *name, slax_string_t *value);

/*
 * Add a value to an attribute on an XML element.  The value consists of
 * one or more items, each of which can be a string, a variable reference,
 * or an xpath expression.  If the value (any items) does not contain an
 * xpath expression, we use the normal attribute substitution to achieve
 * this ({$foo}).  Otherwise, we use the xsl:attribute element to construct
 * the attribute.
 */
void slaxAttribAddValue (slax_data_t *sdp, const char *name,
			 slax_string_t *value);

/*
 * Add a simple value attribute as straight string
 */
void slaxAttribAddString (slax_data_t *sdp, const char *name,
			  slax_string_t *, unsigned flags);

/*
 * Add a literal value as an attribute to the current node
 */
void slaxAttribAddLiteral(slax_data_t *sdp, const char *name, const char *val);

/*
 * Add an element to the top of the context stack
 */
void slaxElementOpen (slax_data_t *sdp, const char *tag);

/*
 * Pop an element off the context stack
 */
void slaxElementClose (slax_data_t *sdp);

/*
 * Extend the existing value for an attribute, appending the given value.
 */
void slaxAttribExtend (slax_data_t *sdp, const char *attrib, const char *val);

/*
 * Extend the existing value for an XSL attribute, appending the given value.
 */
void slaxAttribExtendXsl (slax_data_t *sdp, const char *attrib,
			  const char *val);

/*
 * Construct a temporary (and unique!) namespace node and put the given
 * for the "func" exslt library and put the given node into
 * that namespace.  We also have to add this as an "extension"
 * namespace.
 */
void
slaxSetFuncNs (slax_data_t *sdp, xmlNodePtr nodep);

void
slaxSetSlaxNs (slax_data_t *sdp, xmlNodePtr nodep, int local);

void
slaxSetExtNs (slax_data_t *sdp, xmlNodePtr nodep, int local);

/**
 * Detect if the string needs the "slax" namespace
 * @value: string to test
 * @returns TRUE if the strings needs the slax namespace
 */
int
slaxNeedsSlaxNs (slax_string_t *value);

/**
 * Determine if a node's name and namespace match those given.
 * @nodep: the node to test
 * @uri: the namespace string
 * @name: the element name (localname)
 * @return non-zero if this is a match
 */
int slaxNodeIs (xmlNodePtr nodep, const char *uri, const char *name);

/**
 * Detect a particular xsl:* node
 *
 * @nodep: node to test
 * @name: <xsl:*> element name to test
 * @returns non-zero if the node matches the given name
 */
int
slaxNodeIsXsl (xmlNodePtr nodep, const char *name);

/**
 * Find a namespace, or make one if the prefix is well known
 */
xmlNsPtr
slaxFindNs (slax_data_t *sdp, xmlNodePtr nodep,
	       const char *prefix, int len);

/**
 * Check that we have a known namespace for a function name
 */
void
slaxCheckFunction (slax_data_t *sdp, const char *fname);

/**
 * Simple (casted) version of xmlGetProp
 */
static inline char *
slaxGetAttrib (xmlNodePtr nodep, const char *name)
{
    return (char *) xmlGetProp(nodep, (const xmlChar *) name);
}

void
slaxTernaryExpand (slax_data_t *, slax_string_t *, unsigned);

#define SLAX_TERNARY_PREFIX "$slax-ternary-"
#define SLAX_TERNARY_FUNCTION "slax:value"
#define SLAX_TERNARY_VAR_LEADER SLAX_TERNARY_FUNCTION "(" SLAX_TERNARY_PREFIX
#define SLAX_TERNARY_VAR_FORMAT SLAX_TERNARY_VAR_LEADER "%u)"
#define SLAX_TERNARY_COND_SUFFIX "-cond"
#define SLAX_TERNARY_VAR_FORMAT_WIDTH 12
