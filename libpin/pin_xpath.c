/*
 * Copyright (c) 2016-2017, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer (phil@) August 2016
 */

typedef struct pin_xpath_prep_s {
    pin_xpath_t *xxp_xpath;	/* Current XPath */
    pin_parse_t *xxp_script;	/* Parsed script "workspace" */
    pa_atom_t xxp_atom_name;
    pa_atom_t xxp_atom_type;
} pin_xpath_prep_t;

pa_atom_t
pin_xpath_prep (pin_parse_t *input, const char *name)
{
    pin_xpath_prep_t prep;

    bzero(&prep, sizeof(prep));
    prep.xxp_atom_name = pin_parse_namepool_atom(input, "name");
    prep.xxp_atom_type = pin_parse_namepool_atom(input, "type");

    
    
}
