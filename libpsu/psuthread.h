/*
 * Copyright (c) 2010-2013, 2017, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * Threading-specific constructs
 */

#ifndef LIBPSU_PSUTHREAD_H
#define LIBPSU_PSUTHREAD_H

/**
 * @define THREAD_LOCAL Decorate a type to mark the associate variable
 * declaration a "thread local storage", in portable way.  One uses this
 * as:
 *     THREAD_LOCAL(int) my_flag;
 *     static THREAD_LOCAL(int) my_other_flag;
 *     static THREAD_LOCAL(char) data[2][4];
 * These can be scoped either file or local (declared inside a nested scope).
 */

/*
 * Three styles of specifying thread-local variables are supported.
 * configure.ac has the brains to run each possibility through the
 * compiler and see what works; we are left to define the THREAD_LOCAL
 * macro to the right value.  Most toolchains (clang, gcc) use
 * "before", but some (borland) use "after" and I've heard of some
 * (ms) that use __declspec.  Any others out there?
 */
#define THREAD_LOCAL_before 1
#define THREAD_LOCAL_after 2
#define THREAD_LOCAL_declspec 3

#ifndef HAVE_THREAD_LOCAL
#define THREAD_LOCAL(_x) _x
#elif HAVE_THREAD_LOCAL == THREAD_LOCAL_before
#define THREAD_LOCAL(_x) __thread _x
#elif HAVE_THREAD_LOCAL == THREAD_LOCAL_after
#define THREAD_LOCAL(_x) _x __thread
#elif HAVE_THREAD_LOCAL == THREAD_LOCAL_declspec
#define THREAD_LOCAL(_x) __declspec(_x)
#else
#error unknown thread-local setting
#endif /* HAVE_THREADS_H */

/**
 * THREAD_GLOBAL is the opposite of THREAD_LOCAL, indicating that we
 * _want_ this variable to be global/common to all threads.  It allows
 * us to mark our variables as "I thought about this and decided it
 * should be THREAD_GLOBAL" in a bold and obvious way.
 */
#define THREAD_GLOBAL(_x) _x

#endif /* LIBPSU_PSUTHREAD_H */
