#
# New ports collection makefile for libslax
#

PORTNAME=	libslax
PORTVERSION=	0.22.0
PORTREVISION?=	1
CATEGORIES?=	textproc
MASTER_SITES=	https://github.com/Juniper/libslax/releases/download/0.22.0/
DIST_SUBDIR=	textproc

MAINTAINER?=	juise@googlegroups.com
COMMENT?=	The SLAX language (XSLT alternative)

FETCH_ARGS?=    -Fpr

.if !defined(REFERENCE_PORT)

USE_GMAKE=	yes
GNU_CONFIGURE=	yes
USE_LDCONFIG=	yes
USE_GNOME?=	libxml2 libxslt
CONFIGURE_ARGS?= 

CPPFLAGS+=	-I${LOCALBASE}/include
LDFLAGS+=	-L${LOCALBASE}/lib

.if defined(MASTERDIR)
_SLAVE_PORT=	yes
.else
MAN3=		libslax.3x
MAN1=		slaxproc.1x

OPTIONS=	DEBUG "Enable debugging" off \
		WARNINGS "Enable compile warnings (-Werror)" off
.endif

.include <bsd.port.pre.mk>

.if defined(WITH_DEBUG)
CONFIGURE_ARGS+=	--enable-debug
.endif

.if defined(WITH_WARNINGS)
CONFIGURE_ARGS+=--enable-warnings
.endif

.include <bsd.port.post.mk>

.endif
