#!/usr/bin/env bash
#
# Generates the xmlNode/xmlNs/xmlAttr/xmlDoc/xmlDtd accessor functions
# (and, in a second file group, xmlNodeSet's) from a table of field
# descriptions, in three flavors per group:
#
#   - xmlaccessors-inline.h : static inline functions (today's behavior)
#   - xmlaccessors-decl.h   : extern declarations for a non-inline build
#   - xmlaccessors.c        : the matching non-inline definitions
#
# The point: today these are `static inline`, so they cost nothing but
# also can't hide a real implementation change. The plan is to let
# tree.h pick between the inline header and the decl header via a
# controlling macro, build the non-inline flavor, and see whether LTO
# re-inlines the non-inline calls at link time with no measurable
# performance cost. If so, we're free to change the underlying node
# storage without a tree-wide rewrite, because everything already goes
# through these functions instead of touching struct fields directly.
#
# All generated files live under the build tree, not the source tree:
# the source tree (other than this script) stays read-only. The
# tree.h-struct headers land at
# $(top_builddir)/libbxml/include/libxml/gen/xmlaccessors-{inline,decl}.h,
# reached as <libxml/gen/xmlaccessors-inline.h> -- already resolvable
# tree-wide via configure.ac's existing LIBXML_CFLAGS, which already
# has -I$(top_builddir)/libbxml/include. The .c file lands alongside
# at $(top_builddir)/libbxml/gen/xmlaccessors.c.
#
# xmlNodeSet and xmlXPathContext are declared in xpath.h, not tree.h,
# so their accessors can't live in the same generated header: tree.h
# includes the tree-struct accessors before xpath.h even exists, and
# neither struct is defined until partway through xpath.h. A second,
# separately included file group -- xmlaccessors-xpath-{inline,decl}.h
# and xmlaccessors-xpath.c -- covers both, included from xpath.h right
# after the xmlXPathContext struct definition, same NOINLINE-flavor
# switch as tree.h.
#
# Table format, one field per row, colon-separated ("field:ctype:getter:
# setter:param:comment"). A field left empty between colons means
# "derive the default"; trailing fields can just be omitted instead of
# left as trailing empty colons:
#
#   prev:xmlNodePtr
#   doc:xmlDocPtr::xmlNodeSetDocRaw::xmlNodeSetDoc collides with...
#
# - ctype is the getter-return/setter-param type (it may contain
#   spaces, e.g. "const xmlChar *" -- only colons separate columns).
#   Use "get-type|set-type" to give the getter and setter different
#   types (e.g. a const-qualified getter over a non-const field, with
#   no cast needed since the two are compiled from the same row).
# - getter/setter default to xml<Struct>Get<Field>/xml<Struct>Set<Field>
#   (field name with its first letter capitalized) when left empty.
#   setter "none" means the field is getter-only: no setter is emitted.
# - param (the setter's value parameter name) defaults to the field
#   name when left empty.
#
# Array fields (an "*_array_fields" table, e.g. nodeset_array_fields)
# use a different row shape ("field:elemtype:getter:setter:param:
# comment") and get a per-element xml<Struct>Get<Entry>(ptr, index)
# accessor instead of a getter returning the raw array pointer -- see
# emit_array_field. These default to getter-only (empty setter column):
# put "auto" in the setter column to opt into a xml<Struct>Set<Entry>
# (ptr, index, value) setter, or an explicit name to override it.
#
# Run with no arguments to regenerate all three files using the default
# (in-tree) build directory, or pass a top_srcdir/top_builddir pair the
# way libbxml/Makefile.am's generation rule does:
#
#   gen-xmlaccessors.sh $(top_srcdir) $(top_builddir)

set -e

top_srcdir=${1:-$(cd "$(dirname "$0")/.." && pwd)}
top_builddir=${2:-$top_srcdir/build}

incdir="$top_builddir/libbxml/include/libxml/gen"
srcdir="$top_builddir/libbxml/gen"

inline_h="$incdir/xmlaccessors-inline.h"
decl_h="$incdir/xmlaccessors-decl.h"
impl_c="$srcdir/xmlaccessors.c"

mkdir -p "$incdir" "$srcdir"

# ----------------------------------------------------------------------
# Field tables: struct-tag ptr-type var-name, followed by its rows.
# ----------------------------------------------------------------------

node_tag=Node ; node_ptr=xmlNodePtr ; node_var=node
node_fields='
type:xmlElementType
name:const xmlChar *::xmlNodeSetNameRaw::xmlNodeSetName is already public libxml2 API with different semantics (dict-aware rename); this sets the raw name field pointer instead.
children:xmlNodePtr
last:xmlNodePtr
parent:xmlNodePtr
next:xmlNodePtr
prev:xmlNodePtr
doc:xmlDocPtr::xmlNodeSetDocRaw::xmlNodeSetDoc collides with an internal (static) helper of the same name in tree.c, which recursively updates a subtree'"'"'s dict along with the doc pointer; this sets the raw doc field pointer on a single node instead.
ns:xmlNsPtr
content:xmlChar *:xmlNodeGetContentRaw:xmlNodeSetContentRaw::xmlNodeGetContent/xmlNodeSetContent are already public libxml2 API with different semantics (allocate-and-return, copy-and-escape); these access the raw content field pointer instead.
properties:xmlAttrPtr
nsDef:xmlNsPtr
psvi:void *
line:unsigned short
extra:unsigned short
'

ns_tag=Ns ; ns_ptr=xmlNsPtr ; ns_var=ns
ns_fields='
next:xmlNsPtr
type:xmlNsType
href:const xmlChar *
prefix:const xmlChar *
_private:void *:xmlNsGetPrivate:xmlNsSetPrivate:priv
'

attr_tag=Attr ; attr_ptr=xmlAttrPtr ; attr_var=attr
attr_fields='
type:xmlElementType::none
name:const xmlChar *::none
children:xmlNodePtr
last:xmlNodePtr
parent:xmlNodePtr
next:xmlAttrPtr
prev:xmlAttrPtr
doc:xmlDocPtr
ns:xmlNsPtr
atype:xmlAttributeType
psvi:void *
'

doc_tag=Doc ; doc_ptr=xmlDocPtr ; doc_var=doc
doc_fields='
_private:void *:xmlDocGetPrivate:xmlDocSetPrivate:priv
type:xmlElementType
name:const char *::none
children:xmlNodePtr
last:xmlNodePtr
parent:xmlNodePtr
next:xmlNodePtr
prev:xmlNodePtr
psvi:void *
doc:xmlDocPtr:::self:doc is a self-reference (a document'"'"'s containing doc is itself); named to match the field, not to be confused with the accessor for other structs'"'"' doc field.
oldNs:xmlNsPtr
compression:int
standalone:int
intSubset:xmlDtdPtr
extSubset:xmlDtdPtr
version:const xmlChar *|xmlChar *
encoding:const xmlChar *|xmlChar *
ids:void *
URL:const xmlChar *|xmlChar *
charset:int
dict:xmlDictPtr
parseFlags:int
properties:int
'

dtd_tag=Dtd ; dtd_ptr=xmlDtdPtr ; dtd_var=dtd
dtd_fields='
type:xmlElementType::none
name:const xmlChar *::none
children:xmlNodePtr::none
last:xmlNodePtr::none
parent:xmlDocPtr::none
next:xmlNodePtr::none
prev:xmlNodePtr::none
doc:xmlDocPtr::none
'

structs="node ns attr doc dtd"

# ----------------------------------------------------------------------
# Second file group: structs declared in xpath.h, not tree.h (see the
# banner comment above for why these can't share the first group's
# generated header).
# ----------------------------------------------------------------------

xpath_incdir="$top_builddir/libbxml/include/libxml/gen"
xpath_srcdir="$top_builddir/libbxml/gen"

xpath_inline_h="$xpath_incdir/xmlaccessors-xpath-inline.h"
xpath_decl_h="$xpath_incdir/xmlaccessors-xpath-decl.h"
xpath_impl_c="$xpath_srcdir/xmlaccessors-xpath.c"

nodeset_tag=NodeSet ; nodeset_ptr=xmlNodeSetPtr ; nodeset_var=ns
nodeset_fields='
nodeNr:int::none
nodeMax:int::none
'
# nodeTab is an array; see emit_array_field (shared with
# gen-xsltaccessors.sh's identical pattern for templTab/varsTab).
# It gets a setter (xmlNodeSetSetNodeEntry) because
# xsltDocumentSortFunction/xsltDoSortFunction in xsltutils.c swap
# entries in place while sorting a node-set.
nodeset_array_fields='
nodeTab:xmlNodePtr::auto
'

# xmlXPathContext has ~38 real fields (excluding the four
# nb_*_unused/max_*_unused legacy placeholders, which nothing touches
# and so get no accessor at all). Only the 15 below are ever touched
# by a consumer outside libbxml itself (libslax/extensions/slaxproc/
# libbxslt); the rest are deferred until a real call site needs them,
# same "touched-only" coverage rule used for xmlNodeSet:
#
#   varHash, nb_types, max_types, types (array), funcHash, nb_axis,
#   max_axis, axis (array), user, xptr, here, origin, varLookupFunc,
#   varLookupData, funcLookupFunc, funcLookupData, tmpNsList (array),
#   tmpNsNr, userData, error, lastError (embedded xmlError, not a
#   pointer), debugNode, cache
#
# namespaces is a xmlNs** field but is always saved/restored as a
# whole pointer alongside nsNr (e.g. transform.c's scope push/pop) --
# no call site ever indexes into it, so unlike nodeTab it's a plain
# field accessor, not an array "Entry" accessor.
#
# function/functionURI are set internally by the XPath evaluator
# during a function call and only ever read by consumers (libexslt's
# extension functions, python/libxslt.c); no call site sets them, so
# they're getter-only like xmlNode's type/name fields.
xpathcontext_tag=XPathContext ; xpathcontext_ptr=xmlXPathContextPtr ; xpathcontext_var=ctxt
xpathcontext_fields='
doc:xmlDocPtr
node:xmlNodePtr
namespaces:xmlNs **
nsNr:int
contextSize:int
proximityPosition:int
nsHash:xmlHashTablePtr
extra:void *
function:const xmlChar *::none
functionURI:const xmlChar *::none
dict:xmlDictPtr
flags:int
opLimit:unsigned long
opCount:unsigned long
depth:int
'

xpath_structs="nodeset xpathcontext"

# ----------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------

cap_first () {
    awk '{ print toupper(substr($0,1,1)) substr($0,2) }' <<< "$1"
}

write_banner () {
    local file=$1 ; local guard=$2 ; local blurb=$3 ; local extra=$4

    cat > "$file" <<EOF
/*
 * This file is generated automatically by bin/gen-xmlaccessors.sh;
 * do not edit. Edit the field table in that script instead.
 */

/**
 * @file
 *
 * @brief $blurb
 */

#ifndef $guard
#define $guard
EOF

    if [[ -n $extra ]]; then
        printf '\n%s\n' "$extra" >> "$file"
    fi

    cat >> "$file" <<EOF

#ifdef __cplusplus
extern "C" {
#endif
EOF
}

write_trailer () {
    local file=$1

    cat >> "$file" <<EOF

#ifdef __cplusplus
}
#endif

#endif /* $2 */
EOF
}

section_header () {
    local tag=$1

    cat >> "$inline_h" <<EOF

/* ----------------------------------------------------------------------
 * xml$tag accessors
 */
EOF
    cat >> "$decl_h" <<EOF

/* ----------------------------------------------------------------------
 * xml$tag accessors
 */
EOF
    cat >> "$impl_c" <<EOF

/* ----------------------------------------------------------------------
 * xml$tag accessors
 */
EOF
}

emit_field () {
    local tag=$1 ptr=$2 var=$3
    local field=$4 ctype=$5 getter=$6 setter=$7 param=$8 comment=$9

    local gtype=$ctype stype=$ctype
    if [[ $ctype == *"|"* ]]; then
        gtype=${ctype%%|*}
        stype=${ctype#*|}
    fi

    local fieldcap ; fieldcap=$(cap_first "$field")

    [[ -z $getter ]] && getter="xml${tag}Get${fieldcap}"

    local have_setter=1
    if [[ $setter == "none" ]]; then
        have_setter=0
    elif [[ -z $setter ]]; then
        setter="xml${tag}Set${fieldcap}"
    fi

    [[ -z $param ]] && param="$field"

    # No space between a pointer star and the parameter name, matching
    # this codebase's existing style ("xmlChar *content", not "xmlChar * content").
    local stype_decl="$stype $param"
    [[ $stype == *"*" ]] && stype_decl="${stype}${param}"

    local comment_block=""
    if [[ -n $comment ]]; then
        comment_block="/*
 * $comment
 */
"
    fi

    # -- inline header --
    {
        printf '\n%s' "$comment_block"
        cat <<EOF
static inline $gtype
$getter (const $ptr $var)
{
    return ${var}->${field};
}
EOF
        if [[ $have_setter -eq 1 ]]; then
            cat <<EOF

static inline void
$setter ($ptr $var, $stype_decl)
{
    ${var}->${field} = $param;
}
EOF
        fi
    } >> "$inline_h"

    # -- decl header --
    {
        printf '\n%s' "$comment_block"
        printf 'XMLPUBFUN %s %s (const %s %s);\n' \
            "$gtype" "$getter" "$ptr" "$var"
        if [[ $have_setter -eq 1 ]]; then
            printf 'XMLPUBFUN void %s (%s %s, %s);\n' \
                "$setter" "$ptr" "$var" "$stype_decl"
        fi
    } >> "$decl_h"

    # -- impl .c --
    {
        printf '\n%s' "$comment_block"
        cat <<EOF
PSU_ALWAYS_INLINE
$gtype
$getter (const $ptr $var)
{
    return ${var}->${field};
}
EOF
        if [[ $have_setter -eq 1 ]]; then
            cat <<EOF

PSU_ALWAYS_INLINE
void
$setter ($ptr $var, $stype_decl)
{
    ${var}->${field} = $param;
}
EOF
        fi
    } >> "$impl_c"
}


# Emits a xml<Tag>Get<Entry>(ptr, index) accessor for an array field,
# instead of a getter that hands back the raw array pointer -- that
# would leave every call site indexing it directly, a hidden dependency
# on the field being contiguous storage. The entry name is the field's
# capitalized name with a trailing "Tab" swapped for "Entry"
# (nodeTab -> NodeEntry), or plain "Entry" appended otherwise.
# Read-only by default (empty setter column): no setter is emitted
# unless a real call site writes through an index. To opt in, put a
# setter name in the table row -- "auto" derives the default name
# (xml<Tag>Set<Entry>), or give an explicit name. Same pattern as
# gen-xsltaccessors.sh's identical helper.
emit_array_field () {
    local tag=$1 ptr=$2 var=$3
    local field=$4 elemtype=$5 getter=$6 setter=$7 param=$8 comment=$9

    local fieldcap ; fieldcap=$(cap_first "$field")
    local entryname=$fieldcap
    [[ $entryname == *Tab ]] && entryname=${entryname%Tab}
    entryname="${entryname}Entry"

    [[ -z $getter ]] && getter="xml${tag}Get${entryname}"

    local have_setter=0
    if [[ -n $setter ]]; then
        have_setter=1
        [[ $setter == "auto" ]] && setter="xml${tag}Set${entryname}"
    fi

    [[ -z $param ]] && param="index"
    local value="value"

    local comment_block=""
    if [[ -n $comment ]]; then
        comment_block="/*
 * $comment
 */
"
    fi

    # -- inline header --
    {
        printf '\n%s' "$comment_block"
        cat <<EOF
static inline $elemtype
$getter (const $ptr $var, int $param)
{
    return ${var}->${field}[$param];
}
EOF
        if [[ $have_setter -eq 1 ]]; then
            cat <<EOF

static inline void
$setter ($ptr $var, int $param, $elemtype $value)
{
    ${var}->${field}[$param] = $value;
}
EOF
        fi
    } >> "$inline_h"

    # -- decl header --
    {
        printf '\n%s' "$comment_block"
        printf 'XMLPUBFUN %s %s (const %s %s, int %s);\n' \
            "$elemtype" "$getter" "$ptr" "$var" "$param"
        if [[ $have_setter -eq 1 ]]; then
            printf 'XMLPUBFUN void %s (%s %s, int %s, %s %s);\n' \
                "$setter" "$ptr" "$var" "$param" "$elemtype" "$value"
        fi
    } >> "$decl_h"

    # -- impl .c --
    {
        printf '\n%s' "$comment_block"
        cat <<EOF
PSU_ALWAYS_INLINE
$elemtype
$getter (const $ptr $var, int $param)
{
    return ${var}->${field}[$param];
}
EOF
        if [[ $have_setter -eq 1 ]]; then
            cat <<EOF

PSU_ALWAYS_INLINE
void
$setter ($ptr $var, int $param, $elemtype $value)
{
    ${var}->${field}[$param] = $value;
}
EOF
        fi
    } >> "$impl_c"
}

process_struct () {
    local name=$1
    local tag=${name}_tag ; tag=${!tag}
    local ptr=${name}_ptr ; ptr=${!ptr}
    local var=${name}_var ; var=${!var}
    local fields=${name}_fields ; fields=${!fields}

    section_header "$tag"

    while IFS=: read -r field ctype getter setter param comment; do
        [[ -z $field ]] && continue
        emit_field "$tag" "$ptr" "$var" "$field" "$ctype" "$getter" "$setter" "$param" "$comment"
    done <<< "$fields"

    local arrayfields=${name}_array_fields
    if [[ -n ${!arrayfields:-} ]]; then
        while IFS=: read -r field elemtype getter setter param comment; do
            [[ -z $field ]] && continue
            emit_array_field "$tag" "$ptr" "$var" "$field" "$elemtype" "$getter" "$setter" "$param" "$comment"
        done <<< "${!arrayfields}"
    fi
}

# ----------------------------------------------------------------------
# Drive it
# ----------------------------------------------------------------------

dict_extra='/*
 * xmlDocGetDict/xmlDocSetDict need xmlDictPtr. tree.h'"'"'s real
 * struct-definition branch (entered via XML_TREE_INTERNALS from
 * parser.h) includes this header before parser.h gets to its own
 * `#include <libxml/dict.h>`, so xmlDictPtr isn'"'"'t visible yet unless
 * pulled in here explicitly.
 */
#include <libxml/dict.h>'

write_banner "$inline_h" __XML_ACCESSORS_INLINE_H__ \
    "Inline accessor functions for libxml2 tree structs (generated)." \
    "$dict_extra"
write_banner "$decl_h" __XML_ACCESSORS_DECL_H__ \
    "Non-inline accessor declarations for libxml2 tree structs (generated)." \
    "$dict_extra"

cat > "$impl_c" <<EOF
/*
 * This file is generated automatically by bin/gen-xmlaccessors.sh;
 * do not edit. Edit the field table in that script instead.
 */

/**
 * @file
 *
 * @brief Non-inline accessor definitions for libxml2 tree structs (generated).
 */

/*
 * This TU always provides the real (non-inline) definitions, no
 * matter which flavor the rest of the library is built with, so it
 * needs tree.h to declare rather than inline them here -- otherwise
 * these definitions collide with tree.h's static inline ones.
 */
#ifndef LIBXML_ACCESSORS_NOINLINE
#define LIBXML_ACCESSORS_NOINLINE
#endif
#include "libxml.h"
#include <libpsu/psulto.h>
#include <libxml/tree.h>
EOF

for s in $structs; do
    process_struct "$s"
done

write_trailer "$inline_h" __XML_ACCESSORS_INLINE_H__
write_trailer "$decl_h" __XML_ACCESSORS_DECL_H__

# ----------------------------------------------------------------------
# Drive the second (xpath.h) file group. emit_field/emit_array_field/
# section_header/write_banner/write_trailer all read/write the global
# inline_h/decl_h/impl_c variables directly, so retarget them to the
# xpath-group paths for this pass.
# ----------------------------------------------------------------------

tree_inline_h=$inline_h
tree_decl_h=$decl_h
tree_impl_c=$impl_c

inline_h=$xpath_inline_h
decl_h=$xpath_decl_h
impl_c=$xpath_impl_c

write_banner "$inline_h" __XML_ACCESSORS_XPATH_INLINE_H__ \
    "Inline accessor functions for libxml2 xpath.h structs (generated)."
write_banner "$decl_h" __XML_ACCESSORS_XPATH_DECL_H__ \
    "Non-inline accessor declarations for libxml2 xpath.h structs (generated)."

cat > "$impl_c" <<EOF
/*
 * This file is generated automatically by bin/gen-xmlaccessors.sh;
 * do not edit. Edit the field table in that script instead.
 */

/**
 * @file
 *
 * @brief Non-inline accessor definitions for libxml2 xpath.h structs (generated).
 */

/*
 * This TU always provides the real (non-inline) definitions, no
 * matter which flavor the rest of the library is built with, so it
 * needs xpath.h to declare rather than inline them here -- otherwise
 * these definitions collide with xpath.h's static inline ones.
 */
#ifndef LIBXML_ACCESSORS_NOINLINE
#define LIBXML_ACCESSORS_NOINLINE
#endif
#include "libxml.h"
#include <libpsu/psulto.h>
#include <libxml/xpath.h>
EOF

for s in $xpath_structs; do
    process_struct "$s"
done

write_trailer "$inline_h" __XML_ACCESSORS_XPATH_INLINE_H__
write_trailer "$decl_h" __XML_ACCESSORS_XPATH_DECL_H__

echo "Generated:"
echo "  $tree_inline_h"
echo "  $tree_decl_h"
echo "  $tree_impl_c"
echo "  $xpath_inline_h"
echo "  $xpath_decl_h"
echo "  $xpath_impl_c"
