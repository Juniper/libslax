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

#ifndef LIBPSU_PSUBASE64_H
#define LIBPSU_PSUBASE64_H

#include <string.h>

/**
 * Encode data using base64 encoding.  Allocates a buffer and returns
 * it, after filling it with the base64-encoded data.  The returned
 * data is always NUL terminated, but that NUL is not counted in the
 * length of the string.
 *
 * @param[in] buf Input buffer
 * @param[in] blen Number of bytes available in destination buffer
 * @param[out] olenp Pointer to number of bytes used to encode data
 * @return Newly allocated buffer
 */
char *
psu_base64_encode (const char *buf, size_t blen, size_t *olenp);

/**
 * Decode data using base64 encoding.  Allocates a buffer and returns
 * it, after filling it with the unencoded data.  The returned data is
 * always NUL terminated, but that NUL is not counted in the length of
 * the string.
 *
 * @param[in] buf Input buffer
 * @param[in] blen Number of bytes available in destination buffer
 * @param[out] olenp Pointer to number of bytes used to encode data
 * @return Newly allocated buffer
 */
char *
psu_base64_decode (const char *buf, size_t blen, size_t *olenp);

#endif /* LIBPSU_PSUBASE64_H */
