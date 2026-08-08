#!/bin/sh
#
# This script generates header files that can generates C code that
# will allow C to discriminate between all the "atom numbers" that
# parrotdb used in a way that won't drive developers crazy.
#
# We make a wrapper for atoms.  We use wrappers like these to help the
# compiler enforce type safety and keep us sane.  Otherwise too many
# uint32_t-based types will happily be treated identically.
#

atom_generic="
paXXX_gen.h \
pa_XXX_atom_t \
pa_XXX_atom_s \
pa_atom \
pa_XXX_is_null \
pa_XXX_atom \
pa_XXX_atom_of \
pa_XXX_null_atom \
"

# Generates alloc/free/addr inline functions for typed-atom-keyed fixed arrays.
# Parameters: atom_type, elem_type, base_type, field, alloc_fn, free_fn,
#             addr_fn, build_fn, atom_of_fn, is_null_fn.
FIXED_FUNCTIONS_CODE='
static inline $elem_type *
$alloc_fn ($base_type *basep, $atom_type *atomp)
{
    if (atomp == NULL)
	return NULL;
    pa_fixed_atom_t atom = pa_fixed_alloc_atom(basep->$field);
    $elem_type *datap = pa_fixed_atom_addr(basep->$field, atom);
    *atomp = $build_fn(pa_fixed_atom_of(atom));
    return datap;
}

static inline void
$free_fn ($base_type *basep, $atom_type atom)
{
    if ($is_null_fn(atom))
	return;
    pa_fixed_free_atom(basep->$field, $atom_of_fn(atom));
}

static inline $elem_type *
$addr_fn ($base_type *basep, $atom_type atom)
{
    return ($elem_type *) pa_fixed_atom_addr(basep->$field, $atom_of_fn(atom));
}

static inline $elem_type *
$elem_fn ($base_type *basep, $atom_type atom)
{
    return ($elem_type *) pa_fixed_element(basep->$field,
					   pa_fixed_atom_of($atom_of_fn(atom)));
}
'

# Generates alloc/free/addr inline functions for pa_atom_t-keyed fixed arrays.
# Parameters: elem_type, base_type, field, alloc_fn, free_fn, addr_fn.
PLAIN_FUNCTIONS_CODE='
static inline $elem_type *
$alloc_fn ($base_type *basep, pa_atom_t *atomp)
{
    if (atomp == NULL)
	return NULL;
    pa_fixed_atom_t _fa = pa_fixed_alloc_atom(basep->$field);
    $elem_type *_datap = pa_fixed_atom_addr(basep->$field, _fa);
    *atomp = pa_fixed_atom_of(_fa);
    return _datap;
}

static inline void
$free_fn ($base_type *basep, pa_atom_t _atom)
{
    if (_atom == PA_NULL_ATOM) return;
    pa_fixed_free_atom(basep->$field, pa_fixed_atom(_atom));
}

static inline $elem_type *
$addr_fn ($base_type *basep, pa_atom_t _atom)
{
    if (_atom == PA_NULL_ATOM) return NULL;
    return ($elem_type *) pa_fixed_atom_addr(basep->$field, pa_fixed_atom(_atom));
}
'

# Like atom_generic but wraps pa_fixed_atom_t (for PA_FIXED_ATOM_TYPE style).
# Used for libxi typed atoms that index into pa_fixed_t arrays.
FIXED_ATOM_CODE='
typedef struct $atom_struct {
    pa_fixed_atom_t $field;		/* Fixed atom number */
} $atom_type;

static inline psu_boolean_t
$is_null_fn ($atom_type atom)
{
    return pa_fixed_is_null(atom.$field);
}

static inline $atom_type
$build_fn (pa_atom_t atom)
{
    return ($atom_type){ pa_fixed_atom(atom) };
}

static inline pa_fixed_atom_t
$atom_of_fn ($atom_type atom)
{
    return atom.$field;
}

static inline $atom_type
$null_atom_fn (void)
{
    return $build_fn(PA_NULL_ATOM);
}
'

base_atoms="\
arb \
bitmap \
fixed \
istr \
istr_data \
mmap \
pat \
pat_data \
"

fixed_atoms="\
one \
two \
"

field_arb=pra_atom
field_bitmap=pba_atom
field_fixed=pfa_atom
field_istr=pia_atom
field_istr_data=pida_atom
field_mmap=pma_atom
field_pat=ppa_atom
field_pat_data=ppa_data_atom

# libxi fixed-atom typed wrappers: file, type, struct, field, is_null, build, atom_of, null_atom
atom_xi_rule_id="
xi_rule_id_gen.h \
xi_rule_id_t \
xi_rule_id_s \
xrid_atom \
xi_rule_id_is_null \
xi_rule_id \
xi_rule_id_atom_of \
xi_rule_id_null_atom \
"

atom_xi_rstate_id="
xi_rstate_id_gen.h \
xi_rstate_id_t \
xi_rstate_id_s \
xrsid_atom \
xi_rstate_id_is_null \
xi_rstate_id \
xi_rstate_id_atom_of \
xi_rstate_id_null_atom \
"

atom_xi_node_id="
xi_node_id_gen.h \
xi_node_id_t \
xi_node_id_s \
xnid_atom \
xi_node_id_is_null \
xi_node_id \
xi_node_id_atom_of \
xi_node_id_null_atom \
"

atom_xi_ns_map_id="
xi_ns_map_id_gen.h \
xi_ns_map_id_t \
xi_ns_map_id_s \
xnsid_atom \
xi_ns_map_id_is_null \
xi_ns_map_id \
xi_ns_map_id_atom_of \
xi_ns_map_id_null_atom \
"

xi_fixed_atoms="\
xi_rule_id \
xi_rstate_id \
xi_node_id \
xi_ns_map_id \
"

# libxi typed-atom function sets: file, atom_type, elem_type, base_type, field,
#   alloc_fn, free_fn, addr_fn, build_fn, atom_of_fn, is_null_fn
fixed_funcs_xi_rule="
xi_rule_id_funcs_gen.h \
xi_rule_id_t \
xi_rule_t \
xi_rulebook_t \
xrb_rules \
xi_rule_alloc \
xi_rule_free \
xi_rule_addr \
xi_rule_id \
xi_rule_id_atom_of \
xi_rule_id_is_null \
"

fixed_funcs_xi_rstate="
xi_rstate_id_funcs_gen.h \
xi_rstate_id_t \
xi_rstate_t \
xi_rulebook_t \
xrb_states \
xi_rstate_alloc \
xi_rstate_free \
xi_rstate_addr \
xi_rstate_id \
xi_rstate_id_atom_of \
xi_rstate_id_is_null \
"

fixed_funcs_xi_node="
xi_node_gen.h \
xi_node_id_t \
xi_node_t \
xi_workspace_t \
xw_nodes \
xi_node_alloc \
xi_node_free \
xi_node_addr \
xi_node_id \
xi_node_id_atom_of \
xi_node_id_is_null \
"

fixed_funcs_xi_ns_map="
xi_ns_map_gen.h \
xi_ns_map_id_t \
xi_ns_map_t \
xi_workspace_t \
xw_ns_map \
xi_ns_map_alloc \
xi_ns_map_free \
xi_ns_map_addr \
xi_ns_map_id \
xi_ns_map_id_atom_of \
xi_ns_map_id_is_null \
"

xi_fixed_func_atoms="\
xi_rule \
xi_rstate \
xi_node \
xi_ns_map \
"

# libxi plain-atom function sets: file, elem_type, base_type, field, alloc_fn, free_fn, addr_fn
plain_xi_nodeset_chunk="
xi_nodeset_chunk_gen.h \
xi_nodeset_chunk_t \
xi_nodeset_t \
xns_workspace->xw_nodeset_chunks \
xi_nodeset_chunk_alloc \
xi_nodeset_chunk_free \
xi_nodeset_chunk_addr \
"

plain_xi_nodeset_info="
xi_nodeset_info_gen.h \
xi_nodeset_info_t \
xi_workspace_t \
xw_nodeset_info \
xi_nodeset_info_alloc \
xi_nodeset_info_free \
xi_nodeset_info_addr \
"

xi_plain_atoms="\
xi_nodeset_chunk \
xi_nodeset_info \
"

ATOM_CODE='
typedef struct $atom_struct {
    pa_atom_t $field;		/* Atom number */
} $atom_type;

static inline psu_boolean_t
$is_null_fn ($atom_type atom)
{
    return (atom.$field == PA_NULL_ATOM);
}

static inline $atom_type
$build_fn (pa_atom_t atom)
{
    return ($atom_type){ atom };
}

static inline pa_atom_t
$atom_of_fn ($atom_type atom)
{
    return atom.$field;
}

static inline $atom_type
$null_atom_fn (void)
{
    return $build_fn(PA_NULL_ATOM);
}
'
    
write_header () {
    local file=$1 ; shift
    local tag=`echo $file | sed -e 's:[/\\.]:_:g' | tr '[a-z]' '[A-Z]'`

    cat > $file <<EOF
/*
 * This file is generated automatically; do not edit
 */

#ifndef $tag
#define $tag

EOF
}

open_files=()

add_open_file () {
    local file=$1 ; shift

    open_files=( ${open_files[*]} $file )
}

write_to_file () {
    local file=$1 ; shift
    local dir=`dirname $file`

    if [ ! -z "$dir" ]; then
	mkdir -p $dir
    fi

    match=0
    for i in ${open_files[*]}; do
	if [ "$i" = "$file" ]; then
	    match=0
	fi
    done

    if [ "$match" -eq 0 ]; then
	write_header $file
    fi

    cat >> $file
}

make_one_atom() {
    local name=$1 ; shift
    local file=$1 ; shift
    local atom_type=$1 ; shift
    local atom_struct=$1 ; shift
    local field=$1 ; shift
    local is_null_fn=$1 ; shift
    local build_fn=$1 ; shift
    local atom_of_fn=$1 ; shift
    local null_atom_fn=$1 ; shift

    local rfield=`eval echo '\$field_'$name`
    if [ ! -z "$rfield" ]; then
	field=$rfield
    fi

    echo "Generating header for $name ( gen/$file ) ..."

    echo "${ATOM_CODE}" | sed \
        -e "s:\$atom_type:$atom_type:g" \
        -e "s:\$atom_struct:$atom_struct:g" \
        -e "s:\$field:$field:g" \
        -e "s:\$is_null_fn:$is_null_fn:g" \
        -e "s:\$build_fn:$build_fn:g" \
        -e "s:\$atom_of_fn:$atom_of_fn:g" \
        -e "s:\$null_atom_fn:$null_atom_fn:g" \
	| write_to_file gen/$file

    add_open_file gen/$file
}

write_trailers () {
    for f in ${open_files[*]}; do
	cat >> $f <<EOF
#endif /* EOF */
EOF
    done
}

make_one_named_atom() {
    local name=$1 ; shift

    args=`eval echo '\$atom_'$name`
    if [ -z "$args" ]; then
	args=`echo $atom_generic | sed -e "s:XXX:$name:g"`
    fi

    make_one_atom $name $args
}

make_one_fixed_atom() {
    local name=$1 ; shift
    local file=$1 ; shift
    local atom_type=$1 ; shift
    local atom_struct=$1 ; shift
    local field=$1 ; shift
    local is_null_fn=$1 ; shift
    local build_fn=$1 ; shift
    local atom_of_fn=$1 ; shift
    local null_atom_fn=$1 ; shift

    echo "Generating fixed header for $name ( gen/$file ) ..."

    echo "${FIXED_ATOM_CODE}" | sed \
        -e "s:\$atom_type:$atom_type:g" \
        -e "s:\$atom_struct:$atom_struct:g" \
        -e "s:\$field:$field:g" \
        -e "s:\$is_null_fn:$is_null_fn:g" \
        -e "s:\$build_fn:$build_fn:g" \
        -e "s:\$atom_of_fn:$atom_of_fn:g" \
        -e "s:\$null_atom_fn:$null_atom_fn:g" \
	| write_to_file gen/$file

    add_open_file gen/$file
}

make_one_fixed_named_atom() {
    local name=$1 ; shift

    args=`eval echo '\$atom_'$name`
    make_one_fixed_atom $name $args
}

make_one_fixed_funcs_atom() {
    local name=$1 ; shift
    local file=$1 ; shift
    local atom_type=$1 ; shift
    local elem_type=$1 ; shift
    local base_type=$1 ; shift
    local field=$1 ; shift
    local alloc_fn=$1 ; shift
    local free_fn=$1 ; shift
    local addr_fn=$1 ; shift
    local build_fn=$1 ; shift
    local atom_of_fn=$1 ; shift
    local is_null_fn=$1 ; shift

    local elem_fn="${addr_fn%addr}element"

    echo "Generating fixed functions for $name ( gen/$file ) ..."

    echo "${FIXED_FUNCTIONS_CODE}" | sed \
        -e "s:\$atom_type:$atom_type:g" \
        -e "s:\$elem_type:$elem_type:g" \
        -e "s:\$base_type:$base_type:g" \
        -e "s:\$field:$field:g" \
        -e "s:\$alloc_fn:$alloc_fn:g" \
        -e "s:\$free_fn:$free_fn:g" \
        -e "s:\$addr_fn:$addr_fn:g" \
        -e "s:\$elem_fn:$elem_fn:g" \
        -e "s:\$build_fn:$build_fn:g" \
        -e "s:\$atom_of_fn:$atom_of_fn:g" \
        -e "s:\$is_null_fn:$is_null_fn:g" \
        | write_to_file gen/$file

    add_open_file gen/$file
}

make_one_fixed_funcs_named_atom() {
    local name=$1 ; shift

    args=`eval echo '\$fixed_funcs_'$name`
    make_one_fixed_funcs_atom $name $args
}

make_one_plain_atom() {
    local name=$1 ; shift
    local file=$1 ; shift
    local elem_type=$1 ; shift
    local base_type=$1 ; shift
    local field=$1 ; shift
    local alloc_fn=$1 ; shift
    local free_fn=$1 ; shift
    local addr_fn=$1 ; shift

    echo "Generating plain functions for $name ( gen/$file ) ..."

    echo "${PLAIN_FUNCTIONS_CODE}" | sed \
        -e "s:\$elem_type:$elem_type:g" \
        -e "s:\$base_type:$base_type:g" \
        -e "s:\$field:$field:g" \
        -e "s:\$alloc_fn:$alloc_fn:g" \
        -e "s:\$free_fn:$free_fn:g" \
        -e "s:\$addr_fn:$addr_fn:g" \
        | write_to_file gen/$file

    add_open_file gen/$file
}

make_one_plain_named_atom() {
    local name=$1 ; shift

    args=`eval echo '\$plain_'$name`
    make_one_plain_atom $name $args
}

do_work() {
    local name
    for name in $base_atoms; do
	make_one_named_atom $name
    done

    write_trailers
}

do_one() {
    make_one_named_atom $1
}

do_xi() {
    local name
    for name in $xi_fixed_atoms; do
	make_one_fixed_named_atom $name
    done
    for name in $xi_fixed_func_atoms; do
	make_one_fixed_funcs_named_atom $name
    done
    for name in $xi_plain_atoms; do
	make_one_plain_named_atom $name
    done

    write_trailers
}

job=$1 ; shift
if [ -z "$job" ]; then
    job=work
fi

do_$job $*

exit 0
