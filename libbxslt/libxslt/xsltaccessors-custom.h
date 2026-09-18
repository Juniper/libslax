/*
 * Hand-written accessors, kept separate from the generated
 * xsltaccessors-{inline,decl}.h/xsltaccessors.c (bin/gen-xsltaccessors.sh)
 * because these don't fit the generator's plain get-field/set-field
 * pattern: the value a caller wants isn't actually stored in the
 * struct field its name suggests.
 *
 * Do not add ordinary field accessors here -- those belong in the
 * generator's field table (bin/gen-xsltaccessors.sh) so they get
 * regenerated for free. Only add an accessor to this file when the
 * real storage location differs from the "obvious" struct field, so
 * a generated get/set-the-field accessor would be actively wrong.
 *
 * Copy: See Copyright for the status of this software.
 */

#ifndef __XSLT_ACCESSORS_CUSTOM_H__
#define __XSLT_ACCESSORS_CUSTOM_H__

#include <libpsu/psucommon.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------
 * xsltTransformContext.debugStatus
 *
 * There is no per-context debug status: debugging status is process-wide,
 * held in the single global "xslDebugStatus" (xsltutils.c), which is
 * what most of the debugger-aware code in transform.c actually needs to
 * agree on. xsltTransformContext used to carry its own "debugStatus"
 * field as a copy, seeded once from the global when the context was
 * created (in xsltNewTransformContext) and then never kept in sync --
 * callers that set the global via xsltSetDebuggerStatus() had no way to
 * tell whether they also needed to poke a live context's copy, and most
 * of them didn't, so a debugger-issued "run" or "reload" often left
 * older per-context reads seeing a stale mode.
 *
 * The field has been removed. These accessors exist for code outside
 * libslax/libbxslt/libbxml -- callers that only ever saw a struct field
 * or a Get/Set accessor and shouldn't need to know it went away. "ctxt"
 * is accepted (and ignored) only so those call sites don't need to
 * change; a NULL ctxt is fine here, unlike most other accessors.
 *
 * Code inside libslax/libbxslt/libbxml is internal and can just refer
 * to "xslDebugStatus" directly instead of calling through these -- see
 * e.g. transform.c, documents.c, attributes.c, variables.c, slaxext.c.
 * ---------------------------------------------------------------------- */

extern int xslDebugStatus;

static inline int
xsltTransformContextGetDebugStatus (const xsltTransformContextPtr ctxt UNUSED)
{
    return xslDebugStatus;
}

static inline void
xsltTransformContextSetDebugStatus (xsltTransformContextPtr ctxt UNUSED,
				     int debugStatus)
{
    xslDebugStatus = debugStatus;
}

#ifdef __cplusplus
}
#endif

#endif /* __XSLT_ACCESSORS_CUSTOM_H__ */
