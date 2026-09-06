/*
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer, August 2026
 *
 * Op-dispatch execution engine for libpin template bodies.
 *
 * Each xsl:template body is compiled into a linked list of pin_op_t nodes
 * stored in the rulebook's prb_ops pool.  When the matched element closes,
 * pin_exec_run() executes the op sequence against the retained subtree.
 *
 * The engine maintains three stacks in pin_exec_state_t:
 *   pes_rb[]    — rulebook stack (active rulebook per input depth)
 *   pes_val[]   — value stack (pin_value_t operands)
 *   pes_seq[]   — op-sequence stack (pc + context node per frame)
 *
 * Temporary nodesets (select results, variable values) live in pes_nodesets[],
 * a heap-allocated growable table indexed by pin_value_t.pv_atom when
 * pv_type == PVT_NODESET.  Variable bindings use the same infrastructure.
 *
 * Include order: pin_exec.h may be included before pin_rules.h.
 * pin_rulebook_t is forward-declared here; pin_op_id_funcs_gen.h (which
 * references pin_rulebook_t) is included at the bottom of pin_rules.h.
 */

#ifndef LIBSLAX_PIN_EXEC_H
#define LIBSLAX_PIN_EXEC_H

#include <stdio.h>
#include <stdint.h>
#include <parrotdb/pacommon.h>
#include <libpin/pin_value.h>
#include <libpin/pin_node.h>

/* pin_rstate_id_t — needed for pes_rb[]; does not pull in pin_rules.h */
#include "gen/pin_rstate_id_gen.h"

/* Generated typed atom for op ids (wraps pa_fixed_atom_t) */
#include "gen/pin_op_id_gen.h"

/* Forward declarations; definitions are in pin_rules.h and pin_parse.h */
struct pin_rulebook_s;
struct pin_parse_s;

/* Forward typedef so pin_op_func_t can use pin_exec_state_t * */
typedef struct pin_exec_state_s pin_exec_state_t;

/*
 * Operation types
 *
 * Complex ops (types 1 .. PIN_OP_MAX_COMPLEX-1) require BIA_ fallback when
 * present in a compiled sequence.  Their type numbers are also bit indices in
 * pin_op_cursor_t.poc_complexity: bit N set means op-type N is present.
 *
 * Non-complex ops start at PIN_OP_MAX_COMPLEX.  They never set bits in
 * poc_complexity, so the full 64-bit mask can encode up to 63 distinct
 * complex-op types (bit 0 is PIN_OP_NONE, which is never emitted).
 */

typedef uint16_t pin_op_type_t;

#define PIN_OP_NONE         0   /* Sentinel / null op — never emitted */

/* --- complex ops: bit (1 << type) set in poc_complexity when present --- */
#define PIN_OP_APPLY        1   /* Apply-templates to retained children (po_name = mode) */
#define PIN_OP_PUSH_NODE    2   /* Push context node itself → PVT_NODE */
#define PIN_OP_COMPLEX_EXPR 3   /* Stub: unsupported test expression, always false */
/* 4-7: reserved for future complex ops */

#define PIN_OP_MAX_COMPLEX  8   /* First non-complex op number */

/* --- non-complex ops: numbered from PIN_OP_MAX_COMPLEX --- */
#define PIN_OP_EMIT_OPEN   (PIN_OP_MAX_COMPLEX +  0) /* Emit static open tag to output */
#define PIN_OP_EMIT_CLOSE  (PIN_OP_MAX_COMPLEX +  1) /* Emit static close tag to output */
#define PIN_OP_EMIT_ATTRIB (PIN_OP_MAX_COMPLEX +  2) /* Push name/value pair for next EMIT_OPEN */
#define PIN_OP_EMIT        (PIN_OP_MAX_COMPLEX +  3) /* Emit (pop + output) top of value stack */
#define PIN_OP_PUSH_STRING (PIN_OP_MAX_COMPLEX +  4) /* Push literal string atom → PVT_STRING */
#define PIN_OP_PUSH_ATTR   (PIN_OP_MAX_COMPLEX +  5) /* Push attribute value → PVT_STRING */
#define PIN_OP_PUSH_TEXT   (PIN_OP_MAX_COMPLEX +  6) /* Push text content → PVT_STRING */
#define PIN_OP_PUSH_NODES  (PIN_OP_MAX_COMPLEX +  7) /* Push matching children → PVT_NODESET */
#define PIN_OP_PUSH_BOOL   (PIN_OP_MAX_COMPLEX +  8) /* Convert top of stack to PVT_BOOLEAN */
#define PIN_OP_CONVERT     (PIN_OP_MAX_COMPLEX +  9) /* Convert top to type in po_name */
#define PIN_OP_IF          (PIN_OP_MAX_COMPLEX + 10) /* Pop PVT_BOOLEAN; if false jump to po_alt */
#define PIN_OP_GOTO        (PIN_OP_MAX_COMPLEX + 11) /* Unconditional jump to po_alt */
#define PIN_OP_JUMP        (PIN_OP_MAX_COMPLEX + 12) /* Join point — falls through po_next */
#define PIN_OP_CALL        (PIN_OP_MAX_COMPLEX + 13) /* Invoke named template (future) */
#define PIN_OP_RETURN      (PIN_OP_MAX_COMPLEX + 14) /* Return from named template (future) */
#define PIN_OP_DISCARD     (PIN_OP_MAX_COMPLEX + 15) /* Pop and discard top of value stack */
#define PIN_OP_STORE_VAR   (PIN_OP_MAX_COMPLEX + 16) /* Pop top; bind to variable named po_name */
#define PIN_OP_LOAD_VAR    (PIN_OP_MAX_COMPLEX + 17) /* Push value of variable named po_name */
#define PIN_OP_FOR_EACH    (PIN_OP_MAX_COMPLEX + 18) /* Iterate matching children (po_name=tag, po_alt=body, po_name2=sort-spec) */
#define PIN_OP_MAX         (PIN_OP_MAX_COMPLEX + 19) /* Sentinel: number of defined op codes */

/*
 * Compiled op node (stored in prb_ops pa_fixed pool)
 */

typedef struct pin_op_s {
    pin_op_id_t    po_next;      /* Next op in sequence; null = end of list */
    pin_op_id_t    po_alt;       /* Branch target: IF false / GOTO / join */
    pin_op_type_t  po_type;      /* Opcode — index into pin_op_table[] */
    pin_name_id_t  po_name;      /* Primary operand: tag / attr / string atom */
    pin_name_id_t  po_name2;     /* Secondary operand (e.g. attribute value) */
    pin_name_id_t  po_src_file;  /* Namepool atom: source XSLT filename */
    uint32_t       po_src_line;  /* Source line number */
} pin_op_t;

/*
 * Op dispatch table
 */

typedef uint32_t pin_op_flags_t;

#define PIN_OP_FUNC_ARGS \
        pin_exec_state_t *esp UNUSED, struct pin_parse_s *parsep UNUSED, \
	pin_op_t *opp UNUSED

typedef pin_value_t (*pin_op_func_t)(PIN_OP_FUNC_ARGS);

typedef struct pin_op_def_s {
    const char    *pod_name;   /* Human-readable mnemonic for psu_log()/dump */
    pin_op_func_t  pod_func;   /* Dispatch function */
    pin_op_flags_t pod_flags;  /* POF_* */
} pin_op_def_t;

extern pin_op_def_t pin_op_table[PIN_OP_MAX];

/*
 * Execution state (heap-allocated, one per parse session)
 */

/*
 * One frame on the rulebook stack.
 * Pushed when the parser descends into a child element that switches state.
 */
typedef struct pin_exec_rb_frame_s {
    pin_rstate_id_t prf_sid;    /* State to restore on pop */
    pin_depth_t     prf_depth;  /* Input depth at which this state was pushed */
} pin_exec_rb_frame_t;

/*
 * One frame on the op-sequence stack.
 * psf_context is the retained tree node for the matched element
 * (the node against which PUSH_ATTR, PUSH_TEXT, PUSH_NODES operate).
 */
typedef struct pin_exec_seq_frame_s {
    pin_op_id_t   psf_pc;        /* Next op to execute */
    pin_node_id_t psf_context;   /* Retained matched element node */
} pin_exec_seq_frame_t;

typedef struct pin_exec_state_s {
    /* Rulebook stack (growable; pes_rb may be reallocated) */
    pin_exec_rb_frame_t *pes_rb;
    int                  pes_rb_top;
    int                  pes_rb_cap;

    /* Value stack (growable; pes_val may be reallocated) */
    pin_value_t *pes_val;
    int          pes_val_top;
    int          pes_val_cap;

    /* Op-sequence stack (growable; pes_seq may be reallocated) */
    pin_exec_seq_frame_t *pes_seq;
    int                   pes_seq_top;
    int                   pes_seq_cap;

    /* Nodeset table: indexed by PVT_NODESET pv_atom */
    pin_ns_entry_t *pes_nodesets;
    uint32_t        pes_nodeset_count;
    uint32_t        pes_nodeset_cap;

    /* Variable bindings (xsl:variable / mutable variables) */
    pin_var_binding_t *pes_vars;
    uint32_t           pes_var_count;
    uint32_t           pes_var_cap;
} pin_exec_state_t;

/*
 * Execute the op sequence starting at 'start' against 'context_node'.
 * Returns 0 on success, -1 on error.
 */
int
pin_exec_run (pin_exec_state_t *esp, struct pin_parse_s *parsep,
	      pin_op_id_t start, pin_node_id_t context_node);

/*
 * Dump all rulebooks, rules, and op sequences in human-readable form.
 * Format matches the examples in build/pin-plan.md.
 */
void
pin_exec_dump (struct pin_rulebook_s *rbp, struct pin_parse_s *parsep, FILE *out);

/* Rulebook stack helpers */
int pin_exec_rb_push (pin_exec_state_t *esp, pin_rstate_id_t sid,
		      pin_depth_t depth);
void pin_exec_rb_pop (pin_exec_state_t *esp);

/* Seq-frame stack helpers */
int pin_exec_push_seq_frame (pin_exec_state_t *esp, pin_op_id_t pc,
			     pin_node_id_t ctx);

/* Value stack helpers */
int pin_exec_push (pin_exec_state_t *esp, pin_value_t val);
pin_value_t pin_exec_pop (pin_exec_state_t *esp);
pin_value_t pin_exec_peek (pin_exec_state_t *esp);

/* Nodeset table helpers */
uint32_t pin_exec_nodeset_alloc (pin_exec_state_t *esp);
void pin_exec_nodeset_free (pin_exec_state_t *esp, uint32_t idx);
int pin_exec_nodeset_append (pin_exec_state_t *esp, uint32_t idx,
			     uint32_t node_atom);

/* pin_op_id_funcs_gen.h (alloc/free/addr into prb_ops) is included
 * at the bottom of pin_rules.h, after pin_rulebook_t is defined.    */

#endif /* LIBSLAX_PIN_EXEC_H */
