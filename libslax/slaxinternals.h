/*
 * $Id: slaxinternals.h,v 1.3 2008/05/21 02:06:12 phil Exp $
 *
 * Copyright (c) 2006-2010, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/param.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/xmlsave.h>

#include <libxslt/documents.h>

#include "config.h"
#include "slaxutil.h"
#include "slaxdyn.h"
#include "xmlsoft.h"

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

/* --- slaxwriter.h --- */

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
