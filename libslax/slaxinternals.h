/*
 * Copyright (c) 2006-2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#ifndef SLAX_SLAXINTERNALS_H
#define SLAX_SLAXINTERNALS_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/param.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/xmlsave.h>

#include <libxslt/documents.h>

#include "slaxconfig.h"
#include "slaxversion.h"

#include <libpsu/psucommon.h>
#include <libpsu/psualloc.h>
#include <libpsu/psulog.h>

#include "xmlsoft.h"
#include "slaxdyn.h"

/* Forward declarations for common structures */
struct slax_string_s; typedef struct slax_string_s slax_string_t;
struct slax_data_s; typedef struct slax_data_s slax_data_t;

#include "slaxnames.h"
#include "slaxstring.h"
#include "slaxlexer.h"
#include "slaxtree.h"
#include "slaxloader.h"
#include "slaxio.h"
#include "slaxprofiler.h"

extern int slaxYyDebug;

static inline const char *
slaxIntoString (const xmlChar *xp)
{
    return (const char *) xp;
}

/*
 * The rest of the .c files expose so little we don't bother with
 * distinct header files.
 */

/* --- slaxext.h --- */

/**
 * Emit an error using a parser context
 */
void
slaxTransformError (xmlXPathParserContextPtr ctxt, const char *fmt, ...);

/*
 * Emit an error using a transform context
 */
void
slaxTransformError2 (xsltTransformContextPtr tctxt, const char *fmt, ...);

/* --- slaxmvar.h --- */

#define SLAX_MVAR_PREFIX "slax-"

void slaxMvarAddSvarName (slax_data_t *sdp, xmlNodePtr nodep);

/**
 * Called from the parser grammar to rewrite a variable definition into
 * a mutable variable (mvar) and its shadow variable (svar).  The shadow
 * variable is used as a "history" mechanism, keeping previous values
 * of the mvar so references to those contents don't dangle when the
 * mvar value changes.
 *
 * @sdp: main slax parsing data structure
 * @varname: variable name (mvar)
 */
void
slaxMvarCreateSvar (slax_data_t *sdp, const char *varname);

void
slaxMvarRegister (void);

xsltStackElemPtr
slaxMvarLookupQname (xsltTransformContextPtr tctxt, const xmlChar *svarname,
		     int *localp);

/* --- slaxwriter.h --- */

struct slax_writer_s;
typedef struct slax_writer_s slax_writer_t;

slax_writer_t *
slaxGetWriter (slaxWriterFunc_t func, void *data);

void
slaxFreeWriter (slax_writer_t *swp);

void
slaxWriteSetFlags (slax_writer_t *swp, slaxWriterFlags_t flags);

void
slaxWriteSetVersion (slax_writer_t *swp, const char *version);

void
slaxWriteSetWidth (slax_writer_t *swp, int width);

void
slaxWrite (slax_writer_t *swp, const char *fmt, ...);

int
slaxWriteDocument (slax_writer_t *swp, xmlDocPtr docp);

void
slaxWriteIndent (slax_writer_t *swp, int change);

int
slaxWriteNewline (slax_writer_t *swp, int change);

#define NEWL_INDENT 1
#define NEWL_OUTDENT -1

/**
 * See if we need to rewrite a function call into something else.
 * The only current case is "..."/slax:build-sequnce.
 */
int
slaxWriteRedoFunction(slax_data_t *, const char *, slax_string_t *);

slax_string_t *
slaxWriteRedoTernary (slax_data_t *sdp, slax_string_t *);

slax_string_t *
slaxWriteRedoConcat (slax_data_t *sdp, slax_string_t *f);

slax_string_t *
slaxWriteRemoveParens (slax_data_t *sdp, slax_string_t *);

char *
slaxConvertExpression (const char *opt_expression, int is_slax);

/* --- slaxloader.h -- */
void
slaxDataCleanup (slax_data_t *sdp);

#endif /* SLAX_SLAXINTERNALS_H */
