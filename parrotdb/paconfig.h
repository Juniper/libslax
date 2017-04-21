/*
 * Copyright (c) 2016-2017, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Phil Shafer (phil@) June 2016
 *
 * Handle simple configuration values read from simple configuration
 * files.  Configuration values use the syntax "name=value", with the
 * names having a hierarchical syntax (parent.child.name=value).
 */

#ifndef PARROTDB_PACONFIG_H
#define PARROTDB_PACONFIG_H

/**
 * Load config values from the given file
 *
 * @param[in] file File pointer holding config values to be read
 */
void
pa_config_read_file (FILE *file);

/**
 * Load config values from the given file
 *
 * @param[in] filename File of config values to be read
 */
void
pa_config_read (const char *filename);

/**
 * Return a value from config
 *
 * @param[in] base basename/prefix of the config name
 * @param[in] name name of this table
 * @return value to be used
 */
const char *
pa_config_value (const char *base, const char *name);

/**
 * Return a value from config, or the default if nothing is configured.
 *
 * @param[in] base basename/prefix of the config name
 * @param[in] name name of this table
 * @param[in] def default value
 * @return value to be used
 */
uint32_t
pa_config_value32 (const char *base, const char *name, uint32_t def);

/**
 * Return a value from config, but only if the default is zero or
 * if the config value is greater than the default.  In any case
 * the returned value is never less than the default.
 *
 * @param[in] base basename/prefix of the config value
 * @param[in] name name of this table
 * @param[in] def default value
 * @return value to be used
 */
static inline uint32_t
pa_config_value32_min (const char *base, const char *name, uint32_t def)
{
    uint32_t val = pa_config_value32(base, name, def);
    return (def == 0 || val > def) ? val : def;
}

/**
 * Build the name of a configuration variable
 *
 * @param[in] namebuf Buffer to fill in with the name
 * @param[in] size Size of the buffer
 * @param[in] base Base of the variable name
 * @param[in] name Extension part of the name
 * @return Variable name, located in the namebuf parameter
 */
static inline const char *
pa_config_name (char *namebuf, size_t size, const char *base, const char *name)
{
    snprintf(namebuf, size, "%s.%s", base, name);
    return namebuf;
}

#endif /* PARROTDB_PACONFIG_H */
