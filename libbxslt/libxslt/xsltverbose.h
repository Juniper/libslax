/*
 * Summary: internal-use-only helper for conditional verbose tracing
 * Description: replaces the scattered
 *                  #ifdef WITH_XSLT_VERBOSE_XXX
 *                  xsltGenericDebug(xsltGenericDebugContext, ...);
 *                  #endif
 *              pattern with a single xsltVerbose(XSLT_VB_XXX, ...) call,
 *              so the source isn't broken up by preprocessor conditionals.
 *              Each XSLT_VB_XXX flag is 0 or 1 depending on whether the
 *              matching WITH_XSLT_VERBOSE_XXX component macro is defined;
 *              since none of the per-subsystem WITH_XSLT_VERBOSE_XXX
 *              macros are ever defined by the build (only the general
 *              WITH_XSLT_VERBOSE is configure-controlled), the optimizer
 *              folds away every xsltVerbose(XSLT_VB_XXX, ...) call whose
 *              flag is a constant 0, same as the #ifdef it replaces.
 *
 * This header is not installed; it is for use within libbxslt's own
 * sources only.
 *
 * Copy: See Copyright for the status of this software.
 */

#ifndef __XML_XSLTVERBOSE_H__
#define __XML_XSLTVERBOSE_H__

#include <stdarg.h>
#include <libpsu/psucommon.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WITH_XSLT_VERBOSE
#define XSLT_VB_GENERAL 1
#else
#define XSLT_VB_GENERAL 0
#endif

#ifdef WITH_XSLT_VERBOSE_ATTRIBUTES
#define XSLT_VB_ATTRIBUTES 1
#else
#define XSLT_VB_ATTRIBUTES 0
#endif

#ifdef WITH_XSLT_VERBOSE_AVT
#define XSLT_VB_AVT 1
#else
#define XSLT_VB_AVT 0
#endif

#ifdef WITH_XSLT_VERBOSE_BLANKS
#define XSLT_VB_BLANKS 1
#else
#define XSLT_VB_BLANKS 0
#endif

#ifdef WITH_XSLT_VERBOSE_DOCUMENTS
#define XSLT_VB_DOCUMENTS 1
#else
#define XSLT_VB_DOCUMENTS 0
#endif

#ifdef WITH_XSLT_VERBOSE_EXTENSIONS
#define XSLT_VB_EXTENSIONS 1
#else
#define XSLT_VB_EXTENSIONS 0
#endif

#ifdef WITH_XSLT_VERBOSE_EXTRA
#define XSLT_VB_EXTRA 1
#else
#define XSLT_VB_EXTRA 0
#endif

#ifdef WITH_XSLT_VERBOSE_FUNCTION
#define XSLT_VB_FUNCTION 1
#else
#define XSLT_VB_FUNCTION 0
#endif

#ifdef WITH_XSLT_VERBOSE_KEYS
#define XSLT_VB_KEYS 1
#else
#define XSLT_VB_KEYS 0
#endif

#ifdef WITH_XSLT_VERBOSE_PARSING
#define XSLT_VB_PARSING 1
#else
#define XSLT_VB_PARSING 0
#endif

#ifdef WITH_XSLT_VERBOSE_PATTERN
#define XSLT_VB_PATTERN 1
#else
#define XSLT_VB_PATTERN 0
#endif

#ifdef WITH_XSLT_VERBOSE_PREPROC
#define XSLT_VB_PREPROC 1
#else
#define XSLT_VB_PREPROC 0
#endif

#ifdef WITH_XSLT_VERBOSE_PROCESS
#define XSLT_VB_PROCESS 1
#else
#define XSLT_VB_PROCESS 0
#endif

#ifdef WITH_XSLT_VERBOSE_TEMPLATES
#define XSLT_VB_TEMPLATES 1
#else
#define XSLT_VB_TEMPLATES 0
#endif

#ifdef WITH_XSLT_VERBOSE_VARIABLE
#define XSLT_VB_VARIABLE 1
#else
#define XSLT_VB_VARIABLE 0
#endif

/*
 * va_list counterpart to xsltGenericDebug/xsltGenericDebugDefaultFunc
 * (xsltutils.c), used by xsltVerbose() below.  Not part of the public
 * API; not listed in libxslt.syms.
 */
extern void xsltGenericDebugV(void *ctx, const char *fmt, va_list ap)
    PSU_PRINTFLIKE(2, 0);

/**
 * xsltVerbose:
 * @enable: one of the XSLT_VB_XXX flags above, or TRUE/FALSE directly
 * @ctx: debug context, normally xsltGenericDebugContext
 * @fmt: printf-style format string
 *
 * Replacement for "#ifdef WITH_XSLT_VERBOSE_XXX / xsltGenericDebug(...)
 * / #endif".  When @enable is a compile-time constant 0 (the normal
 * case for every XSLT_VB_XXX flag, since none of the subsystem-specific
 * WITH_XSLT_VERBOSE_XXX macros are ever defined), the optimizer removes
 * this call and its arguments entirely, just as the #ifdef it replaces
 * would have.
 */
static inline void PSU_PRINTFLIKE(3, 4)
    xsltVerbose(int enable, void *ctx, const char *fmt, ...)
{
    if (!enable)
        return;

    va_list vap;
    va_start(vap, fmt);
    xsltGenericDebugV(ctx, fmt, vap);
    va_end(vap);
}

#ifdef __cplusplus
}
#endif

#endif /* __XML_XSLTVERBOSE_H__ */
