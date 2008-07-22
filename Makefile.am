INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/libxslt -I$(top_srcdir)/libslax \
	   -I$(top_builddir) -I$(top_builddir)/libxslt \
	   -I$(top_builddir)/libslax $(LIBXML_CFLAGS) $(CFLAGS)

AM_CFLAGS = $(LIBGCRYPT_CFLAGS)

lib_LTLIBRARIES = libslax.la

exsltincdir = $(includedir)/libslax

exsltinc_HEADERS = \
	slax.h \
	slaxinternals.h

libslax_la_SOURCES = \
	slaxparser.c \
	slaxloader.c \
	slaxstring.c \
	slaxwriter.c

MY_YACC = ${BISON} -kvd -b slaxparser

slaxparser.h: slaxparser.c
slaxloader.o slaxwriter.o: slaxparser.h

slaxparser.c: slaxparser-out.y
	${MY_YACC} -o slaxparser.c slaxparser-out.y
	@mv $@ $@.old
	sed 's:int yyparse (void);::' < $@.old  > $@
	@rm $@.old

#
# See the comment at the top of slaxparser-xp.y for details
#
SLAXPIECES = \
	slaxparser.y \
	slaxparser-xp.y \
	slaxparser-xpl.y \
	slaxparser-tail.y

slaxparser-out.y: ${SLAXPIECES}
	cat $> > $@

slaxparser-xpl.y: slaxparser-xp.y
	sed 's/xp_/xpl_/g' < $slaxparser-xp.y > slaxparser-xpl.y

clean-slaxparser:
	${RM} -f slaxparser-xpl.y slaxparser-out.y

libslax_la_LIBADD = $(top_builddir)/libxslt/libxslt.la $(EXTRA_LIBS)
libslax_la_LDFLAGS = -version-info @LIBSLAX_VERSION_INFO@ $(LIBGCRYPT_LIBS)

man_MANS = libslax.3

EXTRA_DIST = $(man_MANS)
