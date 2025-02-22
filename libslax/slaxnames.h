/*
 * Copyright (c) 2006-2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#define XSL_VERSION "1.0"

#define XSL_URI "http://www.w3.org/1999/XSL/Transform"
#define XSL_PREFIX  "xsl"

#define XML_URI "http://www.w3.org/XML/1998/namespace"
#define XML_PREFIX  "xml"

#define EXT_URI "http://xmlsoft.org/XSLT/namespace"
#define EXT_PREFIX "slax-ext"

#define FUNC_URI EXSLT_FUNCTIONS_NAMESPACE
#define FUNC_PREFIX "slax-func"

#define SLAX_URI "http://xml.libslax.org/slax"
#define SLAX_PREFIX "slax"

/* Attribute names */
#define ATT_CDATA_SECTION_ELEMENTS "cdata-section-elements"
#define ATT_CODE	"code"
#define ATT_COUNT	"count"
#define ATT_DATE	"date"
#define ATT_DISABLE_OUTPUT_ESCAPING "disable-output-escaping"
#define ATT_DISABLE_OUTPUT_ESCAPING "disable-output-escaping"
#define ATT_ELEMENTS	"elements"
#define ATT_EXCLUDE_RESULT_PREFIXES "exclude-result-prefixes"
#define ATT_EXTENSION_ELEMENT_PREFIXES "extension-element-prefixes"
#define ATT_FORMAT	"format"
#define ATT_FROM	"from"
#define ATT_GID		"gid"
#define ATT_GROUPING_SEPARATOR "grouping-separator"
#define ATT_GROUPING_SIZE "grouping-size"
#define ATT_HREF	"href"
#define ATT_INDENT	"indent"
#define ATT_JSON	"json"
#define ATT_LANG	"lang"
#define ATT_MAKE	"make"
#define ATT_MATCH	"match"
#define ATT_METHOD	"method"
#define ATT_MODE	"mode"
#define ATT_MUTABLE	"mutable"
#define ATT_MVARNAME	"mvarname"
#define ATT_NAME	"name"
#define ATT_NAMESPACE	"namespace"
#define ATT_NAN		"NaN"	/* Yes, it's mixed case */
#define ATT_PATH	"path"
#define ATT_PRIORITY	"priority"
#define ATT_RESULT_PREFIX "result-prefix"
#define ATT_SELECT	"select"
#define ATT_STYLESHEET_PREFIX "stylesheet-prefix"
#define ATT_SVARNAME	"svarname"
#define ATT_TERMINATE	"terminate"
#define ATT_TEST	"test"
#define ATT_TYPE	"type"
#define ATT_UID		"uid"
#define ATT_USE		"use"
#define ATT_USE_ATTRIBUTE_SETS	"use-attribute-sets"
#define ATT_VALUE	"value"
#define ATT_VERSION	"version" 

/* Element names */
#define ELT_APPEND_TO_VARIABLE "append-to-variable"
#define ELT_APPLY_IMPORTS "apply-imports"
#define ELT_APPLY_TEMPLATES "apply-templates"
#define ELT_ATTRIBUTE	"attribute"
#define ELT_ATTRIBUTE_SET "attribute-set"
#define ELT_BRIEF	"brief"
#define ELT_CALL_TEMPLATE "call-template"
#define ELT_CHOOSE	"choose"
#define ELT_COMMENT	"comment"
#define ELT_COPY	"copy"
#define ELT_COPY_OF	"copy-of"
#define ELT_CREATE	"create"
#define ELT_DATE	"date"
#define ELT_DEPTH	"depth"
#define ELT_DIRECTORY	"directory"
#define ELT_DIRECTORY	"directory"
#define ELT_DIRECTORY_LIST "directory-list"
#define ELT_DIRECTORY_NAME "directory-name"
#define ELT_ELEMENT	"element"
#define ELT_ENTRY	"entry"
#define ELT_ERRNO	"errno"
#define ELT_ERROR	"error"
#define ELT_EXECUTABLE	"executable"
#define ELT_FOR_EACH	"for-each"
#define ELT_FUNCTION	"function"
#define ELT_GROUP	"group"
#define ELT_HIDDEN	"hidden"
#define ELT_IF		"if"
#define ELT_IMPORT	"import"
#define ELT_INCLUDE	"include"
#define ELT_JSON	"json"
#define ELT_LINKS	"links"
#define ELT_MEMBER	"member"
#define ELT_MESSAGE	"message"
#define ELT_MODE	"mode"
#define ELT_NAME	"name"
#define ELT_NAME	"name"
#define ELT_NAMESPACE_ALIAS "namespace-alias"
#define ELT_OTHERWISE	"otherwise"
#define ELT_OUTPUT	"output"
#define ELT_OWNER	"owner"
#define ELT_PARAM	"param"
#define ELT_PATH	"path"
#define ELT_PERMISSIONS "permissions"
#define ELT_PRETTY	"pretty"
#define ELT_QUOTES	"quotes"
#define ELT_RECURSE	"recurse"
#define ELT_RESULT	"result"
#define ELT_SET_VARIABLE "set-variable"
#define ELT_SIZE	"size"
#define ELT_SORT	"sort"
#define ELT_STYLESHEET	"stylesheet"
#define ELT_SYMLINK	"symlink"
#define ELT_SYMLINK_TARGET "symlink-target"
#define ELT_TEMPLATE	"template"
#define ELT_TEXT	"text"
#define ELT_TRACE	"trace"
#define ELT_TRANSFORM	"transform"
#define ELT_TYPE	"type"
#define ELT_USE_ATTRIBUTE_SETS	"use-attribute-sets"
#define ELT_VALUE_OF	"value-of"
#define ELT_VARIABLE	"variable"
#define ELT_WHEN	"when"
#define ELT_WHILE	"while"
#define ELT_WITH_PARAM	"with-param"

#define VAL_ARRAY	"array"
#define VAL_FALSE	"false"
#define VAL_JSON	"json"
#define VAL_MEMBER	"member"
#define VAL_NULL	"null"
#define VAL_NUMBER	"number"
#define VAL_OPTIONAL	"optional"
#define VAL_TRUE	"true"
#define VAL_XPATH	"xpath"

/* Names for generated code */
#define FOR_VARIABLE_PREFIX "$slax-dot-"
#define FUNC_BUILD_SEQUENCE "build-sequence"
#define FUNC_MVAR_INIT "mvar-init"
