AM_CPPFLAGS = \
    -I$(top_builddir) \
    -I$(top_srcdir)/libbxslt \
    -I$(top_srcdir)/libbxslt/libxslt \
    -I$(top_srcdir)/libbxslt/libexslt \
    -I$(top_builddir)/libbxslt \
    -I$(top_builddir)/libbxslt/libxslt \
    -I$(top_builddir)/libbxslt/libexslt \
    -I$(top_builddir)/libbxml/include \
    -I$(top_srcdir)/libbxml/include

lib_LTLIBRARIES = libexslt.la

exsltincdir = $(includedir)/libexslt

exsltinc_HEADERS =                      \
	exslt.h				\
	exsltexports.h
nodist_exsltinc_HEADERS =               \
	exsltconfig.h

libexslt_la_SOURCES =                   \
	exslt.c				\
	common.c			\
	crypto.c			\
	math.c				\
	sets.c				\
	functions.c			\
	strings.c			\
	date.c				\
	saxon.c				\
	libexslt.h			\
	dynamic.c

libexslt_la_CFLAGS = $(AM_CFLAGS) $(LIBGCRYPT_CFLAGS)

libexslt_la_LIBADD = \
    $(top_builddir)/libbxslt/libxslt/libbxslt.la \
    $(top_builddir)/libbxml/libbxml.la \
    $(EXTRA_LIBS) $(LIBGCRYPT_LIBS) $(LIBM)

libexslt_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined \
		      -version-info $(LIBEXSLT_VERSION_INFO)

man_MANS = libexslt.3

EXTRA_DIST = $(man_MANS)
