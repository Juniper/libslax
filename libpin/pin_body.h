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
    pin_body_instr_type_t bi_type;
    pin_name_id_t bi_tag;           /* BIA_EMIT_OPEN / BIA_EMIT_CLOSE: tag name atom */
    pin_name_id_t bi_text;          /* BIA_EMIT_TEXT: text content atom */
    pin_name_id_t bi_select;        /* BIA_COPY_SELECT / BIA_APPLY: select= atom */
    pin_name_id_t bi_mode;          /* BIA_APPLY: mode= atom */
} pin_body_instr_t;

/*
 * Body execution modes (for pin_body_frame_t.pbf_mode).
 */
typedef uint8_t pin_body_mode_t;

#define PBMODE_NONE  0   /* Not executing a body */
#define PBMODE_EXEC  1   /* Running EMIT_* instructions; no input consumed */
#define PBMODE_COPY  2   /* Consuming input subtree via BIA_COPY */
#define PBMODE_APPLY 3   /* Dispatching children through the rulebook (BIA_APPLY) */

/*
 * One frame on the body execution stack.
 * A new frame is pushed each time a rule with pr_body fires.
 */
#define PIN_BODY_DEPTH_MAX 16

typedef struct pin_body_frame_s {
    pin_body_instr_id_t pbf_pc;         /* Next instruction to run on resume */
    pin_body_mode_t pbf_mode;           /* Current body execution mode */
    pin_name_id_t pbf_match_name;       /* Name of the matched input element */
    const char *pbf_match_prefix;       /* Namespace prefix of matched element */
    char *pbf_match_attribs;            /* Attribute string from matched element open */
    pin_depth_t pbf_copy_depth;         /* Output depth of matched element during BIA_COPY */
} pin_body_frame_t;

/*
 * Body execution state embedded in pin_insert_t.
 * pbe_depth is the number of active body frames (0 = idle).
 */
typedef struct pin_body_exec_s {
    pin_body_frame_t pbe_stack[PIN_BODY_DEPTH_MAX];
    int pbe_depth;
} pin_body_exec_t;

#endif /* LIBSLAX_PIN_BODY_H */
