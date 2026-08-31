#
# Link-time optimization (LTO) support.
#
# ENABLE_LTO defaults to the value chosen at configure time
# (--enable-lto, off by default), but -- unlike an AM_CONDITIONAL,
# which is resolved once and baked into the generated Makefile --
# this stays a live make variable, so it can be overridden per
# invocation for testing without reconfiguring:
#
#   make ENABLE_LTO=yes clean all
#
ENABLE_LTO ?= @ENABLE_LTO@

ifeq "${ENABLE_LTO}" "yes"
LTO_CFLAGS = -flto
LTO_LDFLAGS = -flto
# Final-binary-only: tells libtool to prefer the .a member of any .la
# dependency it links against instead of the .dylib/.so, since LTO
# can't inline across a dynamic-library boundary. Only meaningful on
# a bin_PROGRAMS' own *_LDFLAGS, not on a lib_LTLIBRARIES.
#
# -L/usr/lib is pinned ahead of everything else because pulling a
# dependency's real object code (instead of its pre-linked .dylib)
# into this link can expose an -L search-order footgun: if the
# target also searches /opt/local/lib (e.g. -enable-readline), that
# directory's own libiconv.dylib shadows the system one and it only
# exports GNU-style libiconv/libiconv_open/libiconv_close symbols,
# not the plain iconv/iconv_open/iconv_close names this code calls.
LTO_STATIC_LDFLAGS = -static -L/usr/lib
else
LTO_CFLAGS =
LTO_LDFLAGS =
LTO_STATIC_LDFLAGS =
endif
