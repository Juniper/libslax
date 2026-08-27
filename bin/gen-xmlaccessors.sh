#!/usr/bin/env bash
#
# Generates the xmlNode/xmlNs/xmlAttr/xmlDoc/xmlDtd accessor functions
# from a single table of field descriptions, in three flavors:
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
# All three files are generated files, so they live under the build
# tree, not the source tree: the source tree (other than this script)
# stays read-only. The headers land at
# $(top_builddir)/libbxml/include/libxml/gen/xmlaccessors-{inline,decl}.h,
# reached as <libxml/gen/xmlaccessors-inline.h> -- already resolvable
# tree-wide via configure.ac's existing LIBXML_CFLAGS, which already
# has -I$(top_builddir)/libbxml/include. The .c file lands alongside
# at $(top_builddir)/libbxml/gen/xmlaccessors.c.
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
# Helpers
# ----------------------------------------------------------------------

cap_first () {
    awk '{ print toupper(substr($0,1,1)) substr($0,2) }' <<< "$1"
}

write_banner () {
    local file=$1 ; local guard=$2 ; local blurb=$3

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

/*
 * xmlDocGetDict/xmlDocSetDict need xmlDictPtr. tree.h's real
 * struct-definition branch (entered via XML_TREE_INTERNALS from
 * parser.h) includes this header before parser.h gets to its own
 * \`#include <libxml/dict.h>\`, so xmlDictPtr isn't visible yet unless
 * pulled in here explicitly.
 */
#include <libxml/dict.h>

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
}

# ----------------------------------------------------------------------
# Drive it
# ----------------------------------------------------------------------

write_banner "$inline_h" __XML_ACCESSORS_INLINE_H__ \
    "Inline accessor functions for libxml2 tree structs (generated)."
write_banner "$decl_h" __XML_ACCESSORS_DECL_H__ \
    "Non-inline accessor declarations for libxml2 tree structs (generated)."

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

echo "Generated:"
echo "  $inline_h"
echo "  $decl_h"
echo "  $impl_c"
