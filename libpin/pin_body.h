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
 * Body instruction set for the template body FSM.
 *
 * Each xsl:template body is compiled into a linked list of pin_body_instr_t
 * nodes stored in the rulebook's prb_body_instrs pool.  At runtime the
 * body FSM in pin_parse.c executes the instruction list, pausing when it
 * needs to consume streaming input (BIA_COPY) and resuming when the
 * corresponding input subtree closes.
 */

#ifndef LIBSLAX_PIN_BODY_H
#define LIBSLAX_PIN_BODY_H

#include <parrotdb/pacommon.h>
#include <parrotdb/pafixed.h>
#include <libpin/pin_common.h>
#include <libpin/pin_node.h>
#include <libxo/xo.h>
#include <libxo/xo_buf.h>

/* Generated typed atom for body instruction ids (wraps pa_fixed_atom_t) */
#include "gen/pin_body_instr_id_gen.h"

/*
 * Body instruction type codes.
 */
typedef uint8_t pin_body_instr_type_t;

#define BIA_NONE        0   /* Sentinel / null instruction */
#define BIA_EMIT_OPEN   1   /* Emit static open tag to output */
#define BIA_EMIT_TEXT   2   /* Emit static text content to output */
#define BIA_EMIT_CLOSE  3   /* Emit static close tag to output */
#define BIA_COPY        4   /* Copy current input element (streaming or retained) */
#define BIA_COPY_SELECT 5   /* Copy named children from retained subtree */
#define BIA_APPLY       6   /* Apply-templates to current element's children */
#define BIA_VALUE_OF    7   /* Emit text content of current element (no element tags) */
#define BIA_JUMP        8   /* Unconditional fall-through join point (follows bi_next) */
#define BIA_IF          9   /* Conditional: if true, follow bi_next; if false, bi_else */
#define BIA_GOTO       10   /* Unconditional jump to bi_else (used for else-branch skip) */
#define BIA_FOR_EACH   11   /* Iterate nodes: bi_select=path, bi_text=sort-spec, bi_else=body-head */
#define BIA_IF_POSITION 12  /* position()-based test: bi_filter_idx=N, bi_tag.pnid_atom=PCMP_* */
#define BIA_VARIABLE   13   /* xsl:variable: bi_tag=var-name, bi_select=select-expr */
#define BIA_ELEMENT_OPEN  14  /* xsl:element open: bi_select=name-AVT, bi_text=attrs-AVT */
#define BIA_ELEMENT_CLOSE 15  /* close the last BIA_ELEMENT_OPEN element */
#define BIA_ATTRIB        16  /* xsl:attribute: bi_tag=name, bi_text=value-AVT */
#define BIA_COPY_OPEN     17  /* xsl:copy: emit open tag of context node */
#define BIA_MESSAGE_OPEN  18  /* xsl:message: start text capture */
#define BIA_MESSAGE_CLOSE 19  /* xsl:message: flush capture to stderr; bi_tag != null → terminate */
#define BIA_COMMENT_OPEN  20  /* xsl:comment: start text capture */
#define BIA_COMMENT_CLOSE 21  /* xsl:comment: flush capture as XML comment */
#define BIA_PI_OPEN       22  /* xsl:processing-instruction: start capture; bi_tag=target atom */
#define BIA_PI_CLOSE      23  /* xsl:processing-instruction: flush capture as XML PI */
#define BIA_NUMBER        24  /* xsl:number: bi_select=value-expr, bi_text=format */

/* Comparison operators for BIA_IF_POSITION (stored in bi_tag.pnid_atom) */
#define PCMP_EQ  0   /* position() = N */
#define PCMP_NE  1   /* position() != N */
#define PCMP_LT  2   /* position() < N */
#define PCMP_LE  3   /* position() <= N */
#define PCMP_GT  4   /* position() > N */
#define PCMP_GE  5   /* position() >= N */

/*
 * Retention requirement for the matched element.  Computed at compile time
 * from the body instruction list and stored in pin_rule_t.pr_body_retain.
 *
 * The scope ladder is: NONE → ELEMENT → SIBLINGS → DOCUMENT.
 * DISCARD means the body has no BIA_COPY/BIA_APPLY; the matched element
 * is discarded after the body's EMIT_* instructions run.
 */
typedef uint8_t pin_body_retain_t;

#define BRETAIN_DISCARD     0  /* No copy-of/apply; matched element discarded */
#define BRETAIN_NONE        1  /* Stream via single BIA_COPY select="." */
#define BRETAIN_CHILDREN    2  /* Per-child accumulators (optimization, future) */
#define BRETAIN_ELEMENT     3  /* Buffer matched element subtree (SCOPE_ELEMENT) */
#define BRETAIN_SIBLINGS    4  /* Buffer all selected siblings (SCOPE_SIBLINGS, future) */
#define BRETAIN_DOCUMENT    5  /* Full document retained (SCOPE_DOCUMENT, future) */

/*
 * A single compiled body instruction.
 * Instructions form a singly-linked list via bi_next.
 */
typedef struct pin_body_instr_s {
    pin_body_instr_id_t bi_next;    /* Next instruction; null = end of list */
    pin_body_instr_id_t bi_else;    /* BIA_IF: first instruction after the true-body */
    pin_body_instr_type_t bi_type;
    pin_name_id_t bi_tag;           /* BIA_EMIT_OPEN / BIA_EMIT_CLOSE: tag name atom */
    pin_name_id_t bi_text;          /* BIA_EMIT_TEXT: text content atom */
    pin_name_id_t bi_select;        /* BIA_EMIT_OPEN: attribute string; BIA_COPY_SELECT / BIA_APPLY / BIA_IF: select/test atom */
    pin_name_id_t bi_mode;          /* BIA_APPLY: mode= atom */
    uint32_t bi_filter_idx;         /* BIA_IF: index into prb_if_filters[] */
} pin_body_instr_t;

/*
 * Body execution modes (for pin_body_frame_t.pbf_mode).
 */
typedef uint8_t pin_body_mode_t;

#define PBMODE_NONE          0   /* Not executing a body */
#define PBMODE_EXEC          1   /* Running EMIT_* instructions; no input consumed */
#define PBMODE_COPY          2   /* Consuming input subtree via BIA_COPY */
#define PBMODE_APPLY         3   /* Dispatching children through the rulebook (BIA_APPLY) */
#define PBMODE_VALUE_OF      4   /* Collecting text content of current element (BIA_VALUE_OF) */
#define PBMODE_FOR_EACH_WAIT 5   /* Retaining matched element; defer BIA_FOR_EACH until closed */

/*
 * One frame on the body execution stack.
 * A new frame is pushed each time a rule with pr_body fires.
 */
typedef struct pin_body_frame_s {
    pin_body_instr_id_t pbf_pc;         /* Next instruction to run on resume */
    pin_body_mode_t pbf_mode;           /* Current body execution mode */
    pin_name_id_t pbf_match_name;       /* Name of the matched input element */
    const char *pbf_match_prefix;       /* Namespace prefix of matched element */
    char *pbf_match_attribs;            /* Attribute string from matched element open */
    pin_depth_t pbf_copy_depth;         /* Output depth of matched element during BIA_COPY */
    int pbf_depth_counter;              /* PBMODE_VALUE_OF: nesting depth of child elements */
    pin_name_id_t pbf_apply_mode_id;    /* PBMODE_APPLY: mode for child dispatch (null = default) */
    xo_buffer_t pbf_value_cache;        /* Cached text from select="." (non-null after first collect) */
    /* For-each iteration context (valid inside pin_body_foreach_body calls) */
    pin_node_id_t pbf_ctx_node;         /* Current for-each context node (null = none) */
    uint32_t pbf_position;              /* 1-based position in for-each (0 outside) */
    uint32_t pbf_last;                  /* Total node count for last() */
    /* xsl:variable bindings scoped to this body frame; grown as needed */
    pin_name_id_t *pbf_var_names;
    pin_name_id_t *pbf_var_values;
    int pbf_var_count;
    int pbf_var_size;
} pin_body_frame_t;

/*
 * Body execution state embedded in pin_insert_t.
 * pbe_depth is the number of active body frames (0 = idle).
 */
typedef struct pin_body_exec_s {
    pin_body_frame_t *pbe_stack;   /* heap-allocated frame stack, pbe_size entries */
    int pbe_depth;
    int pbe_size;
} pin_body_exec_t;

#endif /* LIBSLAX_PIN_BODY_H */
