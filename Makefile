
SRCS = main.c text.c

CFLAGS += -std=gnu99 -fgnu89-inline
CFLAGS += $(shell pkg-config --cflags egl)
CFLAGS += $(shell pkg-config --cflags freetype2)

LDFLAGS += $(shell pkg-config --libs egl)
LDFLAGS += $(shell pkg-config --libs freetype2)

LDFLAGS +=  -lm -lpthread

stos-splash: ${SRCS} Makefile
	${CC} -O2 ${SRCS} ${CFLAGS} -o $@ ${LDFLAGS}
