/*
 * Copyright (c) 2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Some constants for LLVM/clang's "link time optimization" (LTO) feature
 *
 */

#ifndef LIBPSU_PSULTO_H
#define LIBPSU_PSULTO_H

#ifdef HAVE_GCC

#define PSU_ALWAYS_INLINE	
#define PSU_NEVER_INLINE

#else /* HAVE_GCC */

#define PSU_ALWAYS_INLINE __attribute__((always_inline))
#define PSU_NEVER_INLINE __attribute__((noinline))

#endif /* HAVE_GCC */

#endif /* LIBPSU_PSULTO_H */

