/*
 * Summary: the XSLT engine transformation part.
 * Description: This module implements the bulk of the actual
 *              transformation processing. Most of the xsl: element
 *              constructs are implemented in this module.
 *
 * Copy: See Copyright for the status of this software.
 *
 * Author: Daniel Veillard
 */

#ifndef __XML_XSLT_TRANSFORM_H__
#define __XML_XSLT_TRANSFORM_H__

#include <libxml/parser.h>
#include <libxml/xmlIO.h>
#include "xsltexports.h"
#include <libxslt/xsltInternals.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * XInclude default processing.
 */
XSLTPUBFUN void XSLTCALL xsltSetXIncludeDefault (int xinclude);
XSLTPUBFUN int XSLTCALL xsltGetXIncludeDefault (void);

/**
 * Export context to users.
 */
XSLTPUBFUN xsltTransformContextPtr XSLTCALL
xsltNewTransformContext (xsltStylesheetPtr style, xmlDocPtr doc);

XSLTPUBFUN void XSLTCALL
xsltFreeTransformContext (xsltTransformContextPtr ctxt);

XSLTPUBFUN xmlDocPtr XSLTCALL xsltApplyStylesheetUser (
    xsltStylesheetPtr style, xmlDocPtr doc, const char **params,
    const char *output, FILE *profile, xsltTransformContextPtr userCtxt);
XSLTPUBFUN void XSLTCALL xsltProcessOneNode (xsltTransformContextPtr ctxt,
                                             xmlNodePtr node,
                                             xsltStackElemPtr params);
/**
 * Private Interfaces.
 */
XSLTPUBFUN void XSLTCALL xsltApplyStripSpaces (xsltTransformContextPtr ctxt,
                                               xmlNodePtr node);
XSLTPUBFUN xmlDocPtr XSLTCALL xsltApplyStylesheet (xsltStylesheetPtr style,
                                                   xmlDocPtr doc,
                                                   const char **params);
XSLTPUBFUN xmlDocPtr XSLTCALL xsltProfileStylesheet (xsltStylesheetPtr style,
                                                     xmlDocPtr doc,
                                                     const char **params,
                                                     FILE *output);
XSLTPUBFUN int XSLTCALL xsltRunStylesheet (xsltStylesheetPtr style,
                                           xmlDocPtr doc, const char **params,
                                           const char *output,
                                           xmlSAXHandlerPtr SAX,
                                           xmlOutputBufferPtr IObuf);
XSLTPUBFUN int XSLTCALL xsltRunStylesheetUser (
    xsltStylesheetPtr style, xmlDocPtr doc, const char **params,
    const char *output, xmlSAXHandlerPtr SAX, xmlOutputBufferPtr IObuf,
    FILE *profile, xsltTransformContextPtr userCtxt);
XSLTPUBFUN void XSLTCALL xsltApplyOneTemplate (xsltTransformContextPtr ctxt,
                                               xmlNodePtr node, xmlNodePtr list,
                                               xsltTemplatePtr templ,
                                               xsltStackElemPtr params);
XSLTPUBFUN void XSLTCALL xsltDocumentElem (xsltTransformContextPtr ctxt,
                                           xmlNodePtr node, xmlNodePtr inst,
                                           xsltElemPreCompPtr comp);
XSLTPUBFUN void XSLTCALL xsltSort (xsltTransformContextPtr ctxt,
                                   xmlNodePtr node, xmlNodePtr inst,
                                   xsltElemPreCompPtr comp);
XSLTPUBFUN void XSLTCALL xsltCopy (xsltTransformContextPtr ctxt,
                                   xmlNodePtr node, xmlNodePtr inst,
                                   xsltElemPreCompPtr comp);
XSLTPUBFUN void XSLTCALL xsltText (xsltTransformContextPtr ctxt,
                                   xmlNodePtr node, xmlNodePtr inst,
                                   xsltElemPreCompPtr comp);
XSLTPUBFUN void XSLTCALL xsltElement (xsltTransformContextPtr ctxt,
                                      xmlNodePtr node, xmlNodePtr inst,
                                      xsltElemPreCompPtr comp);
XSLTPUBFUN void XSLTCALL xsltComment (xsltTransformContextPtr ctxt,
                                      xmlNodePtr node, xmlNodePtr inst,
                                      xsltElemPreCompPtr comp);
XSLTPUBFUN void XSLTCALL xsltAttribute (xsltTransformContextPtr ctxt,
                                        xmlNodePtr node, xmlNodePtr inst,
                                        xsltElemPreCompPtr comp);
XSLTPUBFUN void XSLTCALL
xsltProcessingInstruction (xsltTransformContextPtr ctxt, xmlNodePtr node,
                           xmlNodePtr inst, xsltElemPreCompPtr comp);
XSLTPUBFUN void XSLTCALL xsltCopyOf (xsltTransformContextPtr ctxt,
                                     xmlNodePtr node, xmlNodePtr inst,
                                     xsltElemPreCompPtr comp);
XSLTPUBFUN void XSLTCALL xsltValueOf (xsltTransformContextPtr ctxt,
                                      xmlNodePtr node, xmlNodePtr inst,
                                      xsltElemPreCompPtr comp);
XSLTPUBFUN void XSLTCALL xsltNumber (xsltTransformContextPtr ctxt,
                                     xmlNodePtr node, xmlNodePtr inst,
                                     xsltElemPreCompPtr comp);
XSLTPUBFUN void XSLTCALL xsltApplyImports (xsltTransformContextPtr ctxt,
                                           xmlNodePtr node, xmlNodePtr inst,
                                           xsltElemPreCompPtr comp);
XSLTPUBFUN void XSLTCALL xsltCallTemplate (xsltTransformContextPtr ctxt,
                                           xmlNodePtr node, xmlNodePtr inst,
                                           xsltElemPreCompPtr comp);
XSLTPUBFUN void XSLTCALL xsltApplyTemplates (xsltTransformContextPtr ctxt,
                                             xmlNodePtr node, xmlNodePtr inst,
                                             xsltElemPreCompPtr comp);
XSLTPUBFUN void XSLTCALL xsltChoose (xsltTransformContextPtr ctxt,
                                     xmlNodePtr node, xmlNodePtr inst,
                                     xsltElemPreCompPtr comp);
XSLTPUBFUN void XSLTCALL xsltIf (xsltTransformContextPtr ctxt, xmlNodePtr node,
                                 xmlNodePtr inst, xsltElemPreCompPtr comp);
XSLTPUBFUN void XSLTCALL xsltForEach (xsltTransformContextPtr ctxt,
                                      xmlNodePtr node, xmlNodePtr inst,
                                      xsltElemPreCompPtr comp);
XSLTPUBFUN void XSLTCALL xsltRegisterAllElement (xsltTransformContextPtr ctxt);

XSLTPUBFUN xmlNodePtr XSLTCALL xsltCopyTextString (xsltTransformContextPtr ctxt,
                                                   xmlNodePtr target,
                                                   const xmlChar *string,
                                                   int noescape);

/* Following 2 functions needed for libexslt/functions.c */
XSLTPUBFUN void XSLTCALL xsltLocalVariablePop (xsltTransformContextPtr ctxt,
                                               int limitNr, int level);
XSLTPUBFUN int XSLTCALL xsltLocalVariablePush (xsltTransformContextPtr ctxt,
                                               xsltStackElemPtr variable,
                                               int level);
/*
 * Hook for the debugger if activated.
 */
XSLTPUBFUN void XSLTCALL xslHandleDebugger (xmlNodePtr cur, xmlNodePtr node,
                                            xsltTemplatePtr templ,
                                            xsltTransformContextPtr ctxt);

/*
 * Hook for the debugger to stop (unconditionally, regardless of any
 * breakpoint or step/continue state) at the point where a runtime
 * error aborted the transform, so the current frame can still be
 * inspected before the transform unwinds.  If no debugger is
 * attached, this reports the error itself in compiler-style format
 * (see xsltReportFatalError() below) instead of declining outright.
 *
 * "note" is an optional, more specific detail about the failure
 * (e.g. from xsltLastXPathErrorMessage()), or NULL if none is
 * available; the debugger reports it as a note following its own
 * report.
 *
 * Returns non-zero if the error was actually reported (so the caller
 * can skip its own, less informative error message), or zero if the
 * debugger declined to report it (e.g. it's mid-eval on its own
 * behalf, as when checking a breakpoint condition) -- in which case
 * the caller's own message is still the only report this failure
 * will get.
 */
XSLTPUBFUN int XSLTCALL xslHandleDebuggerError (xmlNodePtr cur,
                                            xsltTransformContextPtr ctxt,
                                            const char *message,
                                            const char *note);

/*
 * Report a fatal runtime error in a compiler-style format:
 *
 *     file:line:col: fatal error: message
 *     line |     source text
 *          |            ^
 *     note: detail
 *
 * "cur" is the source node where the error occurred, used (together
 * with any "select"/"test" attribute on it) to find the real
 * file/line/column to report.  "note" is an optional, more specific
 * detail about the failure, printed as an indented "note: ..." line,
 * or NULL if none is available.  "script_buffer", if non-NULL, is the
 * full text of the script, searched for the line to echo instead of
 * reading one from disk -- for callers (e.g. the SLAX debugger) that
 * already have the script's text in memory and may not have a real,
 * re-readable file to read back from.
 *
 * This is the default error reporter used by xslHandleDebuggerError()
 * when no debugger is attached; most callers go through that
 * function and never need to call this directly.
 */
XSLTPUBFUN void XSLTCALL xsltReportFatalError (xmlNodePtr cur,
                                            const char *message,
                                            const char *note,
                                            const char *script_buffer);

/**
 * Print a full backtrace of "ctxt"'s real call-frame stack
 * (ctxt->frameTab), one real frame per line -- named templates and
 * pseudo-frames (xsl:if, xsl:call-template, built-in-rule descent,
 * etc) alike, with each frame's parameter/variable values underneath
 * it. This works with no debugger attached: the frame stack is
 * always-on, maintained in lockstep with ctxt->depth. The output
 * matches the SLAX debugger's own "where full" command output line
 * for line, so the two are interchangeable.
 *
 * Called from the engine's own fatal-error paths (runaway recursion,
 * too many nested variables) in place of the older xsltDebug(), which
 * only had the flat, unstructured templTab/varsTab to dump.
 */
XSLTPUBFUN void XSLTCALL xsltFormatBacktrace (xsltTransformContextPtr ctxt);

#ifdef __cplusplus
}
#endif

#endif /* __XML_XSLT_TRANSFORM_H__ */
