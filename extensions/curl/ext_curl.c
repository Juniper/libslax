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
#include <assert.h>

#include <sys/queue.h>
#include <curl/curl.h>

#include <libxml/xpathInternals.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlsave.h>
#include <libxml/HTMLparser.h>

#include "slaxconfig.h"

#include <libxslt/extensions.h>
#include <libslax/slaxdata.h>
#include <libslax/slaxdyn.h>
#include <libslax/slaxio.h>
#include <libslax/xmlsoft.h>
#include <libpsu/psulog.h>
#include <libpsu/psutime.h>

#include "ext_curl.h"

#define CURL_FULL_NS "http://xml.libslax.org/curl"
#define CURL_NAME_SIZE 12

/*
 * Defines the set of options we which to control the next request.
 * Options can be temporarily given on the curl:perform() call
 */
typedef struct curl_opts_s {
    char *co_url;		/* URL to operate on */
    char *co_method;		/* HTTP method (GET,POST,etc) */
    u_int8_t co_upload;		/* Are we uploading (aka FTP PUT)? */
    u_int8_t co_fail_on_error;	/* Fail explicitly if HTTP code >= 400 */
    u_int8_t co_verbose;	/* Verbose (debug) output */
    u_int8_t co_insecure;	/* Allow insecure SSL certs  */
    u_int8_t co_secure;		/* Use SSL-enabled version of protocol */
    int co_errors;		/* How to handle errors (SLAX_ERROR_*) */
    long co_timeout;		/* Operation timeout */
    long co_connect_timeout;	/* Connect timeout */
    char *co_username;		/* Value for CURLOPT_USERNAME */
    char *co_password;		/* Value for CURLOPT_PASSWORD */
    slax_data_list_t co_headers; /* Headers for CURLOPT_HTTPHEADER */
    slax_data_list_t co_params; /* Parameters for GET or POST */
    char *co_content_type;	 /* Content-Type header */
    char *co_contents;		 /* Contents for a post */

    /* Email-specific fields */
    char *co_format;		 /* Format of the response */
    char *co_server;		 /* Email server name */
    char *co_local;		 /* Email local  name */
    char *co_from;		 /* Email from address */
    char *co_subject;		 /* Email subject */
    slax_data_list_t co_to;	 /* Email "To:" addresses */
    slax_data_list_t co_cc;	 /* Email "Cc:" addresses */
} curl_opts_t;

/*
 * This is the main curl datastructure, a handle that is maintained
 * between curl function calls.  They are named using a small integer,
 * which is passed back to the extension functions to find the handle.
 * All other information is hung off this handle, including the underlaying
 * libcurl handle.
 */
typedef struct curl_handle_s {
    TAILQ_ENTRY(curl_handle_s) ch_link; /* Next session */
    slax_data_list_t ch_reply_headers;
    slax_data_list_t ch_reply_data;
    char ch_name[CURL_NAME_SIZE]; /* Unique ID for this handle */
    CURL *ch_handle;		/* libcurl "easy" handle */
    unsigned ch_code;		/* Most recent return code */
    curl_opts_t ch_opts;	/* Options set for this handle */
    char ch_error[CURL_ERROR_SIZE]; /* Error buffer for CURLOPT_ERRORBUFFER */
} curl_handle_t;

TAILQ_HEAD(curl_session_s, curl_handle_s) extCurlSessions;

/*
 * Discard any transient data in the handle, particularly data
 * read from the peer.
 */
static void
extCurlHandleClean (curl_handle_t *curlp)
{
    slaxDataListClean(&curlp->ch_reply_data);
    slaxDataListClean(&curlp->ch_reply_headers);
}

/*
 * Release a set of options. The curl_opts_t is not freed, only its
 * contents.
 */
static void
extCurlOptionsRelease (curl_opts_t *opts)
{
    xmlFreeAndEasy(opts->co_url);
    xmlFreeAndEasy(opts->co_method);
    xmlFreeAndEasy(opts->co_username);
    xmlFreeAndEasy(opts->co_password);
    xmlFreeAndEasy(opts->co_content_type);
    xmlFreeAndEasy(opts->co_contents);
    xmlFreeAndEasy(opts->co_format);
    xmlFreeAndEasy(opts->co_server);
    xmlFreeAndEasy(opts->co_local);
    xmlFreeAndEasy(opts->co_from);
    xmlFreeAndEasy(opts->co_subject);

    slaxDataListClean(&opts->co_headers);
    slaxDataListClean(&opts->co_params);
    slaxDataListClean(&opts->co_to);
    slaxDataListClean(&opts->co_cc);

    bzero(opts, sizeof(*opts));
}

#define COPY_STRING(_name) top->_name = xmlStrdup2(fromp->_name);
#define COPY_FIELD(_name) top->_name = fromp->_name;

/*
 * Copy a set of options from one curl_opts_t to another.
 */
static void
extCurlOptionsCopy (curl_opts_t *top, curl_opts_t *fromp)
{
    bzero(top, sizeof(*top));

    COPY_STRING(co_url);
    COPY_STRING(co_method);
    COPY_FIELD(co_upload);
    COPY_FIELD(co_fail_on_error);
    COPY_FIELD(co_verbose);
    COPY_FIELD(co_insecure);
    COPY_FIELD(co_secure);
    COPY_FIELD(co_errors);
    COPY_STRING(co_username);
    COPY_STRING(co_password);
    COPY_STRING(co_content_type);
    COPY_STRING(co_contents);
    COPY_STRING(co_format);
    COPY_STRING(co_server);
    COPY_STRING(co_local);
    COPY_STRING(co_from);
    COPY_STRING(co_subject);
    COPY_FIELD(co_timeout);
    COPY_FIELD(co_connect_timeout);

    slaxDataListInit(&top->co_headers);
    slaxDataListCopy(&top->co_headers, &fromp->co_headers);

    slaxDataListInit(&top->co_params);
    slaxDataListCopy(&top->co_params, &fromp->co_params);

    slaxDataListInit(&top->co_to);
    slaxDataListCopy(&top->co_to, &fromp->co_to);

    slaxDataListInit(&top->co_cc);
    slaxDataListCopy(&top->co_cc, &fromp->co_cc);
}

/*
 * Free a curl handle
 */
static void
extCurlHandleFree (curl_handle_t *curlp)
{
    extCurlHandleClean(curlp);

    if (curlp->ch_handle)
	curl_easy_cleanup(curlp->ch_handle);
    extCurlOptionsRelease(&curlp->ch_opts);

    TAILQ_REMOVE(&extCurlSessions, curlp, ch_link);
    xmlFree(curlp);
}

/*
 * Given the name of a handle, return it.
 */
static curl_handle_t *
extCurlHandleFind (const char *name)
{
    curl_handle_t *curlp;

    TAILQ_FOREACH(curlp, &extCurlSessions, ch_link) {
	if (streq(name, curlp->ch_name))
	    return curlp;
    }

    return NULL;
}

static int
strceq (const char *red, const char *blue)
{
    return (red && blue && tolower((int) *red) == tolower((int) *blue)
	    && strcasecmp(red + 1, blue + 1) == 0);
}

static void
extCurlSetContents (curl_opts_t *opts, xmlNodePtr nodep)
{
    if (opts->co_contents) {
	xmlFree(opts->co_contents);
	opts->co_contents = NULL;
    }

    if (nodep && nodep->type == XML_ELEMENT_NODE && nodep->children
		&& nodep->children->next == NULL) {
	xmlBufferPtr buf = xmlBufferCreate();
	if (buf) {
	    xmlSaveCtxtPtr handle = xmlSaveToBuffer(buf, NULL, 0);
	    if (handle) {
		xmlSaveTree(handle, nodep->children);
		xmlSaveFlush(handle);
		xmlSaveClose(handle);

		opts->co_contents = (char *) buf->content;
		buf->content = NULL;
	    }

	    xmlBufferFree(buf);
	}
    }
}

#define CURL_SET_STRING(_v) \
    do { \
	if (_v) \
	    xmlFree(_v); \
	_v = xmlStrdup2(xmlNodeValue(nodep)); \
    } while (0)

/*
 * Parse any options from an input XML node.
 */
static void
extCurlParseNode (curl_opts_t *opts, xmlNodePtr nodep)
{
    const char *key;

    key = xmlNodeName(nodep);
    if (key == NULL)
	return;

    if (streq(key, "url"))
	CURL_SET_STRING(opts->co_url);
    else if (streq(key, "method"))
	CURL_SET_STRING(opts->co_method);
    else if (streq(key, "username"))
	CURL_SET_STRING(opts->co_username);
    else if (streq(key, "password"))
	CURL_SET_STRING(opts->co_password);
    else if (streq(key, "content-type"))
	CURL_SET_STRING(opts->co_content_type);
    else if (streq(key, "contents"))
	extCurlSetContents(opts, nodep);
    else if (streq(key, "format"))
	CURL_SET_STRING(opts->co_format);
    else if (streq(key, "server"))
	CURL_SET_STRING(opts->co_server);
    else if (streq(key, "local"))
	CURL_SET_STRING(opts->co_local);
    else if (streq(key, "from"))
	CURL_SET_STRING(opts->co_from);
    else if (streq(key, "subject"))
	CURL_SET_STRING(opts->co_subject);
    else if (streq(key, "upload"))
	opts->co_upload = TRUE;
    else if (streq(key, "fail-on-error"))
	opts->co_fail_on_error = TRUE;
    else if (streq(key, "verbose"))
	opts->co_verbose = TRUE;
    else if (streq(key, "insecure"))
	opts->co_insecure = TRUE;
    else if (streq(key, "secure"))
	opts->co_secure = TRUE;
    else if (streq(key, "errors"))
	opts->co_errors = slaxErrorValue(xmlNodeValue(nodep));
    else if (streq(key, "timeout"))
	opts->co_timeout = atoi(xmlNodeValue(nodep));
    else if (streq(key, "connect-timeout"))
	opts->co_connect_timeout = atoi(xmlNodeValue(nodep));

    else if (streq(key, "to"))
	slaxDataListAdd(&opts->co_to, xmlNodeValue(nodep));

    else if (streq(key, "cc"))
	slaxDataListAdd(&opts->co_cc, xmlNodeValue(nodep));

    else if (streq(key, "header")) {
	/* Header fields aren't quite so easy */
	char *name = (char *) xmlGetProp(nodep, (const xmlChar *) "name");
	const char *value = xmlNodeValue(nodep);

	size_t bufsiz = BUFSIZ, len;
	char *buf = alloca(bufsiz);

	len = snprintf(buf, bufsiz, "%s: %s", name, value);
	if (len >= bufsiz) {
	    bufsiz = len + 1;
	    buf = alloca(bufsiz);
	    len = snprintf(buf, bufsiz, "%s: %s", name, value);
	}

	slaxDataListAddLen(&opts->co_headers, buf, len);

	xmlFree(name);

    } else if (streq(key, "param")) {
	static char must_escape[] = "-_.!~*'();/?:@&=+$,[] \t\n";
	static char hexnum[] = "0123456789ABCDEF";

	/* Parameters can be either POST or GET style */
	char *name = (char *) xmlGetProp(nodep, (const xmlChar *) "name");
	const char *value = xmlNodeValue(nodep);
	const char *vp, *real_value;
	char *cp;

	size_t bufsiz = BUFSIZ, len;
	char *buf = alloca(bufsiz);

	int ecount = 0;
	for (vp = value; *vp; vp++)
	    if (strchr(must_escape, *vp) != 0)
		ecount += 1;

	real_value = value;
	if (ecount) {
	    cp = alloca(strlen(value) + ecount * 3 + 1);
	    if (cp) {
		real_value = cp;
		for (vp = value; *vp; vp++) {
		    if (strchr(must_escape, *vp) != 0) {
			*cp++ = '%';
			*cp++ = hexnum[(*vp >> 4) & 0xF];
			*cp++ = hexnum[*vp & 0xF];
		    } else {
			*cp++ = *vp;
		    }
		}
		*cp = '\0';
	    }
	}

	len = snprintf(buf, bufsiz, "%s=%s", name, real_value);
	if (len >= bufsiz) {
	    bufsiz = len + 1;
	    buf = alloca(bufsiz);
	    len = snprintf(buf, bufsiz, "%s=%s", name, real_value);
	}

	slaxDataListAddLen(&opts->co_params, buf, len);

	xmlFree(name);

    }
}

/*
 * Record data from a libcurl callback into our curl handle.
 */
static size_t
extCurlRecordData (curl_handle_t *curlp UNUSED, void *buf, size_t bufsiz,
		      slax_data_list_t *listp)
{
    slaxDataListAddLen(listp, buf, bufsiz);

    return bufsiz;
}

/*
 * The callback we give libcurl to write data that has been received from
 * a transfer request.
 */
static size_t
extCurlWriteData (void *buf, size_t membsize, size_t nmemb, void *userp)
{
    curl_handle_t *curlp = userp;
    size_t bufsiz = membsize * nmemb;

    extCurlRecordData(curlp, buf, bufsiz, &curlp->ch_reply_data);

    return bufsiz;
}

/*
 * The callback we give libcurl to catch header data that has been received
 * from a server.
 */
static size_t 
extCurlHeaderData (void *buf, size_t membsize, size_t nmemb, void *userp)
{
    curl_handle_t *curlp = userp;
    size_t bufsiz = membsize * nmemb;

    extCurlRecordData(curlp, buf, bufsiz, &curlp->ch_reply_headers);

    return bufsiz;
}

#define CURL_SET(_n, _v) curl_easy_setopt(curlp->ch_handle, _n, _v)
#define CURL_COND(_n, _v) \
    do { \
	if (_v) \
	    curl_easy_setopt(curlp->ch_handle, _n, _v);	\
    } while (0)

/*
 * Allocate a curl handle and populate it with reasonable values.  In
 * truth, we wrap the real libcurl handle inside our own structure, so
 * this is a "half us and half them" thing.
 */
static curl_handle_t *
extCurlHandleAlloc (void)
{
    curl_handle_t *curlp = xmlMalloc(sizeof(*curlp));
    static unsigned seed = 1618; /* Non-zero starting number (why not phi?) */

    if (curlp) {
	bzero(curlp, sizeof(*curlp));
	/* Give it a unique number as a name */
	snprintf(curlp->ch_name, sizeof(curlp->ch_name), "curl%u", seed++);
	slaxDataListInit(&curlp->ch_reply_data);
	slaxDataListInit(&curlp->ch_reply_headers);
	slaxDataListInit(&curlp->ch_opts.co_headers);
	slaxDataListInit(&curlp->ch_opts.co_params);
	slaxDataListInit(&curlp->ch_opts.co_to);
	slaxDataListInit(&curlp->ch_opts.co_cc);

	/* Create and populate the real libcurl handle */
	curlp->ch_handle = curl_easy_init();

	/* Add it to the list of curl handles */
	TAILQ_INSERT_TAIL(&extCurlSessions, curlp, ch_link);
    }

    return curlp;
}

static int
extCurlVerbose (CURL *handle UNUSED, curl_infotype type,
		char *data, size_t size, void *opaque)
{
    curl_handle_t *curlp UNUSED = opaque;
    const char *text, *dir;
 
    switch (type) {
    case CURLINFO_TEXT:
	slaxLog("curl:== Info: %s", data);
	return 0;
 
    case CURLINFO_HEADER_OUT:
	text = "Send header";
	dir = "=> ";
	break;

    case CURLINFO_DATA_OUT:
	text = "Send data";
	dir = "=> ";
	break;

    case CURLINFO_SSL_DATA_OUT:
	text = "Send SSL data";
	dir = "=> ";
	break;

    case CURLINFO_HEADER_IN:
	text = "Recv header";
	dir = "<= ";
	break;

    case CURLINFO_DATA_IN:
	text = "Recv data";
	dir = "<= ";
	break;

    case CURLINFO_SSL_DATA_IN:
	text = "Recv SSL data";
	dir = "<= ";
	break;

    default:	    /* In case a new one is introduced to shock us */ 
	return 0;
    }
 
    psu_mem_dump(text, data, size, dir, 0);
    return 0;
}

/*
 * Turn a chain of cur_data_t into a libcurl-style slist.
 */
static struct curl_slist *
extCurlBuildSlist (slax_data_list_t *listp, struct curl_slist *slist)
{
    slax_data_node_t *dnp;

    SLAXDATALIST_FOREACH(dnp, listp) {
	slist = curl_slist_append(slist, dnp->dn_data);
    }

    return slist;
}

static char *
extCurlBuildParamData (slax_data_list_t **chains)
{
    slax_data_list_t **chainp;
    slax_data_node_t *dnp;
    size_t len = 0;
    char *buf, *cp;

    for (chainp = chains; *chainp; chainp++) {
	SLAXDATALIST_FOREACH(dnp, *chainp) {
	    len += dnp->dn_len + 1;
	}
    }

    if (len == 0)
	return NULL;

    buf = xmlMalloc(len);
    if (buf == 0)
	return NULL;

    for (cp = buf, chainp = chains; *chainp; chainp++) {
	SLAXDATALIST_FOREACH(dnp, *chainp) {
	    if (cp != buf)
		*cp++ = '&';
	    memcpy(cp, dnp->dn_data, dnp->dn_len);
	    cp += dnp->dn_len;
	}
    }
    *cp = '\0';

    return buf;
}

/*
 * Open a persistent curl handle to allow persistent connections of the
 * underlaying protocol.
 *
 * Usage:
 *    var $handle = curl:open();
 */
static void
extCurlOpen (xmlXPathParserContext *ctxt, int nargs)
{
    curl_handle_t *curlp;

    if (nargs != 0) {
	/* Error: too many args */
	xmlXPathSetArityError(ctxt);
	return;
    }

    curlp = extCurlHandleAlloc();

    /* Return session cookie */
    xmlXPathReturnString(ctxt, xmlStrdup((const xmlChar *) curlp->ch_name));
}

/*
 *  Close a given handle.
 *
 * Usage:
 *    expr curl:close($handle); 
 */
static void
extCurlClose (xmlXPathParserContext *ctxt, int nargs)
{
    char *name = NULL;
    curl_handle_t *curlp;

    if (nargs != 1) {
	xmlXPathSetArityError(ctxt);
	return;
    }

    name = (char *) xmlXPathPopString(ctxt);
    if (name == NULL) {
	LX_ERR("curl:close: no argument\n");
	return;
    }

    curlp = extCurlHandleFind(name);
    if (curlp)
	extCurlHandleFree(curlp);

    xmlFree(name);

    valuePush(ctxt, xmlXPathNewNodeSet(NULL));
}

struct cr_data {
    char *cr_data;
    size_t cr_len;
    size_t cr_offset;
};

static size_t
extCurlReadContents (char *buf, size_t isize, size_t nitems, void *userp)
{
    struct cr_data *crp = userp;
    size_t bufsiz = isize * nitems;

    if (crp->cr_offset >= crp->cr_len)
	return 0;

    if (bufsiz > crp->cr_len - crp->cr_offset)
	bufsiz = crp->cr_len - crp->cr_offset;

    memcpy(buf, crp->cr_data + crp->cr_offset, bufsiz);
    crp->cr_offset += bufsiz;

    return bufsiz;
}

static char *
extCurlBuildEmail (curl_opts_t *opts)
{
    char *to_line, *cc_line;
    time_t now = time(NULL);
    char buf[80];
    char *date_line = psu_iso_time(&now, buf, sizeof(buf));
    char msgid[BUFSIZ];
    size_t len;
    char *cp;
    char *subject = opts->co_subject;
    int rc;

    snprintf(msgid, sizeof(msgid), "ext-curl-%lu-%lu",
	     (unsigned long) time, (unsigned long) random());

    len = slaxDataListAsCharLen(&opts->co_to, ", ");
    if (len <= 1)
	to_line = NULL;
    else {
	to_line = alloca(len);
	slaxDataListAsChar(to_line, len, &opts->co_to, ", ");
    }

    len = slaxDataListAsCharLen(&opts->co_cc, ", ");
    if (len <= 1)
	cc_line = NULL;
    else {
	cc_line = alloca(len);
	slaxDataListAsChar(cc_line, len, &opts->co_cc, ", ");
    }

    cp = strchr(subject, '\n');
    if (cp)
	*cp = '\0';

    rc = asprintf(&cp, "From: %s%s%s%s%s%s%s\nDate: %s\n"
	     "Message-ID: %s\n\n%s\n",
	     opts->co_from ?: "",
	     to_line ? "\nTo: " : "", to_line ?: "",
	     cc_line ? "\nCc: " : "", cc_line ?: "",
	     subject ? "\nSubject: " : "", subject ?: "",
	     date_line, msgid, opts->co_contents ?: "");
    return (rc > 0) ? cp : NULL;
}

/*
 * Email has a whole set of values that need pumped into the
 * curl handle.
 */
static CURLcode
extCurlDoEmail (curl_handle_t *curlp, curl_opts_t *opts UNUSED)
{
    CURLcode success;
    struct curl_slist *mailto_listp = NULL;
    char ubuf[BUFSIZ];
    char *from;
    size_t len;
    char *buf;
    struct cr_data cr;

    /*
     * Email is a completely different fish; we need to build the
     * URL from a couple of values.
     */
    snprintf(ubuf, sizeof(ubuf), "smtp%s://%s%s%s",
	     opts->co_secure ? "s" : "", opts->co_server,
	     opts->co_local ? "/" : "", opts->co_local ?: "");
    CURL_SET(CURLOPT_URL, ubuf);

    /* The "from" needs to be wrapped in "<>"s, if not present */
    if (strchr(opts->co_from, '<') != NULL)
	from = opts->co_from;
    else {
	len = strlen(opts->co_from) + 3;
	from = alloca(len);

	snprintf(from, len, "<%s>", opts->co_from);
    }
    CURL_SET(CURLOPT_MAIL_FROM, from);

    mailto_listp = extCurlBuildSlist(&opts->co_to, NULL);
    mailto_listp = extCurlBuildSlist(&opts->co_cc, mailto_listp);
    CURL_SET(CURLOPT_MAIL_RCPT, mailto_listp);

    bzero(&cr, sizeof(cr));
    buf = cr.cr_data = extCurlBuildEmail(opts);
    if (buf) {
	cr.cr_len = strlen(buf);
	cr.cr_offset = 0;

	CURL_SET(CURLOPT_READFUNCTION, extCurlReadContents);
	CURL_SET(CURLOPT_READDATA, &cr);
	CURL_SET(CURLOPT_UPLOAD, 1L);
    }

    success = curl_easy_perform(curlp->ch_handle);

    CURL_SET(CURLOPT_MAIL_RCPT, NULL);
    curl_slist_free_all(mailto_listp);
    if (buf)
	free(buf);

    /* curl won't send the QUIT command until you call cleanup */
    curl_easy_reset(curlp->ch_handle);

    return success;
}

/*
 * Perform a libcurl transfer but setting up all the options which
 * we've been provided with, and then calling curl_easy_perform().
 * Since options are normally _very_ sticky to libcurl, we'd like to
 * explicitly set all values to the ones we've been asked for.  But
 * due to the nature of the curl_easy_setopt interface we are forced
 * use curl_easy_reset instead.
 */
static CURLcode
extCurlDoPerform (curl_handle_t *curlp, curl_opts_t *opts)
{
    CURLcode success;
    long putv = 0, postv = 0, getv = 0, deletev = 0, headv = 0, emailv = 0,
	    uploadv = 0;
    struct curl_slist *headers = NULL;
    char *param_data = NULL;

    curl_easy_reset(curlp->ch_handle);
    extCurlHandleClean(curlp); /* Shouldn't be needed */

    if (opts->co_method) {
	if (strceq(opts->co_method, "delete")) {
	    deletev = 1;
	    CURL_SET(CURLOPT_CUSTOMREQUEST, "DELETE");
	} else if (strceq(opts->co_method, "email"))
	    emailv = 1;
	else if (strceq(opts->co_method, "get"))
	    getv = 1;
	else if (strceq(opts->co_method, "head"))
	    headv = 1;
	else if (strceq(opts->co_method, "post"))
	    postv = 1;
	else if (strceq(opts->co_method, "put")) {
	    putv = 1;
	    CURL_SET(CURLOPT_CUSTOMREQUEST, "PUT");
	} else if (strceq(opts->co_method, "upload"))
	    uploadv = 1;
    }

    if (deletev + emailv + getv + postv + putv + uploadv == 0)
	getv = 1;		/* Default method */

    CURL_COND(CURLOPT_HTTPGET, getv);
    CURL_COND(CURLOPT_UPLOAD, uploadv);
    CURL_COND(CURLOPT_POST, postv);
    CURL_COND(CURLOPT_NOBODY, headv); /* There's no "body" in a HEAD request */

    /* Here are some relatively static options we have to set */
    CURL_SET(CURLOPT_ERRORBUFFER, curlp->ch_error); /* Get real errors */
    CURL_SET(CURLOPT_NETRC, CURL_NETRC_OPTIONAL); /* Allow .netrc */

    /* Register callbacks */
    CURL_SET(CURLOPT_WRITEFUNCTION, extCurlWriteData);
    CURL_SET(CURLOPT_WRITEDATA, curlp);
    CURL_SET(CURLOPT_HEADERFUNCTION, extCurlHeaderData);
    CURL_SET(CURLOPT_WRITEHEADER, curlp);

    CURL_COND(CURLOPT_USERNAME, opts->co_username);
    CURL_COND(CURLOPT_PASSWORD, opts->co_password);
    CURL_COND(CURLOPT_TIMEOUT, opts->co_timeout);
    CURL_COND(CURLOPT_CONNECTTIMEOUT, opts->co_connect_timeout);

    CURL_SET(CURLOPT_FAILONERROR, opts->co_fail_on_error ? 1L : 0L);

    if (opts->co_verbose) {
	/*
	 * Turn on the callback to get debug information out of libcurl.
	 * The DEBUGFUNCTION has no effect until we enable VERBOSE.
	 */ 
	curl_easy_setopt(curlp->ch_handle,
			 CURLOPT_DEBUGFUNCTION, extCurlVerbose);
	curl_easy_setopt(curlp->ch_handle, CURLOPT_DEBUGDATA, curlp);
	curl_easy_setopt(curlp->ch_handle, CURLOPT_VERBOSE, 1L);
    }

    if (opts->co_insecure) {
	/* Don't care about the signing CA */
	curl_easy_setopt(curlp->ch_handle, CURLOPT_SSL_VERIFYPEER, 0L);
	/* Don't care about the common name in the cert */
	curl_easy_setopt(curlp->ch_handle, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    /*
     * Email is a completely different bird, so we let it fly
     * a unique route.
     */
    if (emailv)
	return extCurlDoEmail(curlp, opts);

    if (opts->co_secure)
	CURL_SET(CURLOPT_USE_SSL, (long) CURLUSESSL_ALL);

    /* A missing URL is fatal */
    if (opts->co_url == NULL) {
	LX_ERR("curl: missing URL\n");
	return FALSE;
    }

    CURL_SET(CURLOPT_URL, opts->co_url);

    /* Build our headers */

    /*
     * libcurl turns on the use of "Expect: 100-continue" which
     * is not widely supported; so we turn it off.
     */
    headers = curl_slist_append(NULL, "Expect:"); /* Turn off Expect */

    if (opts->co_content_type) {
	static char content_header_field[] = "Content-Type: ";
	size_t mlen = strlen(opts->co_content_type);
	char *content_header = alloca(mlen + sizeof(content_header_field));

	memcpy(content_header, content_header_field,
	       sizeof(content_header_field));
	memcpy(content_header + sizeof(content_header_field) - 1,
	       opts->co_content_type, mlen + 1);

	headers = curl_slist_append(headers, content_header);
    }

    /*
     * Build a list of headers containing both the handles options
     * and the ones passed into this function.
     */
    if (opts != &curlp->ch_opts)
	headers = extCurlBuildSlist(&curlp->ch_opts.co_headers, headers);
    headers = extCurlBuildSlist(&opts->co_headers, headers);
    CURL_SET(CURLOPT_HTTPHEADER, headers);

    if (postv || putv) {
	int len = opts->co_contents ? strlen(opts->co_contents) : 0;
	CURL_SET(CURLOPT_POSTFIELDSIZE, len);
	CURL_SET(CURLOPT_POSTFIELDS, opts->co_contents ?: "");
    } else if (uploadv && opts->co_contents) {
	struct cr_data cr;

	bzero(&cr, sizeof(cr));
	cr.cr_data = opts->co_contents;
	cr.cr_len = strlen(opts->co_contents);

	CURL_SET(CURLOPT_INFILESIZE, (long) cr.cr_len);
	CURL_SET(CURLOPT_READFUNCTION, extCurlReadContents);
	CURL_SET(CURLOPT_READDATA, &cr);
    }

    slax_data_list_t *param_data_lists[] = {
	&curlp->ch_opts.co_params,
	NULL,			/* Slot filled in below */
	NULL
    };

    if (opts != &curlp->ch_opts)
	param_data_lists[1] = &opts->co_params;

    param_data = extCurlBuildParamData(param_data_lists);
    if (param_data) {
	if (getv || deletev) {
	    size_t ulen = strlen(opts->co_url), plen = strlen(param_data);
	    size_t bufsiz = ulen + plen + 2; /* '?' and NUL */
	    char *buf = alloca(bufsiz);

	    memcpy(buf, opts->co_url, ulen);
	    buf[ulen] = '?';
	    memcpy(buf + ulen + 1, param_data, plen + 1);

	    CURL_SET(CURLOPT_URL, buf);

	} else if (postv || putv) {
	    CURL_SET(CURLOPT_POSTFIELDS, param_data);
	    CURL_SET(CURLOPT_POSTFIELDSIZE, (long) strlen(param_data));
	}
    }

    success = curl_easy_perform(curlp->ch_handle);

    /*
     * Remove our data to avoid dangling references.  We do
     * this for headers and post data.
     */
    if (headers) {		/* Free the header list */
	CURL_SET(CURLOPT_HTTPHEADER, NULL);
	curl_slist_free_all(headers);
    }

    if (param_data)
	xmlFree(param_data);

    /* Clear it on the way out, just to be absolutely certain */
    curl_easy_reset(curlp->ch_handle);

    return success;
}

static void
extCurlBuildDataParsed (curl_handle_t *curlp UNUSED, curl_opts_t *opts,
			    xmlDocPtr docp, xmlNodePtr parent,
			    const char *raw_data)
{
    const char *cp, *ep, *sp;
    char *nbuf = NULL, *vbuf = NULL;
    ssize_t nbufsiz = 0, vbufsiz = 0;
    xmlNodePtr errp = NULL;
    xmlNodePtr nodep = xmlNewDocNode(docp, NULL,
				     (const xmlChar *) "data", NULL);
    if (nodep == NULL)
	return;

    xmlNodePtr addedp = xmlAddChild(parent, nodep);
    if (addedp != NULL) {
        xmlSetProp(addedp, (const xmlChar *) "format",
	       (const xmlChar *) opts->co_format);
    } else {
        xmlFreeNode(nodep);
        return;
    }

    if (opts->co_errors) {
	if (opts->co_errors == SLAX_ERROR_RECORD)
	    errp = xmlNewDocNode(docp, NULL, (const xmlChar *) "errors", NULL);
	if (opts->co_errors != SLAX_ERROR_RECORD || errp)
	    slaxCatchErrors(opts->co_errors, errp);
    }

    if (streq(opts->co_format, "name")) {
	for (cp = raw_data; *cp; cp = ep + 1) {
	    sp = strchr(cp, '=');
	    if (sp == NULL)
		break;
	    ep = strchr(sp, '\n');
	    if (ep == NULL)
		ep = sp + strlen(sp); /* Point at trailing NUL */

	    if (sp - cp >= nbufsiz) {
		nbufsiz = sp - cp + 1;
		nbufsiz += BUFSIZ - 1;
		nbufsiz &= ~(BUFSIZ - 1);
		nbuf = alloca(nbufsiz);
	    }
	    assert(nbuf);
	    memcpy(nbuf, cp, sp - cp);
	    nbuf[sp - cp] = '\0';

	    sp += 1;
	    if (ep - sp >= vbufsiz) {
		vbufsiz = ep - sp + 1;
		vbufsiz += BUFSIZ - 1;
		vbufsiz &= ~(BUFSIZ - 1);
		vbuf = alloca(vbufsiz);
	    }
	    memcpy(vbuf, sp, ep - sp);
	    vbuf[ep - sp] = '\0';

	    xmlNodePtr xp = xmlAddChildContent(docp, nodep,
				   (const xmlChar *) "name",
				   (const xmlChar *) vbuf);
	    if (xp)
		xmlSetProp(xp, (const xmlChar *) "name",
			   (const xmlChar *) nbuf);

	    if (*ep == '\0')
		break;		/* Last one (end of data) */
	}

    } else if (streq(opts->co_format, "url-encoded")
	       || streq(opts->co_format, "urlencoded")) {
	for (cp = raw_data; *cp; cp = ep + 1) {
	    sp = strchr(cp, '=');
	    if (sp == NULL)
		break;
	    ep = strchr(sp, '&');
	    if (ep == NULL)
		ep = sp + strlen(sp); /* Point at trailing NUL */

	    if (sp - cp >= nbufsiz) {
		nbufsiz = sp - cp + 1;
		nbufsiz += BUFSIZ - 1;
		nbufsiz &= ~(BUFSIZ - 1);
		nbuf = alloca(nbufsiz);
	    }
	    memcpy(nbuf, cp, sp - cp);
	    nbuf[sp - cp] = '\0';

	    sp += 1;
	    if (ep - sp >= vbufsiz) {
		vbufsiz = ep - sp + 1;
		vbufsiz += BUFSIZ - 1;
		vbufsiz &= ~(BUFSIZ - 1);
		vbuf = alloca(vbufsiz);
	    }
	    assert(vbuf);
	    memcpy(vbuf, sp, ep - sp);
	    vbuf[ep - sp] = '\0';

	    xmlNodePtr xp = xmlAddChildContent(docp, nodep,
				   (const xmlChar *) "name",
				   (const xmlChar *) vbuf);
	    if (xp)
		xmlSetProp(xp, (const xmlChar *) "name",
			   (const xmlChar *) nbuf);

	    if (*ep == '\0')
		break;		/* Last one (end of data) */
	}

    } else if (streq(opts->co_format, "xml")) {
	xmlDocPtr xmlp;

	xmlp = xmlReadMemory(raw_data, strlen(raw_data), "raw_data", NULL,
			     XML_PARSE_NOENT);
	if (xmlp == NULL)
	    goto bail;

	xmlNodePtr childp = xmlDocGetRootElement(xmlp);
	if (childp) {
	    xmlNodePtr newp = xmlDocCopyNode(childp, docp, 1);
	    if (newp)
		xmlNodePtr addedp = xmlAddChild(nodep, newp);
            if (addedp == NULL)
                xmlFreeNode(newp);
	}

	xmlFreeDoc(xmlp);

    } else if (streq(opts->co_format, "html")) {
	xmlDocPtr xmlp;

	xmlp = htmlReadMemory(raw_data, strlen(raw_data), "raw_data", NULL,
			     XML_PARSE_NOENT);
	if (xmlp == NULL)
	    goto bail;

	xmlNodePtr childp = xmlDocGetRootElement(xmlp);
	if (childp) {
	    xmlNodePtr newp = xmlDocCopyNode(childp, docp, 1);
	    if (newp)
		xmlNodePtr addedp = xmlAddChild(nodep, newp);
            if (addedp == NULL)
                xmlFreeNode(newp);
	}

	xmlFreeDoc(xmlp);
    }

 bail:
    if (opts->co_errors) {
	slaxCatchErrors(SLAX_ERROR_DEFAULT, NULL);
	if (errp) {
	    /*
	     * If we have errors, record them; otherwise free the empty
	     * "errors" node.
	     */
	    if (errp->children) {
		xmlNodePtr addedp = xmlAddChild(parent, errp);
                if (addedp == NULL)
                    xmlFreeNode(errp);
	    } else {
		xmlFreeNode(errp);
            }
	}
    }
}

/*
 * Turn a chain of data strings into XML content
 * @returns the built data string (which is inside a text element)
 */
static const char *
extCurlBuildData (curl_handle_t *curlp UNUSED, xmlDocPtr docp,
		     xmlNodePtr nodep, slax_data_list_t *listp,
		     const char *name)
{
    char *buf, *cp;
    size_t bufsiz = 0;
    slax_data_node_t *dnp;
    xmlNodePtr tp, xp;

    bufsiz = 0;
    SLAXDATALIST_FOREACH(dnp, listp) {
	bufsiz += dnp->dn_len;
    }

    if (bufsiz == 0)		/* No data */
	return NULL;

    tp = xmlNewText(NULL);
    if (tp == NULL)
	return NULL;

    cp = buf = xmlMalloc(bufsiz + 1);
    if (buf == NULL)
	return NULL;

    /* Populate buf with the chain of data */
    SLAXDATALIST_FOREACH(dnp, listp) {
	memcpy(cp, dnp->dn_data, dnp->dn_len);
	cp += dnp->dn_len;
    }
    *cp = '\0';			/* NUL terminate content */

    tp->content = (xmlChar *) buf;

    xp = xmlNewDocNode(docp, NULL, (const xmlChar *) name, NULL);
    if (xp) {
	xmlNodePtr addedp = xmlAddChild(nodep, xp);
        if (addedp == NULL) {
            xmlFreeNode(xp);
        } else {
	    xmlNodePtr addedp1 =  xmlAddChild(addedp, tp);
            if (addedp1 == NULL)
                xmlFreeNode(tp);
        }
    }

    return buf;
}

/*
 * Turn the raw header value into a set of real XML tags:
      <header>
        <version>HTTP/1.1</version>
        <code>404</code>
        <message>Not Found</message>
        <field name="Content-Type">text/html</field>
        <field name="Content-Length">345</field>
        <field name="Date">Mon, 08 Aug 2011 03:40:21 GMT</field>
        <field name="Server">lighttpd/1.4.28 juisebox</field>
      </header>
 */
static void
extCurlBuildReplyHeaders (curl_handle_t *curlp, xmlDocPtr docp,
			      xmlNodePtr parent)
{
    if (curlp->ch_reply_headers.tqh_first == NULL)
	return;

    xmlNodePtr nodep = xmlNewDocNode(docp, NULL,
			      (const xmlChar *) "headers", NULL);
    if (nodep == NULL)
	return;

    xmlNodePtr addedp = xmlAddChild(parent, nodep);
    if (addedp == NULL) {
        xmlFreeNode(nodep);
        return;
    }

    int count = 0;
    size_t len;
    char *cp, *sp, *ep;
    slax_data_node_t *dnp;

    SLAXDATALIST_FOREACH(dnp, &curlp->ch_reply_headers) {
	len = dnp->dn_len;
	if (len > 0 && dnp->dn_data[len - 1] == '\n')
	    len -= 1;		/* Drop trailing newlines */
	if (len > 0 && dnp->dn_data[len - 1] == '\r')
	    len -= 1;		  /* Drop trailing returns */
	dnp->dn_data[len] = '\0'; /* NUL terminate it */

	cp = dnp->dn_data;
	ep = cp + len;

	if (count++ == 0) {
	    /*
	     * The first "header" is the HTTP response code.  This is
	     * formatted as "HTTP/v.v xxx message" so we must pull apart
	     * the value into distinct fields.
	     */
	    sp = memchr(cp, ' ', ep - cp);
	    if (sp == NULL)
		continue;

	    *sp++ = '\0';
	    xmlNodePtr addedp1 = xmlAddChildContent(docp, addedp, (const xmlChar *) "version",
			       (const xmlChar *) cp);
            if (addedp1 == NULL) {
                xmlFreeNode(addedp);
                return;
            }

	    cp = sp;
	    sp = memchr(cp, ' ', ep - cp);
	    if (sp == NULL)
		continue;

	    *sp++ = '\0';
	    addedp1 = xmlAddChildContent(docp, addedp, (const xmlChar *) "code",
			       (const xmlChar *) cp);
            if (addedp1 == NULL) {
                xmlFreeNode(addedp);
                return;
            }

	    curlp->ch_code = strtoul(cp, NULL, 0);

	    if (*sp) {
		addedp1 = xmlAddChildContent(docp, addedp,
				   (const xmlChar *) "message",
				   (const xmlChar *) sp);
                if (addedp1 == NULL) {
                    xmlFreeNode(addedp);
                    return;
                }
            }

	} else {
	    /* Subsequent headers are normal "field: value" lines */
	    sp = memchr(cp, ':', ep - cp);
	    if (sp == NULL)
		continue;

	    *sp++ = '\0';
	    while (*sp == ' ')
		sp += 1;
	    xmlNodePtr xp = xmlAddChildContent(docp, nodep,
				   (const xmlChar *) "header",
				   (const xmlChar *) sp);
	    if (xp)
		xmlSetProp(xp, (const xmlChar *) "name", (const xmlChar *) cp);
	}
    }
}

/*
 * Build the results XML hierarchy, using the headers and data
 * returned from the server.
 *
 * NOTE: libcurl defines success as success at the libcurl layer, not
 * success at the protocol level.  This means that "something was
 * transfered" which doesn't really mean "success".  An error will
 * often still result in data being transfered, such as a "404 not
 * found" giving some html data to display to the user.  So you
 * typically don't want to test for success, but look at the
 * header/code value:
 *
 *    if (curl-success && header/code && header/code < 400) { ... }
 */
static xmlNodePtr 
extCurlBuildResults (xmlDocPtr docp, curl_handle_t *curlp,
			curl_opts_t *opts, CURLcode success)
{
    const char *raw_data;
    xmlNodePtr nodep = xmlNewDocNode(docp, NULL,
			      (const xmlChar *) "results", NULL);

    xmlNodePtr addedp = xmlAddChildContent(docp, nodep, (const xmlChar *) "url",
		       (const xmlChar *) opts->co_url);
    if (addedp == NULL) {
        xmlFreeNode(nodep);
        return NULL;
    }

    if (success == 0) {
	xmlNodePtr xp = xmlNewDocNode(docp, NULL,
				      (const xmlChar *) "curl-success", NULL);

	if (xp) {
	   xmlNodePtr addedp1 = xmlAddChild(addedp, xp);
           if (addedp1 == NULL)
               xmlFreeNode(xp);
               return NULL;
        }

	/* Add header information, raw and cooked */
	extCurlBuildData(curlp, docp, addedp1,
			    &curlp->ch_reply_headers, "raw-headers");
	extCurlBuildReplyHeaders(curlp, docp, addedp1);

	/* Add raw data string */
	raw_data = extCurlBuildData(curlp, docp, addedp1,
				       &curlp->ch_reply_data, "raw-data");

	if (raw_data && opts->co_format && curlp->ch_code < 300)
	    extCurlBuildDataParsed(curlp, opts, docp, addedp1, raw_data);

    } else {
	xmlNodePtr addedp2 = xmlAddChildContent(docp, addedp1, (const xmlChar *) "error",
		       (const xmlChar *) curlp->ch_error);
        if (addedp2 == NULL) {
            xmlFreeNode(addedp2);
        }
    }

    return nodep;
}

/*
 * Parse a set of option values and store them in an options structure
 */
static void
extCurlOptionsParse (curl_handle_t *curlp UNUSED, curl_opts_t *opts,
			xmlXPathObject *ostack[], int nargs)
{
    int osi;

    for (osi = nargs - 1; osi >= 0; osi--) {
	xmlXPathObject *xop = ostack[osi];

	if (xop->stringval) {
	    if (opts->co_url)
		xmlFree(opts->co_url);
	    opts->co_url = (char *) xmlStrdup(xop->stringval);

	} else if (xop->nodesetval) {
	    xmlNodeSetPtr nodeset;
	    xmlNodePtr nop, cop;
	    int i;

	    nodeset = xop->nodesetval;
	    for (i = 0; i < nodeset->nodeNr; i++) {
		nop = nodeset->nodeTab[i];

		if (nop->type == XML_ELEMENT_NODE)
		    extCurlParseNode(opts, nop);

		if (nop->children == NULL)
		    continue;

		for (cop = nop->children; cop; cop = cop->next) {
		    if (cop->type != XML_ELEMENT_NODE)
			continue;

		    extCurlParseNode(opts, cop);
		}
	    }
	}
    }
}

/*
 * Set parameters for a curl handle
 * Usage:
      var $handle = curl:open();
      expr curl:set($handle, $opts, $more-opts);
 */
static void
extCurlSet (xmlXPathParserContext *ctxt, int nargs)
{
    xmlXPathObject *ostack[nargs];	/* Stack for objects */
    curl_handle_t *curlp;
    char *name;
    int osi;

    if (nargs < 1) {
	LX_ERR("curl:execute: too few arguments error\n");
	return;
    }

    for (osi = nargs - 1; osi >= 0; osi--)
	ostack[osi] = valuePop(ctxt);

    name = (char *) xmlXPathCastToString(ostack[0]);
    if (name == NULL) {
	LX_ERR("curl:set: no argument\n");
	goto fail;
    }

    curlp = extCurlHandleFind(name);
    if (curlp == NULL) {
	slaxLog("curl:set: unknown handle: %s", name);
	xmlFree(name);
	goto fail;
    }

    /*
     * The zeroeth element of the ostack is the handle name, so we
     * have skip over it.
     */
    extCurlOptionsParse(curlp, &curlp->ch_opts, ostack + 1, nargs - 1);

    xmlXPathReturnString(ctxt, xmlStrdup((const xmlChar *) ""));

 fail:
    for (osi = nargs - 1; osi >= 0; osi--)
	if (ostack[osi])
	    xmlXPathFreeObject(ostack[osi]);
}

/*
 * Main curl entry point for transfering files.
 * Usage:
      var $handle = curl:open();
      var $res = curl:perform($handle, $url, $opts, $more-opts);
 */
static void
extCurlPerform (xmlXPathParserContext *ctxt, int nargs)
{
    xmlXPathObject *ostack[nargs];	/* Stack for objects */
    curl_handle_t *curlp;
    char *name;
    CURLcode success;
    int osi;
    xmlNodePtr nodep;
    xmlXPathObjectPtr ret;
    xmlDocPtr container;
    curl_opts_t co;

    if (nargs < 1) {
	LX_ERR("curl:execute: too few arguments error\n");
	return;
    }

    for (osi = nargs - 1; osi >= 0; osi--)
	ostack[osi] = valuePop(ctxt);

    name = (char *) xmlXPathCastToString(ostack[0]);
    if (name == NULL) {
	LX_ERR("curl:execute: no argument\n");
	return;
    }

    curlp = extCurlHandleFind(name);
    if (curlp == NULL) {
	slaxLog("curl:execute: unknown handle: %s", name);
	xmlFree(name);
	goto fail;
    }

    /*
     * We make a local copy of our options that the parameters will
     * affect, but won't be saved.
     */
    extCurlOptionsCopy(&co, &curlp->ch_opts);

    extCurlOptionsParse(curlp, &co, ostack + 1, nargs - 1);

    success = extCurlDoPerform(curlp, &co);

    /*
     * Create a Result Value Tree container, and register it with RVT garbage
     * collector.
     */
    container = slaxMakeRtf(ctxt);
    if (container == NULL)
	goto fail;

    nodep = extCurlBuildResults(container, curlp, &co, success);

    xmlNodePtr addedp = xmlAddChild((xmlNodePtr) container, nodep);
    if (addedp != NULL) {
        xmlNodeSet *results = xmlXPathNodeSetCreate(NULL);
        xmlXPathNodeSetAdd(results, nodep);
        ret = xmlXPathNewNodeSetList(results);

        extCurlOptionsRelease(&co);
        extCurlHandleClean(curlp);

        valuePush(ctxt, ret);
        xmlXPathFreeNodeSet(results);
    } else {
        xmlFreeNode(newp);
        xmlXPathFreeNodeSet(NULL);
    }

 fail:
    for (osi = nargs - 1; osi >= 0; osi--)
	if (ostack[osi])
	    xmlXPathFreeObject(ostack[osi]);
}

static void
extCurlSingle (xmlXPathParserContext *ctxt UNUSED, int nargs UNUSED)
{
    xmlXPathObject *ostack[nargs];	/* Stack for objects */
    curl_handle_t *curlp;
    CURLcode success;
    int osi;
    xmlNodePtr nodep;
    xmlXPathObjectPtr ret;
    xmlDocPtr container;

    if (nargs < 1) {
	LX_ERR("curl:execute: too few arguments error\n");
	return;
    }

    for (osi = nargs - 1; osi >= 0; osi--)
	ostack[osi] = valuePop(ctxt);

    curlp = extCurlHandleAlloc();
    if (curlp == NULL) {
	slaxLog("curl:fetch: alloc failed");
	goto fail;
    }

    extCurlOptionsParse(curlp, &curlp->ch_opts, ostack, nargs);

    success = extCurlDoPerform(curlp, &curlp->ch_opts);

    /*
     * Create a Result Value Tree container, and register it with RVT garbage
     * collector.
     */
    container = slaxMakeRtf(ctxt);
    if (container == NULL)
	goto fail;

    nodep = extCurlBuildResults(container, curlp, &curlp->ch_opts, success);

    xmlNodePtr addedp = xmlAddChild((xmlNodePtr) container, nodep);
    if (addedp != NULL) {
        xmlNodeSet *results = xmlXPathNodeSetCreate(NULL);
        xmlXPathNodeSetAdd(results, nodep);
        ret = xmlXPathNewNodeSetList(results);

        extCurlHandleFree(curlp);

        valuePush(ctxt, ret);
        xmlXPathFreeNodeSet(results);
    } else {
        xmlFreeNode(nodep);
        xmlXPathFreeNodeSet(NULL);
    }

 fail:
    for (osi = nargs - 1; osi >= 0; osi--)
	if (ostack[osi])
	    xmlXPathFreeObject(ostack[osi]);
}

slax_function_table_t slaxCurlTable[] = {
    {
	"close", extCurlClose,
	"Close a CURL handle",
	"(handle)", XPATH_UNDEFINED,
    },
    {
	"perform", extCurlPerform,
	"Perform a CURL transfer",
	"(handle, options*)", XPATH_XSLT_TREE,
    },
    {
	"single", extCurlSingle,
	"Perform a CURL transfer",
	"(options+)", XPATH_XSLT_TREE,
    },
    {
	"open", extCurlOpen,
	"Open a CURL handle",
	"()", XPATH_NODESET,
    },
    {
	"set", extCurlSet,
	"Set persistent options on a CURL handle",
	"(handle, options+)", XPATH_UNDEFINED,
    },
    { NULL, NULL, NULL, NULL, XPATH_UNDEFINED }
};

void
extCurlInit (void)
{
    TAILQ_INIT(&extCurlSessions);

    slaxRegisterFunctionTable(CURL_FULL_NS, slaxCurlTable);
}

SLAX_DYN_FUNC(slaxDynLibInit)
{
    TAILQ_INIT(&extCurlSessions);

    arg->da_functions = slaxCurlTable; /* Fill in our function table */

    return SLAX_DYN_VERSION;
}
