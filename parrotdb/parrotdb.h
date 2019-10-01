/*
 * Copyright (c) 2016-2017, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer <phil@>, March 2017
 */

#ifndef PARROTDB_PARROTDB_H
#define PARROTDB_PARROTDB_H

/**
 * A pa_dscr_t is meant to provide some abstraction between
 * the operations on an atom and the atom itself, allowing
 * a layered set of features on top of a simple datatype.
 */
typedef struct pa_dscr_s {
    uint8_t pd_dbase;		/* Number of the database */
    uint8_t pd_type;		/* Type of the database */
    uint16_t pd_table;		/* Table number (in pa_mmap_t) */
    pa_atom_t pd_atom;		/* Atom number */
} pa_dscr_t;


struct pa_ops_s;		/* Forward declaration */

/* Typedefs for pa_ops_t functions */
typedef int (*pa_op_lock_t)(pa_mmap_t *dbase, pa_dscr_t atom, 

/**
 * The "pa_ops_t" defines the operations one may perform on
 * any base parrotdb data type.
 */
typedef struct pa_ops_s {
    const char *po_name;	/* Printable name of the type */
    
} pa_ops_t;

#endif /* PARROTDB_PARROTDB_H */

