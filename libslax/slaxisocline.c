/*
 * Copyright (c) 2025, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

/*
 * isocline is built as a single amalgamated translation unit
 * (isocline.c #include's its own sibling sources); this is its own
 * supported build style, not ours.  That vendored code doesn't meet
 * this project's warning level, so those warnings are suppressed
 * only around this one include, leaving the rest of libslax at its
 * normal warning level.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#include "isocline.c"
#pragma GCC diagnostic pop
