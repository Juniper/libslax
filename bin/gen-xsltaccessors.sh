#!/usr/bin/env bash
#
# Generates accessor functions for libxslt's own internal structs
# (xsltStylesheet, xsltTransformContext, xsltTemplate, xsltStackElem,
# xsltStylePreComp, xsltDocument, xsltKeyDef, xsltKeyTable,
# xsltDecimalFormat) from a single table of field descriptions, in
# three flavors:
#
#   - xsltaccessors-inline.h : static inline functions (today's behavior)
#   - xsltaccessors-decl.h   : extern declarations for a non-inline build
#   - xsltaccessors.c        : the matching non-inline definitions
#
# This is the same generator pattern as bin/gen-xmlaccessors.sh (see
# that script for the full rationale); see build/acc-xslt.md for the
# plan this implements, including which fields got getters only versus
# getters+setters.
#
# All three files are generated files, so they live under the build
# tree, not the source tree. The headers land at
# $(top_builddir)/libbxslt/libxslt/gen/xsltaccessors-{inline,decl}.h,
# reached as <libxslt/gen/xsltaccessors-inline.h> via the existing
# LIBXSLT_CFLAGS -I$(top_builddir)/libbxslt. The .c file lands
# alongside at $(top_builddir)/libbxslt/gen/xsltaccessors.c.
#
# Table format: see bin/gen-xmlaccessors.sh's header comment for the
# full field-table syntax ("field:ctype:getter:setter:param:comment").
# setter "none" means the field is getter-only.
#
# Four structs (xsltDocument, xsltDecimalFormat, xsltKeyDef,
# xsltKeyTable) are treated as full-coverage public-API structs: every
# field gets both a getter and a setter, regardless of whether current
# call sites mutate it (per the libxslt devhelp Documents/Keys pages
# documenting them as public). The other structs only get a setter
# where a real write site exists.
#
# xsltStylesheet's "errors" field additionally gets an Increment
# accessor (xsltStylesheetIncrementErrors) that just adds one -- see
# emit_increment below.
#
# Run with no arguments to regenerate all three files using the default
# (in-tree) build directory, or pass a top_srcdir/top_builddir pair the
# way libbxslt/libxslt/Makefile.am's generation rule does:
#
#   gen-xsltaccessors.sh $(top_srcdir) $(top_builddir)

set -e

top_srcdir=${1:-$(cd "$(dirname "$0")/.." && pwd)}
top_builddir=${2:-$top_srcdir/build}

incdir="$top_builddir/libbxslt/libxslt/gen"
srcdir="$top_builddir/libbxslt/gen"

inline_h="$incdir/xsltaccessors-inline.h"
decl_h="$incdir/xsltaccessors-decl.h"
impl_c="$srcdir/xsltaccessors.c"

mkdir -p "$incdir" "$srcdir"

# ----------------------------------------------------------------------
# Field tables: struct-tag ptr-type var-name, followed by its rows.
# ----------------------------------------------------------------------

stylesheet_tag=Stylesheet ; stylesheet_ptr=xsltStylesheetPtr ; stylesheet_var=style
stylesheet_fields='
next:xsltStylesheetPtr::none
imports:xsltStylesheetPtr::none
doc:xmlDocPtr::none
templates:xsltTemplatePtr::none
indent:int
errors:int
'
# errors also gets an Increment accessor -- see emit_increment below.
stylesheet_increment_field=errors
stylesheet_increment_ctype=int

transformcontext_tag=TransformContext ; transformcontext_ptr=xsltTransformContextPtr ; transformcontext_var=ctxt
transformcontext_fields='
style:xsltStylesheetPtr::none
inst:xmlNodePtr::none
templ:xsltTemplatePtr::none
templNr:int::none
vars:xsltStackElemPtr::none
varsNr:int::none
varsBase:int::none
insert:xmlNodePtr
output:xmlDocPtr::none
xpathCtxt:xmlXPathContextPtr::none
debugStatus:int
state:xsltTransformState
globalVars:xmlHashTablePtr::none
dict:xmlDictPtr::none
maxTemplateDepth:int
'
# templTab/varsTab are arrays; rather than a getter that hands back the
# raw array (leaving callers to index it -- a hidden dependency on the
# array being contiguous storage), these get a per-element accessor
# that takes the index itself. See emit_array_field below.
transformcontext_array_fields='
templTab:xsltTemplatePtr
varsTab:xsltStackElemPtr
'

template_tag=Template ; template_ptr=xsltTemplatePtr ; template_var=templ
template_fields='
next:xsltTemplatePtr::none
match:xmlChar *::none
name:const xmlChar *::none
mode:const xmlChar *::none
elem:xmlNodePtr::none
'

stackelem_tag=StackElem ; stackelem_ptr=xsltStackElemPtr ; stackelem_var=elem
stackelem_fields='
next:xsltStackElemPtr::none
comp:xsltStylePreCompPtr::none
computed:int
name:const xmlChar *::none
nameURI:const xmlChar *::none
select:const xmlChar *::none
tree:xmlNodePtr::none
value:xmlXPathObjectPtr
fragment:xmlDocPtr::none
level:int::none
context:xsltTransformContextPtr::none
flags:int::none
'

stylePreComp_tag=StylePreComp ; stylePreComp_ptr=xsltStylePreCompPtr ; stylePreComp_var=comp
stylePreComp_fields='
type:xsltStyleType::none
'

document_tag=Document ; document_ptr=xsltDocumentPtr ; document_var=xdoc
document_fields='
next:xsltDocumentPtr
main:int
doc:xmlDocPtr
keys:void *
includes:xsltDocumentPtr
preproc:int
nbKeysComputed:int
'

keyDef_tag=KeyDef ; keyDef_ptr=xsltKeyDefPtr ; keyDef_var=keyd
keyDef_fields='
next:xsltKeyDefPtr
inst:xmlNodePtr
name:xmlChar *
nameURI:xmlChar *
match:xmlChar *
use:xmlChar *
comp:xmlXPathCompExprPtr
usecomp:xmlXPathCompExprPtr
nsList:xmlNsPtr *
nsNr:int
'

keyTable_tag=KeyTable ; keyTable_ptr=xsltKeyTablePtr ; keyTable_var=keyt
keyTable_fields='
next:xsltKeyTablePtr
name:xmlChar *
nameURI:xmlChar *
keys:xmlHashTablePtr
'

decimalFormat_tag=DecimalFormat ; decimalFormat_ptr=xsltDecimalFormatPtr ; decimalFormat_var=format
decimalFormat_fields='
next:xsltDecimalFormatPtr
nsUri:const xmlChar *
name:xmlChar *
digit:xmlChar *
patternSeparator:xmlChar *
minusSign:xmlChar *
infinity:xmlChar *
noNumber:xmlChar *
decimalPoint:xmlChar *
grouping:xmlChar *
percent:xmlChar *
permille:xmlChar *
zeroDigit:xmlChar *
'

structs="stylesheet transformcontext template stackelem stylePreComp document keyDef keyTable decimalFormat"

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
 * This file is generated automatically by bin/gen-xsltaccessors.sh;
 * do not edit. Edit the field table in that script instead.
 */

/**
 * @file
 *
 * @brief $blurb
 */

#ifndef $guard
#define $guard

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
 * xslt$tag accessors
 */
EOF
    cat >> "$decl_h" <<EOF

/* ----------------------------------------------------------------------
 * xslt$tag accessors
 */
EOF
    cat >> "$impl_c" <<EOF

/* ----------------------------------------------------------------------
 * xslt$tag accessors
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

    [[ -z $getter ]] && getter="xslt${tag}Get${fieldcap}"

    local have_setter=1
    if [[ $setter == "none" ]]; then
        have_setter=0
    elif [[ -z $setter ]]; then
        setter="xslt${tag}Set${fieldcap}"
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
        printf 'XSLTPUBFUN %s %s (const %s %s);\n' \
            "$gtype" "$getter" "$ptr" "$var"
        if [[ $have_setter -eq 1 ]]; then
            printf 'XSLTPUBFUN void %s (%s %s, %s);\n' \
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

# Emits a xslt<Tag>Increment<Field>(ptr) accessor that just adds one to
# an integer field -- used for xsltStylesheet's "errors" counter, which
# call sites bump one at a time rather than set to an absolute value.
emit_increment () {
    local tag=$1 ptr=$2 var=$3 field=$4 ctype=$5
    local fieldcap ; fieldcap=$(cap_first "$field")
    local incname="xslt${tag}Increment${fieldcap}"

    {
        cat <<EOF

static inline void
$incname ($ptr $var)
{
    ${var}->${field}++;
}
EOF
    } >> "$inline_h"

    printf '\nXSLTPUBFUN void %s (%s %s);\n' "$incname" "$ptr" "$var" >> "$decl_h"

    {
        cat <<EOF

PSU_ALWAYS_INLINE
void
$incname ($ptr $var)
{
    ${var}->${field}++;
}
EOF
    } >> "$impl_c"
}


# Emits a xslt<Tag>Get<Entry>(ptr, index) accessor for an array field,
# instead of a getter that hands back the raw array pointer -- that
# would leave every call site indexing it directly, a hidden dependency
# on the field being contiguous storage rather than, say, a later
# switch to a growable vector type. The entry name is the field's
# capitalized name with a trailing "Tab" swapped for "Entry"
# (templTab -> TemplEntry), or plain "Entry" appended otherwise.
# Read-only: no in-scope call site writes through an array index, so no
# setter is emitted (add one here if that ever changes).
emit_array_field () {
    local tag=$1 ptr=$2 var=$3
    local field=$4 elemtype=$5 getter=$6 param=$7 comment=$8

    local fieldcap ; fieldcap=$(cap_first "$field")
    local entryname=$fieldcap
    [[ $entryname == *Tab ]] && entryname=${entryname%Tab}
    entryname="${entryname}Entry"

    [[ -z $getter ]] && getter="xslt${tag}Get${entryname}"
    [[ -z $param ]] && param="index"

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
    } >> "$inline_h"

    # -- decl header --
    {
        printf '\n%s' "$comment_block"
        printf 'XSLTPUBFUN %s %s (const %s %s, int %s);\n' \
            "$elemtype" "$getter" "$ptr" "$var" "$param"
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
        while IFS=: read -r field elemtype getter param comment; do
            [[ -z $field ]] && continue
            emit_array_field "$tag" "$ptr" "$var" "$field" "$elemtype" "$getter" "$param" "$comment"
        done <<< "${!arrayfields}"
    fi

    local incfield=${name}_increment_field
    if [[ -n ${!incfield:-} ]]; then
        local incctype=${name}_increment_ctype ; incctype=${!incctype}
        emit_increment "$tag" "$ptr" "$var" "${!incfield}" "$incctype"
    fi
}

# ----------------------------------------------------------------------
# Drive it
# ----------------------------------------------------------------------

write_banner "$inline_h" __XSLT_ACCESSORS_INLINE_H__ \
    "Inline accessor functions for libxslt internal structs (generated)."
write_banner "$decl_h" __XSLT_ACCESSORS_DECL_H__ \
    "Non-inline accessor declarations for libxslt internal structs (generated)."

cat > "$impl_c" <<EOF
/*
 * This file is generated automatically by bin/gen-xsltaccessors.sh;
 * do not edit. Edit the field table in that script instead.
 */

/**
 * @file
 *
 * @brief Non-inline accessor definitions for libxslt internal structs (generated).
 */

/*
 * This TU always provides the real (non-inline) definitions, no
 * matter which flavor the rest of the library is built with, so it
 * needs xsltInternals.h to declare rather than inline them here --
 * otherwise these definitions collide with its static inline ones.
 */
#ifndef LIBXSLT_ACCESSORS_NOINLINE
#define LIBXSLT_ACCESSORS_NOINLINE
#endif
#include "libxslt.h"
#include <libpsu/psulto.h>
#include <libxslt/xsltInternals.h>
EOF

for s in $structs; do
    process_struct "$s"
done

write_trailer "$inline_h" __XSLT_ACCESSORS_INLINE_H__
write_trailer "$decl_h" __XSLT_ACCESSORS_DECL_H__

echo "Generated:"
echo "  $inline_h"
echo "  $decl_h"
echo "  $impl_c"
