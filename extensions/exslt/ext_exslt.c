/*
 * $Id$
 *
 * Copyright (c) 2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <glob.h>
#include <pwd.h>
#include <grp.h>
#include <dirent.h>

#include <libxml/xpathInternals.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlsave.h>
#include <libexslt/exslt.h>

#include "config.h"

#include <libxslt/extensions.h>
#include <libslax/slaxdata.h>
#include <libslax/slaxdyn.h>
#include <libslax/slaxio.h>
#include <libslax/xmlsoft.h>
#include <libslax/slaxutil.h>
#include <libslax/slaxnames.h>

static int extExsltCount;

SLAX_DYN_FUNC(slaxDynLibInit)
{
    if (extExsltCount++ == 0) {
	slaxLog("exslt: registering exslt library");
	exsltRegisterAll();
    }
	
    return SLAX_DYN_VERSION;
}

SLAX_DYN_FUNC(slaxDynLibClean)
{
    if (--extExsltCount == 0) {
	slaxLog("exslt: registering exslt library (noop)");
    }

    return SLAX_DYN_VERSION;
}
