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
#include <glob.h>
#include <pwd.h>
#include <grp.h>
#include <dirent.h>

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

typedef struct statOptions_s {
    int so_brief;		/* Skip most fields */
    int so_depth;		/* Depth limit */
    int so_hidden;		/* Show hidden files */
    int so_recurse;		/* Should we recurse? */
} statOptions_t;

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

static xmlNodePtr
slaxMakeErrorNode (xmlXPathParserContext *ctxt, int eno,
		   const char *path, const char *message)
{
    xmlDocPtr container = slaxMakeRtf(ctxt);
    xmlNodePtr nodep;

    if (container == NULL)
	return NULL;

    nodep = xmlNewDocNode(container, NULL, (const xmlChar *) ELT_ERROR, NULL);
    if (nodep == NULL) {
	xmlFreeDoc(container);
	return NULL;
    }

    if (eno) {
	char *seno = strerror(eno);
	const char *err_name = slaxErrnoName(eno);

	slaxMakeNode(container, nodep, ELT_ERRNO, seno,
		     err_name ? ATT_CODE : NULL, err_name);
    }

    if (path)
	slaxMakeNode(container, nodep, ELT_PATH, path, NULL, NULL);

    if (message)
	slaxMakeNode(container, nodep, ELT_MESSAGE, message, NULL, NULL);

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

		if (streq(key, ELT_MODE)) {
		    mode_t x = strtol(value, NULL, 8);
		    if (x)
			mode = x;
		} else if (streq(key, ELT_PATH)) {
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
	if (rc && errno != EEXIST) {
	    xmlNodePtr nodep;
	    int eno = errno;

	    slaxLog("os:mkdir for '%s' fails: %d", path, eno);

	    nodep = slaxMakeErrorNode(ctxt, eno, path,
				      "could not make directory");

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

static void
extOsModeToPerm (char *buf, int bufsiz, mode_t mode)
{
    char *p = buf;
    int i;

    if (bufsiz < 11)
	return;

    switch (mode & S_IFMT) {
    case S_IFDIR:
        *p = 'd';
        break;
    case S_IFCHR:
        *p = 'c';
        break;
    case S_IFBLK:
        *p = 'b';
        break;
    case S_IFLNK:
        *p = 'l';
        break;
    case S_IFSOCK:
        *p = 's';
        break;
    case S_IFIFO:
        *p = 'f';
        break;
    default:
	*p = '-';
        break;
    }

    p += 1;

    for (i = 0; i < 3; i++) {
	*p++ = (mode & S_IRUSR) ? 'r' : '-';
	*p++ = (mode & S_IWUSR) ? 'w' : '-';
	*p++ = (mode & S_IXUSR) ? 'x' : '-';
	mode <<= 3;
    }

    *p = '\0';
}

static const char *
extOsModeToType (mode_t mode)
{
    switch (mode & S_IFMT) {
    case S_IFDIR:
	return "directory";
    case S_IFCHR:
	return "character";
    case S_IFBLK:
        return "block";
    case S_IFLNK:
	return "link";
    case S_IFSOCK:
        return "socket";
    case S_IFIFO:
        return "fifo";
    case S_IFREG:
        return "file";
    default:
	return "unknown";
    }
}

static void
extOsStatTime (char *buf, int bufsiz, struct timespec *tvp)
{
    static const char *month_names [] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    struct tm tm;

    localtime_r(&tvp->tv_sec, &tm);
    snprintf(buf, bufsiz, "%s %-2d %02d:%02d",
	     month_names[tm.tm_mon], tm.tm_mday, tm.tm_hour, tm.tm_min);
}

static void
extOsStatPath (xmlNodeSet *results, xmlDocPtr docp, xmlNodePtr parent,
	       const char *path, const char *name, int recurse,
	       statOptions_t *sop);

static void
extOsStatInfo (xmlNodeSet *results, xmlDocPtr docp, xmlNodePtr parent,
	       const char *path, struct stat *stp, int recurse,
	       statOptions_t *sop)
{
    char buf[BUFSIZ];
    char buf2[BUFSIZ/4];
    struct passwd *pwd = getpwuid(stp->st_uid);
    struct group *grp = getgrgid(stp->st_gid);
    int isdir = ((stp->st_mode & S_IFMT) == S_IFDIR);
    const char *tag = isdir ? ELT_DIRECTORY_NAME : ELT_FILE_NAME;

    slaxMakeNode(docp, parent, tag, path, NULL, NULL);

    if ((stp->st_mode & S_IFMT) == S_IFLNK) {
	slaxMakeNode(docp, parent, ELT_FILE_SYMLINK, "@", NULL, NULL);

	if (!sop->so_brief) {
	    ssize_t len = readlink(path, buf, sizeof(buf));

	    if (len < 0)
		buf[0] = '\0';
	    else if ((size_t) len < sizeof(buf))
		buf[len] = '\0';
	    else if ((size_t) len < sizeof(buf))
		buf[sizeof(buf) - 1] = '\0';
	
	    if (buf[0])
		slaxMakeNode(docp, parent, ELT_FILE_SYMLINK_TARGET, buf,
			     NULL, NULL);
	}

    } else if (isdir)
	slaxMakeNode(docp, parent, ELT_FILE_DIRECTORY, "/", NULL, NULL);

    else if (stp->st_mode & S_IXUSR)
	slaxMakeNode(docp, parent, ELT_FILE_EXECUTABLE, "*", NULL, NULL);

    slaxMakeNode(docp, parent, ELT_FILE_TYPE, extOsModeToType(stp->st_mode),
		 NULL, NULL);

    if (!sop->so_brief) {
	extOsModeToPerm(buf, sizeof(buf), stp->st_mode);
	snprintf(buf2, sizeof(buf2), "%o", stp->st_mode & 0777);
	slaxMakeNode(docp, parent, ELT_FILE_PERMISSIONS, buf2,
		     ATT_FORMAT, buf);

	snprintf(buf, sizeof(buf), "%d", stp->st_uid);
	slaxMakeNode(docp, parent, ELT_FILE_OWNER,
		     pwd ? pwd->pw_name : buf, ATT_UID, buf);

	snprintf(buf, sizeof(buf), "%d", stp->st_gid);
	slaxMakeNode(docp, parent, ELT_FILE_GROUP,
		     grp ? grp->gr_name : buf, ATT_GID, buf);

	snprintf(buf, sizeof(buf), "%d", stp->st_nlink);
	slaxMakeNode(docp, parent, ELT_FILE_LINKS, buf, NULL, NULL);

	snprintf(buf, sizeof(buf), "%llu", stp->st_size);
	slaxMakeNode(docp, parent, ELT_FILE_SIZE, buf, NULL, NULL);

	extOsStatTime(buf, sizeof(buf), &stp->st_mtimespec);
	snprintf(buf2, sizeof(buf2), "%ld", (long) stp->st_mtimespec.tv_sec);
	slaxMakeNode(docp, parent, ELT_FILE_DATE, buf2, ATT_FORMAT, buf);
    }

    if (isdir && recurse) {
	DIR *dirp = opendir(path);

	if (dirp) {
	    struct dirent *dp;

	    for (;;) {
		dp = readdir(dirp);
		if (dp == NULL)
		    break;
		if (!sop->so_hidden && dp->d_name[0] == '.')
		    continue;
		extOsStatPath(results, docp, parent, path, dp->d_name,
			      recurse - 1, sop);
	    }

	    closedir(dirp);
	}
    }
}

static void
extOsStatPath (xmlNodeSet *results, xmlDocPtr docp, xmlNodePtr parent,
	       const char *path, const char *name, int recurse,
	       statOptions_t *sop)
{
    int rc;
    struct stat st;
    xmlNodePtr nodep;
    char *newp = NULL;

    if (name) {
	ssize_t plen = strlen(path), nlen = strlen(name);

	/* Avoid double slashes */
	if (path[plen - 1] == '/')
	    plen -= 1;

	newp = alloca(plen + nlen + 2);
	if (newp) {
	    memcpy(newp, path, plen);
	    memcpy(newp + plen + 1, name, nlen);
	    newp[plen] = '/';
	    newp[plen + nlen + 1] = '\0';
	    path = newp;
	}
    }

    rc = stat(path, &st);
    if (rc)
	return;

    const char *tag = ((st.st_mode & S_IFMT) == S_IFDIR)
	? ELT_DIRECTORY : ELT_FILE_INFORMATION;

    nodep = xmlNewDocNode(docp, NULL, (const xmlChar *) tag, NULL);
    if (nodep == NULL)
	return;

    xmlAddChild(parent, nodep);

    if (results)
	xmlXPathNodeSetAdd(results, nodep);

    extOsStatInfo(results, docp, nodep, path, &st, recurse, sop);
}

static void
extOsStat (xmlXPathParserContext *ctxt, int nargs)
{
    xmlXPathObject *stack[nargs];	/* Stack for args as objects */
    xmlXPathObject *xop;
    char **cpp;
    int ndx, rc, i;
    glob_t gl;
    int gflags = GLOB_APPEND | GLOB_TILDE | GLOB_NOCHECK;
    statOptions_t so;

    for (ndx = 0; ndx < nargs; ndx++)
	stack[nargs - 1 - ndx] = valuePop(ctxt);

    bzero(&gl, sizeof(gl));
    bzero(&so, sizeof(so));

    for (ndx = 0; ndx < nargs; ndx++) {
	if (stack[ndx] == NULL)	/* Should not occur */
	    continue;

	xop = stack[ndx];

	if (xop->stringval) {
	    rc = glob((const char *) xop->stringval, gflags, NULL, &gl);
	    if (rc) {
		slaxLog("glob returned %d (%d)", rc, errno);
		/* XXX But we otherwise ignore and continue */
	    }

	} else if (xop->nodesetval) {
	    for (i = 0; i < xop->nodesetval->nodeNr; i++) {
		xmlNodePtr nop, cop;
		const char *value, *key;

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

		    if (streq(key, ELT_BRIEF)) {
			so.so_brief = TRUE;
		    } else if (streq(key, ELT_DEPTH)) {
			so.so_depth = strtol(value, NULL, 0);
		    } else if (streq(key, ELT_HIDDEN)) {
			so.so_hidden = TRUE;
		    } else if (streq(key, ELT_RECURSE)) {
			so.so_recurse = TRUE;
		    }
		}
	    }
	}

	xmlXPathFreeObject(stack[ndx]);
    }

    xmlDocPtr container = slaxMakeRtf(ctxt);
    xmlNodeSet *results = xmlXPathNodeSetCreate(NULL);

    int recurse = so.so_depth ?: so.so_recurse ? -1 : 1;
    for (cpp = gl.gl_pathv; *cpp; cpp++)
	extOsStatPath(results, container, NULL, *cpp, NULL, recurse, &so);

    valuePush(ctxt, xmlXPathWrapNodeSet(results));
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
    {
	"stat", extOsStat,
	"Return stat information about a file",
	"(file-spec)", XPATH_XSLT_TREE,
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
