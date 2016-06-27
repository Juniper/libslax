/*
 * $Id$
 *
 * Copyright (c) 2016, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer (phil@) June 2016
 */

#ifndef LIBSLAX_PA_CONFIG_H
#define LIBSLAX_PA_CONFIG_H

void
pa_config_read_file (FILE *file);

void
pa_config_read (const char *filename);

const char *
pa_config_value (const char *base, const char *name);

uint32_t
pa_config_value32 (const char *base, const char *name, uint32_t def);

static inline const char *
pa_config_name (char *namebuf, size_t size, const char *name, const char *ext)
{
    snprintf(namebuf, size, "%s.%s", name, ext);
    return namebuf;
}


#endif /* LIBSLAX_PA_CONFIG_H */
