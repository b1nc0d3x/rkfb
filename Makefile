KMOD=	rkfb
SRCS=	rkfb.c
SRCS+=	device_if.h
SRCS+=	bus_if.h

.include <bsd.kmod.mk>
