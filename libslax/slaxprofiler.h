/*
 * $Id$
 *
 * Copyright (c) 2010-2011, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

/**
 * Called from the debugger when we want to profile a script.
 *
 * @docp document pointer for the script
 * @returns TRUE is there was a problem
 */
int
slaxProfOpen (xmlDocPtr docp);

/**
 * Called when we enter a instruction.
 *
 * @docp document pointer
 * @inst instruction (slax/xslt code) pointer
 */
void
slaxProfEnter (xmlNodePtr inst);

/**
 * Called when we exit an instruction
 */
void
slaxProfExit (void);

#if 0
typedef int (*slaxProfCallback_t)(void *, const char *fmt, ...);
#endif

/**
 * Report the results
 */
void
slaxProfReport (int);

/**
 * Clear all values
 */
void
slaxProfClear (void);

/**
 * Done (free resources)
 */
void
slaxProfClose (void);
