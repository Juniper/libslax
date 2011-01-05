#
# $Id$
#
# Commonly used sets of warnings
#

MIN_WARNINGS?= -W -Wall

LOW_WARNINGS?= ${MIN_WARNINGS} \
    -Wstrict-prototypes \
    -Wmissing-prototypes \
    -Wpointer-arith

MEDIUM_WARNINGS?= ${LOW_WARNINGS} -Werror

HIGH_WARNINGS?= ${MEDIUM_WARNINGS} \
    -Waggregate-return \
    -Wcast-align \
    -Wcast-qual \
    -Wchar-subscripts \
    -Wcomment \
    -Wformat \
    -Wimplicit \
    -Wmissing-declarations \
    -Wnested-externs \
    -Wparentheses \
    -Wreturn-type \
    -Wshadow \
    -Wswitch \
    -Wtrigraphs \
    -Wuninitialized \
    -Wunused \
    -Wwrite-strings

HIGHER_WARNINGS?= ${HIGH_WARNINGS} \
    -Winline \
    -Wbad-function-cast \
    -Wpacked \
    -Wpadded \
    -Wstrict-aliasing

WARNINGS = ${HIGH_WARNINGS} -fno-inline-functions-called-once
