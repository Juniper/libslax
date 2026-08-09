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

#ifndef LIBSLAX_XI_XPATH_H
#define LIBSLAX_XI_XPATH_H

typedef uint16_t xi_xpath_op_t;	/* Operations */
#define XI_OP_UNKNOWN	0	/* Unknown */
#define XI_OP_NAME	1	/* Location path step name-test */
#define XI_OP_TYPE	2	/* Node-type test */
#define XI_OP_PREDICATE	3	/* Predicate expression */
#define XI_OP_OR	4	/* Logical "OR" */
#define XI_OP_AND	5	/* Logical "AND" */
#define XI_OP_NOT	6	/* Logical "NOT" */

#define XI_OPERAND_MAX	2	/* Number of operands per operator */

/*
 * A piece of a compiled XPath
 */
typedef struct xi_xpath_op_s {
    xi_xpath_op_t xpo_op;	/* Operation (XI_OP_*) */
    pa_atom_t xpo_atom[XI_OPERAND_MAX]; /* Operands */
} xi_xpath_op_t;

/* Conventions for atom fields */
#define xpo_next xpo_atom[0]
#define xpo_child xpo_atom[1]

/*
 * A compiled XPath
 */
typedef struct xi_xpath_s {
    pa_atom_t xp_root;		/* Root of the xpath expression */
} xi_xpath_t;

/*
 * An evaluation context, which includes a set of variables.
 */
typedef struct xi_xpath_context_s {
    /* nothing yet */
} xi_xpath_context_t;

/*
 * An evaluation result
 */
typedef struct xi_xpath_result_s {
    uint16_t xpr_type;		/* Type of result */
    pa_atom_t xpr_result;	/* Resulting atom */
} xi_xpath_result_t;

/* Values for xpr_result */
#define XI_XPR_UNKNOWN	0	/* Unknown */
#define XI_XPR_NODESET	1	/* Creating a nodeset */
#define XI_XPR_STRING	2	/* Building a string */
#define XI_XPR_BOOLEAN	3	/* Boolean result */

#endif /* LIBSLAX_XI_XPATH_H */
