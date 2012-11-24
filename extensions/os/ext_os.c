/*
 * $Id$
 *
 * Copyright (c) 2005-2012, Juniper Networks, Inc.
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

#include <libxml/xpathInternals.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlsave.h>

#include "config.h"

#include <libxslt/extensions.h>
#include <libslax/slaxdata.h>
#include <libslax/slaxdyn.h>
#include <libslax/slaxio.h>
#include <libslax/xmlsoft.h>
#include <libslax/slaxutil.h>
#include <libslax/slaxnames.h>

#define XML_FULL_NS "http://xml.libslax.org/xml"

/*
 * Alright, this completely stinks, but there's no means of turning
 * an errno value into the EXXX value.  So we punt and make our own
 * table, which stinks.
 */
typedef struct errno_map_s {
    int err_no;
    const char *err_name;
} errno_map_t;

errno_map_t errno_map[] = {
#ifdef EPERM
    { err_no: EPERM, err_name: "EPERM" },
#endif /* EPERM */
#ifdef ENOENT
    { err_no: ENOENT, err_name: "ENOENT" },
#endif /* ENOENT */
#ifdef ESRCH
    { err_no: ESRCH, err_name: "ESRCH" },
#endif /* ESRCH */
#ifdef EINTR
    { err_no: EINTR, err_name: "EINTR" },
#endif /* EINTR */
#ifdef EIO
    { err_no: EIO, err_name: "EIO" },
#endif /* EIO */
#ifdef ENXIO
    { err_no: ENXIO, err_name: "ENXIO" },
#endif /* ENXIO */
#ifdef E2BIG
    { err_no: E2BIG, err_name: "E2BIG" },
#endif /* E2BIG */
#ifdef ENOEXEC
    { err_no: ENOEXEC, err_name: "ENOEXEC" },
#endif /* ENOEXEC */
#ifdef EBADF
    { err_no: EBADF, err_name: "EBADF" },
#endif /* EBADF */
#ifdef ECHILD
    { err_no: ECHILD, err_name: "ECHILD" },
#endif /* ECHILD */
#ifdef EDEADLK
    { err_no: EDEADLK, err_name: "EDEADLK" },
#endif /* EDEADLK */
#ifdef ENOMEM
    { err_no: ENOMEM, err_name: "ENOMEM" },
#endif /* ENOMEM */
#ifdef EACCES
    { err_no: EACCES, err_name: "EACCES" },
#endif /* EACCES */
#ifdef EFAULT
    { err_no: EFAULT, err_name: "EFAULT" },
#endif /* EFAULT */
#ifdef ENOTBLK
    { err_no: ENOTBLK, err_name: "ENOTBLK" },
#endif /* ENOTBLK */
#ifdef EBUSY
    { err_no: EBUSY, err_name: "EBUSY" },
#endif /* EBUSY */
#ifdef EEXIST
    { err_no: EEXIST, err_name: "EEXIST" },
#endif /* EEXIST */
#ifdef EXDEV
    { err_no: EXDEV, err_name: "EXDEV" },
#endif /* EXDEV */
#ifdef ENODEV
    { err_no: ENODEV, err_name: "ENODEV" },
#endif /* ENODEV */
#ifdef ENOTDIR
    { err_no: ENOTDIR, err_name: "ENOTDIR" },
#endif /* ENOTDIR */
#ifdef EISDIR
    { err_no: EISDIR, err_name: "EISDIR" },
#endif /* EISDIR */
#ifdef EINVAL
    { err_no: EINVAL, err_name: "EINVAL" },
#endif /* EINVAL */
#ifdef ENFILE
    { err_no: ENFILE, err_name: "ENFILE" },
#endif /* ENFILE */
#ifdef EMFILE
    { err_no: EMFILE, err_name: "EMFILE" },
#endif /* EMFILE */
#ifdef ENOTTY
    { err_no: ENOTTY, err_name: "ENOTTY" },
#endif /* ENOTTY */
#ifdef ETXTBSY
    { err_no: ETXTBSY, err_name: "ETXTBSY" },
#endif /* ETXTBSY */
#ifdef EFBIG
    { err_no: EFBIG, err_name: "EFBIG" },
#endif /* EFBIG */
#ifdef ENOSPC
    { err_no: ENOSPC, err_name: "ENOSPC" },
#endif /* ENOSPC */
#ifdef ESPIPE
    { err_no: ESPIPE, err_name: "ESPIPE" },
#endif /* ESPIPE */
#ifdef EROFS
    { err_no: EROFS, err_name: "EROFS" },
#endif /* EROFS */
#ifdef EMLINK
    { err_no: EMLINK, err_name: "EMLINK" },
#endif /* EMLINK */
#ifdef EPIPE
    { err_no: EPIPE, err_name: "EPIPE" },
#endif /* EPIPE */
#ifdef EDOM
    { err_no: EDOM, err_name: "EDOM" },
#endif /* EDOM */
#ifdef ERANGE
    { err_no: ERANGE, err_name: "ERANGE" },
#endif /* ERANGE */
#ifdef EAGAIN
    { err_no: EAGAIN, err_name: "EAGAIN" },
#endif /* EAGAIN */
#ifdef EWOULDBLOCK
    { err_no: EWOULDBLOCK, err_name: "EWOULDBLOCK" },
#endif /* EWOULDBLOCK */
#ifdef EINPROGRESS
    { err_no: EINPROGRESS, err_name: "EINPROGRESS" },
#endif /* EINPROGRESS */
#ifdef EALREADY
    { err_no: EALREADY, err_name: "EALREADY" },
#endif /* EALREADY */
#ifdef ENOTSOCK
    { err_no: ENOTSOCK, err_name: "ENOTSOCK" },
#endif /* ENOTSOCK */
#ifdef EDESTADDRREQ
    { err_no: EDESTADDRREQ, err_name: "EDESTADDRREQ" },
#endif /* EDESTADDRREQ */
#ifdef EMSGSIZE
    { err_no: EMSGSIZE, err_name: "EMSGSIZE" },
#endif /* EMSGSIZE */
#ifdef EPROTOTYPE
    { err_no: EPROTOTYPE, err_name: "EPROTOTYPE" },
#endif /* EPROTOTYPE */
#ifdef ENOPROTOOPT
    { err_no: ENOPROTOOPT, err_name: "ENOPROTOOPT" },
#endif /* ENOPROTOOPT */
#ifdef EPROTONOSUPPORT
    { err_no: EPROTONOSUPPORT, err_name: "EPROTONOSUPPORT" },
#endif /* EPROTONOSUPPORT */
#ifdef ESOCKTNOSUPPORT
    { err_no: ESOCKTNOSUPPORT, err_name: "ESOCKTNOSUPPORT" },
#endif /* ESOCKTNOSUPPORT */
#ifdef ENOTSUP
    { err_no: ENOTSUP, err_name: "ENOTSUP" },
#endif /* ENOTSUP */
#ifdef EOPNOTSUPP
    { err_no: EOPNOTSUPP, err_name: "EOPNOTSUPP" },
#endif /* EOPNOTSUPP */
#ifdef EPFNOSUPPORT
    { err_no: EPFNOSUPPORT, err_name: "EPFNOSUPPORT" },
#endif /* EPFNOSUPPORT */
#ifdef EAFNOSUPPORT
    { err_no: EAFNOSUPPORT, err_name: "EAFNOSUPPORT" },
#endif /* EAFNOSUPPORT */
#ifdef EADDRINUSE
    { err_no: EADDRINUSE, err_name: "EADDRINUSE" },
#endif /* EADDRINUSE */
#ifdef EADDRNOTAVAIL
    { err_no: EADDRNOTAVAIL, err_name: "EADDRNOTAVAIL" },
#endif /* EADDRNOTAVAIL */
#ifdef ENETDOWN
    { err_no: ENETDOWN, err_name: "ENETDOWN" },
#endif /* ENETDOWN */
#ifdef ENETUNREACH
    { err_no: ENETUNREACH, err_name: "ENETUNREACH" },
#endif /* ENETUNREACH */
#ifdef ENETRESET
    { err_no: ENETRESET, err_name: "ENETRESET" },
#endif /* ENETRESET */
#ifdef ECONNABORTED
    { err_no: ECONNABORTED, err_name: "ECONNABORTED" },
#endif /* ECONNABORTED */
#ifdef ECONNRESET
    { err_no: ECONNRESET, err_name: "ECONNRESET" },
#endif /* ECONNRESET */
#ifdef ENOBUFS
    { err_no: ENOBUFS, err_name: "ENOBUFS" },
#endif /* ENOBUFS */
#ifdef EISCONN
    { err_no: EISCONN, err_name: "EISCONN" },
#endif /* EISCONN */
#ifdef ENOTCONN
    { err_no: ENOTCONN, err_name: "ENOTCONN" },
#endif /* ENOTCONN */
#ifdef ESHUTDOWN
    { err_no: ESHUTDOWN, err_name: "ESHUTDOWN" },
#endif /* ESHUTDOWN */
#ifdef ETOOMANYREFS
    { err_no: ETOOMANYREFS, err_name: "ETOOMANYREFS" },
#endif /* ETOOMANYREFS */
#ifdef ETIMEDOUT
    { err_no: ETIMEDOUT, err_name: "ETIMEDOUT" },
#endif /* ETIMEDOUT */
#ifdef ECONNREFUSED
    { err_no: ECONNREFUSED, err_name: "ECONNREFUSED" },
#endif /* ECONNREFUSED */
#ifdef ELOOP
    { err_no: ELOOP, err_name: "ELOOP" },
#endif /* ELOOP */
#ifdef ENAMETOOLONG
    { err_no: ENAMETOOLONG, err_name: "ENAMETOOLONG" },
#endif /* ENAMETOOLONG */
#ifdef EHOSTDOWN
    { err_no: EHOSTDOWN, err_name: "EHOSTDOWN" },
#endif /* EHOSTDOWN */
#ifdef EHOSTUNREACH
    { err_no: EHOSTUNREACH, err_name: "EHOSTUNREACH" },
#endif /* EHOSTUNREACH */
#ifdef ENOTEMPTY
    { err_no: ENOTEMPTY, err_name: "ENOTEMPTY" },
#endif /* ENOTEMPTY */
#ifdef EPROCLIM
    { err_no: EPROCLIM, err_name: "EPROCLIM" },
#endif /* EPROCLIM */
#ifdef EUSERS
    { err_no: EUSERS, err_name: "EUSERS" },
#endif /* EUSERS */
#ifdef EDQUOT
    { err_no: EDQUOT, err_name: "EDQUOT" },
#endif /* EDQUOT */
#ifdef ESTALE
    { err_no: ESTALE, err_name: "ESTALE" },
#endif /* ESTALE */
#ifdef EREMOTE
    { err_no: EREMOTE, err_name: "EREMOTE" },
#endif /* EREMOTE */
#ifdef EBADRPC
    { err_no: EBADRPC, err_name: "EBADRPC" },
#endif /* EBADRPC */
#ifdef ERPCMISMATCH
    { err_no: ERPCMISMATCH, err_name: "ERPCMISMATCH" },
#endif /* ERPCMISMATCH */
#ifdef EPROGUNAVAIL
    { err_no: EPROGUNAVAIL, err_name: "EPROGUNAVAIL" },
#endif /* EPROGUNAVAIL */
#ifdef EPROGMISMATCH
    { err_no: EPROGMISMATCH, err_name: "EPROGMISMATCH" },
#endif /* EPROGMISMATCH */
#ifdef EPROCUNAVAIL
    { err_no: EPROCUNAVAIL, err_name: "EPROCUNAVAIL" },
#endif /* EPROCUNAVAIL */
#ifdef ENOLCK
    { err_no: ENOLCK, err_name: "ENOLCK" },
#endif /* ENOLCK */
#ifdef ENOSYS
    { err_no: ENOSYS, err_name: "ENOSYS" },
#endif /* ENOSYS */
#ifdef EFTYPE
    { err_no: EFTYPE, err_name: "EFTYPE" },
#endif /* EFTYPE */
#ifdef EAUTH
    { err_no: EAUTH, err_name: "EAUTH" },
#endif /* EAUTH */
#ifdef ENEEDAUTH
    { err_no: ENEEDAUTH, err_name: "ENEEDAUTH" },
#endif /* ENEEDAUTH */
#ifdef EPWROFF
    { err_no: EPWROFF, err_name: "EPWROFF" },
#endif /* EPWROFF */
#ifdef EDEVERR
    { err_no: EDEVERR, err_name: "EDEVERR" },
#endif /* EDEVERR */
#ifdef EOVERFLOW
    { err_no: EOVERFLOW, err_name: "EOVERFLOW" },
#endif /* EOVERFLOW */
#ifdef EBADEXEC
    { err_no: EBADEXEC, err_name: "EBADEXEC" },
#endif /* EBADEXEC */
#ifdef EBADARCH
    { err_no: EBADARCH, err_name: "EBADARCH" },
#endif /* EBADARCH */
#ifdef ESHLIBVERS
    { err_no: ESHLIBVERS, err_name: "ESHLIBVERS" },
#endif /* ESHLIBVERS */
#ifdef EBADMACHO
    { err_no: EBADMACHO, err_name: "EBADMACHO" },
#endif /* EBADMACHO */
#ifdef ECANCELED
    { err_no: ECANCELED, err_name: "ECANCELED" },
#endif /* ECANCELED */
#ifdef EIDRM
    { err_no: EIDRM, err_name: "EIDRM" },
#endif /* EIDRM */
#ifdef ENOMSG
    { err_no: ENOMSG, err_name: "ENOMSG" },
#endif /* ENOMSG */
#ifdef EILSEQ
    { err_no: EILSEQ, err_name: "EILSEQ" },
#endif /* EILSEQ */
#ifdef ENOATTR
    { err_no: ENOATTR, err_name: "ENOATTR" },
#endif /* ENOATTR */
#ifdef EBADMSG
    { err_no: EBADMSG, err_name: "EBADMSG" },
#endif /* EBADMSG */
#ifdef EMULTIHOP
    { err_no: EMULTIHOP, err_name: "EMULTIHOP" },
#endif /* EMULTIHOP */
#ifdef ENODATA
    { err_no: ENODATA, err_name: "ENODATA" },
#endif /* ENODATA */
#ifdef ENOLINK
    { err_no: ENOLINK, err_name: "ENOLINK" },
#endif /* ENOLINK */
#ifdef ENOSR
    { err_no: ENOSR, err_name: "ENOSR" },
#endif /* ENOSR */
#ifdef ENOSTR
    { err_no: ENOSTR, err_name: "ENOSTR" },
#endif /* ENOSTR */
#ifdef EPROTO
    { err_no: EPROTO, err_name: "EPROTO" },
#endif /* EPROTO */
#ifdef ETIME
    { err_no: ETIME, err_name: "ETIME" },
#endif /* ETIME */
#ifdef EOPNOTSUPP
    { err_no: EOPNOTSUPP, err_name: "EOPNOTSUPP" },
#endif /* EOPNOTSUPP */
#ifdef ENOPOLICY
    { err_no: ENOPOLICY, err_name: "ENOPOLICY" },
#endif /* ENOPOLICY */
    { err_no: 0, err_name: NULL },
};

static const char *
slaxErrnoName (int err)
{
    static char buf[16];
    errno_map_t *errp;

    for (errp = errno_map; errp->err_no; errp++)
	if (err == errp->err_no)
	    return errp->err_name;

    /* Not found; punt and hope for the best */
    snprintf(buf, sizeof(buf), "ERRNO%u", err);
    return buf;
}

/*
 * Set the exit code for the process:
 *     expr os:exit-code(15);
 */
static void
extOsExitCode (xmlXPathParserContext *ctxt, int nargs)
{
    int value;

    if (nargs != 1) {
	xmlXPathReturnNumber(ctxt, xsltGetMaxDepth());
	return;
    }

    value = xmlXPathPopNumber(ctxt);
    if (xmlXPathCheckError(ctxt))
	return;

    slaxSetExitCode(value);

    xmlXPathReturnEmptyString(ctxt);
}

static xmlNodePtr
slaxMakeNode (xmlDocPtr docp, xmlNodePtr parent,
	      const char *name, const char *content,
	      const char *attrname, const char *attrvalue)
{
    xmlNodePtr textp = NULL, nodep;

    if (parent == NULL)
	parent = (xmlNodePtr) docp; /* Default parent */

    if (content)
	textp = xmlNewText((const xmlChar *) content);

    nodep = xmlNewDocNode(docp, NULL, (const xmlChar *) name, NULL);
    if (nodep) {
	xmlAddChild(parent, nodep);
	if (textp)
	    xmlAddChild(nodep, textp);
	if (attrname)
	    xmlSetNsProp(nodep, NULL, (const xmlChar *) attrname,
			 (const xmlChar *) attrvalue);
    }

    return nodep;
}

static void
extOsMkdir (xmlXPathParserContext *ctxt, int nargs)
{
    char *name, *path, *cp;
    int rc, i;
    const char *value, *key;
    mode_t mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    int build_path = FALSE;

    if (nargs < 1 || nargs > 2) {
	xmlXPathReturnNumber(ctxt, xsltGetMaxDepth());
	return;
    }

    if (nargs == 2) {
	xmlXPathObject *xop = valuePop(ctxt);
	if (!xop->nodesetval || !xop->nodesetval->nodeNr) {
	    LX_ERR("os:mkdir invalid second parameter\n");
	    xmlXPathFreeObject(xop);
	    xmlXPathReturnEmptyString(ctxt);
	    return;
	}

	for (i = 0; i < xop->nodesetval->nodeNr; i++) {
	    xmlNodePtr nop, cop;

	    nop = xop->nodesetval->nodeTab[i];
	    if (nop->children == NULL)
		continue;

	    for (cop = nop->children; cop; cop = cop->next) {
		if (cop->type != XML_ELEMENT_NODE)
		    continue;

		key = xmlNodeName(cop);
		if (!key)
		    continue;
		value = xmlNodeValue(cop);

		if (streq(key, "mode")) {
		    mode_t x = strtol(value, NULL, 8);
		    if (x)
			mode = x;
		} else if (streq(key, "path")) {
		    build_path = TRUE;
		}
	    }
	}

	xmlXPathFreeObject(xop);
    }

    path = name = (char *) xmlXPathPopString(ctxt);
    if (name == NULL || xmlXPathCheckError(ctxt))
	return;

    cp = NULL;
    while (*name) {
	if (build_path) {
	    cp = strchr(name + 1, '/');
	    if (cp)
		*cp = '\0';
	}

	rc = mkdir(path, mode);
	if (rc && errno != EEXIST && cp == NULL) {
	    int eno = errno;
	    char *seno = strerror(eno);
	    const char *err_name = slaxErrnoName(eno);

	    slaxLog("os:mkdir for '%s' fails: %s", path, seno);

	    xmlDocPtr container = slaxMakeRtf(ctxt);
	    xmlNodePtr nodep = NULL;
	    if (container) {
		nodep = slaxMakeNode(container, NULL, "error", seno,
				     "errno", err_name);
		if (nodep)
		    xmlSetNsProp(nodep, NULL, (const xmlChar *) ATT_PATH,
				 (const xmlChar *) path);

	    }

	    valuePush(ctxt, xmlXPathNewNodeSet(nodep));
	    xmlFreeAndEasy(path);
	    return;
	}

	if (cp == NULL)
	    break;

	*cp = '/';
	name = cp + 1;
    }

    xmlFreeAndEasy(path);

    valuePush(ctxt, xmlXPathNewNodeSet(NULL));
}

slax_function_table_t slaxOsTable[] = {
    {
	"exit-code", extOsExitCode,
	"Specify an exit code for the process",
	"(number)", XPATH_UNDEFINED,
    },
    {
	"mkdir", extOsMkdir,
	"Create a directory",
	"(path)", XPATH_UNDEFINED,
    },
#if 0
    {
	"copy", extOsCopy,
	"Copy files",
	"(filespec, file-or-directory)", XPATH_NUMBER,
    },
    {
	"remove", extOsRemove
	"Remove files",
	"(filespec)", XPATH_UNDEFINED,
    },
    {
	"rmdir", extOsRmdir,
	"Remove a directory",
	"(path)", XPATH_UNDEFINED,
    },
    {
	"stat", extOsStat,
	"Return stat information about a file",
	"(file-spec)", XPATH_XSLT_TREE,
    },
    {
	"chmod", extOsChmod,
	"Change the mode of a file",
	"(file-spec, permissions)", XPATH_UNDEFINED,
    },
    {
	"chown", extOsChown,
	"Change ownership of a file",
	"(file-spec, ownership)", XPATH_UNDEFINED,
    },
#endif

    { NULL, NULL, NULL, NULL, XPATH_UNDEFINED }
};

SLAX_DYN_FUNC(slaxDynLibInit)
{
    arg->da_functions = slaxOsTable; /* Fill in our function table */

    return SLAX_DYN_VERSION;
}
