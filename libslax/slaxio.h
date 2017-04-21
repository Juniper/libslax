/*
 * Copyright (c) 2010-2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

/*
 * Input/output read/write/print functions
 */

/*
 * If we hit any invalid xmlChar (such as ^T), we need to encode this into
 * Unicode XML format "&#xYYYY;" in the Private area of E000-F8FF.  For
 * instance, ^T (0x14) becomes &#xE014;  This ensures we don't break XML
 * parsing and we can read these control characters back in.
 */
#ifndef SLAX_UTF8_CNTRL_BASE
#define SLAX_UTF8_CNTRL_BASE 0xe000
#define SLAX_UTF8_CNTRL_END 0xe100
#define SLAX_UTF8_CNTRL "&#x%04x;"
#define SLAX_UTF8_CNTRL_SIZE 8
#define SLAX_UTF8_CNTRL_BYTES 3
#endif /* SLAX_UTF8_CNTRL_BASE */

/**
 * Output a node
 */
void
slaxOutputNode (xmlNodePtr);

/**
 * Print the given nodeset. First we print the nodeset in a temp file.
 * Then read that file and send the the line to mgd one by one.
 */
void
slaxOutputNodeset (xmlNodeSetPtr nodeset);

#define SLAX_ERROR_DEFAULT	0 /* Default behavior */
#define SLAX_ERROR_IGNORE	1 /* Ignore errors; discard them */
#define SLAX_ERROR_RECORD	2 /* Record errors */
#define SLAX_ERROR_LOG		3 /* Log errors (slaxLog()) */

int
slaxCatchErrors (int mode, xmlNodePtr recorder);

int
slaxErrorValue (const char *name);
