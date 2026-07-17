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

job=$1 ; shift
if [ -z "$job" ]; then
    job=work
fi

do_$job $*

exit 0
