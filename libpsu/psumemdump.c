/*
 * Copyright (c) 2010-2013, 2017, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Logging functions for libpsu
 */

#include <stdio.h>
#include <ctype.h>

#include <libpsu/psucommon.h>
#include <libpsu/psulog.h>

/*
 * dump memory contents in hex/ascii.
0         1         2         3         4         5         6         7
0123456789012345678901234567890123456789012345678901234567890123456789012345
XX XX XX XX  XX XX XX XX - XX XX XX XX  XX XX XX XX abcdefghijklmnop
 */

/**
 * Dump a section of memory, with hex and ascii representations.
 *
 * @param[in] title Header printed above dump
 * @param[in] data Memory area to be dumped
 * @param[in] len Length in bytes of data
 * @param[in] tag Tag to be printed on each line
 * @param[in] indent Number of spaces to indent each line
 */
void
psu_mem_dump (const char *title, const char *data,
	      size_t len, const char *tag, int indent)
{
    enum { MAX_PER_LINE = 16, MAX_LINE = 80 };
    char buf[ MAX_LINE ]; /* We "know" our output fits inside 80 chars */
    char text[ MAX_LINE ];
    char *bp, *tp, *ep = buf + sizeof(buf);
    size_t i;

    if (!psu_log_is_enabled())	/* Don't waste time if logging isn't on */
	return;

    psu_log("%*s[%s] @ %p (%lx/%lu)", indent + 1, tag,
            title, data, (unsigned long) len, (unsigned long) len);

    while (len > 0) {
        bp = buf;
        tp = text;

        for (i = 0; i < MAX_PER_LINE && i < len; i++) {
            if (i && (i % 4) == 0) *bp++ = ' ';
            if (i == 8) {
                *bp++ = '-';
                *bp++ = ' ';
            }

            snprintf(bp, ep - bp, "%02x ", (unsigned char) *data);
            bp += 3;		/* "XX " */

            *tp++ = (isprint((int) *data) && *data >= ' ') ? *data : '.';
            data += 1;
        }

        *tp = 0;
        *bp = 0;
        psu_log("%*s%-54s%s", indent + 1, tag, buf, text);
        len -= i;
    }
}
