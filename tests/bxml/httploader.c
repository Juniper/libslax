/*
 * Stand-in "http://" url loader for the test suite.
 *
 * libxml2 dropped its built-in HTTP client in 2.15 (nanohttp.c is now
 * ABI-compat stubs that always return NULL), so SYSTEM ids like
 * "http://example.org/b/b.dtd" in the test/threads fixtures no
 * longer resolve to anything -- they fall through to a literal local
 * open() of that string and fail with ENOENT.
 *
 * This registers an input callback (matched ahead of the plain-file
 * default, per xmlParserInputBufferCreateUrl's reverse-order search)
 * that redirects "http://example.org/<path>" to the local fixture
 * file test/threads/<path>, which is exactly the layout already
 * checked in (a/a.dtd, b/b.dtd, c/c.dtd, bac.dtd, etc).
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <libxml/xmlIO.h>

#define HTTP_TEST_PREFIX "http://example.org/"

static int
httpTestMatch(const char *url) {
    return !strncasecmp(url, HTTP_TEST_PREFIX, strlen(HTTP_TEST_PREFIX));
}

static void *
httpTestOpen(const char *url) {
    char path[1024];

    snprintf(path, sizeof(path), "test/threads/%s",
	     url + strlen(HTTP_TEST_PREFIX));
    return fopen(path, "rb");
}

static int
httpTestRead(void *context, char *buffer, int len) {
    return (int) fread(buffer, 1, len, (FILE *) context);
}

static int
httpTestClose(void *context) {
    return fclose((FILE *) context) == 0 ? 0 : -1;
}

void
httpTestLoaderInit(void) {
    xmlRegisterInputCallbacks(httpTestMatch, httpTestOpen,
			       httpTestRead, httpTestClose);
}
