# Makefile for libbxml python library

# We use a rule with multiple output files which creates problems with
# parallel builds.
.NOTPARALLEL:

SUBDIRS = . tests

EXTRA_DIST = 			\
	generator.py		\
	libbxml.py		\
	pyproject.toml		\
	meson.build

if WITH_PYTHON
AM_CPPFLAGS = \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/include	\
	$(PYTHON_CFLAGS)

pyexec_LTLIBRARIES = libbxmlmod.la

libbxmlmod_la_SOURCES = libxml.c libxml_wrap.h types.c
nodist_libbxmlmod_la_SOURCES = libbxml-py.h libbxml-py.c
libbxmlmod_la_LDFLAGS = $(AM_LDFLAGS) $(PYTHON_LDFLAGS) -module -avoid-version
libbxmlmod_la_LIBADD = $(top_builddir)/libbxml.la $(PYTHON_LIBS)

BUILT_SOURCES = libbxml-export.c libbxml-py.h libbxml-py.c

python_PYTHON = drv_libbxml.py
nodist_python_PYTHON = libbxml.py

API_DESC = ../doc/html.stamp
GENERATED = libbxml.py $(BUILT_SOURCES)
CLEANFILES = $(GENERATED)

all-local: libbxml.py

$(GENERATED): $(srcdir)/generator.py $(API_DESC)
	$(PYTHON) $(srcdir)/generator.py $(builddir)

# libxml.c #includes libbxml-export.c
libxml.$(OBJEXT): libbxml-export.c

clean-local:
	rm -rf __pycache__ *.pyc

endif
