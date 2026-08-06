AM_CPPFLAGS = -I$(top_builddir) \
	      -I$(top_srcdir)/bxslt -I$(top_srcdir)/bxslt/libxslt -I$(top_srcdir)/bxslt/libexslt \
	      -I$(top_builddir)/bxslt -I$(top_builddir)/bxslt/libxslt \
	      -I$(top_builddir)/bxslt/libexslt \
	      -I$(top_builddir)/bxml/include -I$(top_srcdir)/bxml/include

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
libexslt_la_LIBADD = $(top_builddir)/bxslt/libxslt/libxslt.la $(top_builddir)/bxml/libxml2.la $(EXTRA_LIBS) $(LIBGCRYPT_LIBS) $(LIBM)
libexslt_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined \
		      -version-info $(LIBEXSLT_VERSION_INFO)

man_MANS = libexslt.3

EXTRA_DIST = $(man_MANS)
