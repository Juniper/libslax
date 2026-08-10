/*
 * Copyright (c) 2016-2017, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer (phil@) August 2016
 *
 * The definitions needed to evaluate an xpath expression.  There are
 * three parts to xpath evaluation:
 *
 * - the compiled path (with prefix mappings)
 * - the context node
 * - the evaluation context (with variable settings)
 *
 * Since any xpath expression is a union (a | b | c), we want to allow
 * multiple possibilities as we descend since we _really_ don't want
 * to descend again (though sometimes we may have to).  We call these
 * possibilities "hopes".
 */

#ifndef LIBSLAX_PIN_XPATH_H
#define LIBSLAX_PIN_XPATH_H

typedef uint16_t pin_xpath_op_t;	/* Operations */
#define PIN_OP_UNKNOWN	0	/* Unknown */
#define PIN_OP_NAME	1	/* Location path step name-test */
#define PIN_OP_TYPE	2	/* Node-type test */
#define PIN_OP_PREDICATE	3	/* Predicate expression */
#define PIN_OP_OR	4	/* Logical "OR" */
#define PIN_OP_AND	5	/* Logical "AND" */
#define PIN_OP_NOT	6	/* Logical "NOT" */

#define PIN_OPERAND_MAX	2	/* Number of operands per operator */

/*
 * A piece of a compiled XPath
 */
typedef struct pin_xpath_op_s {
    pin_xpath_op_t ppo_op;	/* Operation (PIN_OP_*) */
    pa_atom_t ppo_atom[PIN_OPERAND_MAX]; /* Operands */
} pin_xpath_op_t;

/* Conventions for atom fields */
#define ppo_next ppo_atom[0]
#define ppo_child ppo_atom[1]

/*
 * A compiled XPath
 */
typedef struct pin_xpath_s {
    pa_atom_t pp_root;		/* Root of the xpath expression */
} pin_xpath_t;

/*
 * An evaluation context, which includes a set of variables.
 */
typedef struct pin_xpath_context_s {
    /* nothing yet */
} pin_xpath_context_t;

/*
 * An evaluation result
 */
typedef struct pin_xpath_result_s {
    uint16_t ppr_type;		/* Type of result */
    pa_atom_t ppr_result;	/* Resulting atom */
} pin_xpath_result_t;

/* Values for ppr_result */
#define PIN_XPR_UNKNOWN	0	/* Unknown */
#define PIN_XPR_NODESET	1	/* Creating a nodeset */
#define PIN_XPR_STRING	2	/* Building a string */
#define PIN_XPR_BOOLEAN	3	/* Boolean result */

#endif /* LIBSLAX_PIN_XPATH_H */
