/*
 * Copyright (c) 2015-2017, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Base64 encode/decode functions
 */

#include <sys/types.h>
#include <stdint.h>

#include <libpsu/psucommon.h>
#include <libpsu/psualloc.h>
#include <libpsu/psubase64.h>

static char encoder[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
    'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3',
    '4', '5', '6', '7', '8', '9', '+', '/'
};

static char decoder[256];	/* Filled in on-demand */

/**
 * Encode data using base64 encoding.  Allocates a buffer and returns
 * it, after filling it with the base64-encoded data.  The returned
 * data is always NUL terminated, but that NUL is not counted in the
 * length of the string.
 *
 * @param[in] buf Input buffer
 * @param[in] blen Number of bytes available in destination buffer
 * @param[out] olenp Pointer to number of bytes used to encode data
 * @return Newly allocated buffer containing encoded data
 */
char *
psu_base64_encode (const char *buf, size_t blen, size_t *olenp)
{
    int olen = (size_t) ((blen  + 2) / 3) * 4;
    char *data = psu_realloc(NULL, olen + 1);
    const char *cp, *ep;
    char *out;
    uint32_t bits;

    if (data == NULL)
	return NULL;

    out = data;
    cp = buf;
    ep = buf + blen;
    while (cp < ep) {
	bits = *cp++ << 16;
	bits += cp < ep ? *cp++ << 8 : 0;
	bits += cp < ep ? *cp++ : 0;

        *out++ = encoder[(bits >> 3 * 6) & 0x3F];
        *out++ = encoder[(bits >> 2 * 6) & 0x3F];
        *out++ = encoder[(bits >> 1 * 6) & 0x3F];
        *out++ = encoder[(bits >> 0 * 6) & 0x3F];
    }

    if (blen % 3 > 0) {
	out[-1] = '=';
	if (blen % 3 == 1)
	    out[-2] = '=';
    }

    *out = '\0';
    *olenp = out - data;
    return data;
}

/**
 * Decode data using base64 encoding.  Allocates a buffer and returns
 * it, after filling it with the unencoded data.  The returned data is
 * always NUL terminated, but that NUL is not counted in the length of
 * the string.
 *
 * @param[in] buf Input buffer
 * @param[in] blen Number of bytes available in destination buffer
 * @param[out] olenp Pointer to number of bytes used to encode data
 * @return Newly allocated buffer containing decoded data
 */
char *
psu_base64_decode (const char *buf, size_t blen, size_t *olenp)
{
    const char *cp, *ep;
    uint32_t bits;
    int i;
    char *out, *data, *stop;

    if (blen % 4 != 0) {
	if ((blen - 1) % 4 == 0
		&& (buf[blen - 1] == '\n' || buf[blen - 1] == '\0'))
	    blen -= 1;
	else
	    return NULL;
    }

    /* If the decoder isn't initialized, fill it with reverse mappings */
    if (decoder['A'] == 0) {
	for (i = 0, cp = decoder; i < 0x40; i++)
	    decoder[(uint) encoder[i]] = i;
    }

    int olen = (blen / 4) * 3;

    cp = buf;
    ep = buf + blen;

    /* Trailing "="+ are ignored */
    if (buf[blen - 1] == '=') {
	olen -= 1;
	ep -= 1;
    }
    if (buf[blen - 2] == '=') {
	olen -= 1;
	ep -= 1;
    }

    out = data = psu_realloc(NULL, olen + 1); /* Add 1 for NUL */
    if (data == NULL)
	return NULL;
    stop = data + olen;

    while (cp < ep) {
	if (*cp == '\n' || *cp == '\r' || *cp == ' ' || *cp == '\t') {
	    cp += 1;
	    continue;
	}

	bits = decoder[(uint) *cp++] << 18;
	bits += cp < ep ? decoder[(uint) *cp++] << 12 : 0;
	bits += cp < ep ? decoder[(uint) *cp++] << 6 : 0;
	bits += cp < ep ? decoder[(uint) *cp++] : 0;

	*out++ = (bits >> 16) & 0xFF;
	if (out < stop)
	    *out++ = (bits >> 8) & 0xFF;
	if (out < stop)
	    *out++ = bits & 0xFF;
    }

    /*
     * Our result is always NUL terminated, but that NUL is not part
     * of the length.
     */
    *out = '\0';
    *olenp = out - data;

    return data;
}
