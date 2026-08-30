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
 */

typedef uint16_t pin_op_type_t;

#define PIN_OP_NONE        0   /* Sentinel / null op */
#define PIN_OP_EMIT_OPEN   1   /* Emit static open tag to output */
#define PIN_OP_EMIT_CLOSE  2   /* Emit static close tag to output */
#define PIN_OP_EMIT_ATTRIB 3   /* Push name/value pair for next EMIT_OPEN */
#define PIN_OP_EMIT        4   /* Emit (pop + output) top of value stack as text */
#define PIN_OP_PUSH_STRING 5   /* Push literal string atom → PVT_STRING */
#define PIN_OP_PUSH_ATTR   6   /* Push attribute value from context node → PVT_STRING */
#define PIN_OP_PUSH_TEXT   7   /* Push text content of context or named child → PVT_STRING */
#define PIN_OP_PUSH_NODES  8   /* Push all children matching po_name → PVT_NODESET */
#define PIN_OP_PUSH_BOOL   9   /* Convert top of stack to PVT_BOOLEAN and push */
#define PIN_OP_PUSH_NODE  10   /* Push context node itself → PVT_NODE */
#define PIN_OP_CONVERT    11   /* Convert top of stack to type encoded in po_name */
#define PIN_OP_IF         12   /* Pop PVT_BOOLEAN; if false jump to po_alt */
#define PIN_OP_GOTO       13   /* Unconditional jump to po_alt */
#define PIN_OP_JUMP       14   /* Join point — falls through po_next */
#define PIN_OP_APPLY      15   /* Apply-templates to retained children (po_name = mode) */
#define PIN_OP_CALL       16   /* Invoke named template (future) */
#define PIN_OP_RETURN     17   /* Return from named template (future) */
#define PIN_OP_DISCARD    18   /* Pop and discard top of value stack */
#define PIN_OP_STORE_VAR  19   /* Pop top; bind to variable named po_name */
#define PIN_OP_LOAD_VAR   20   /* Push value of variable named po_name */
#define PIN_OP_MAX        21   /* Sentinel: number of defined op codes */

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

typedef pin_value_t (*pin_op_func_t)(pin_exec_state_t *esp,
                                     struct pin_parse_s *parsep,
                                     pin_op_t *opp);

typedef struct pin_op_def_s {
    const char    *pod_name;    /* Human-readable mnemonic for psu_log() / dump */
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
