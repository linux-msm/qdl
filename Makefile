OUT := qdl

CFLAGS := -O2 -Wall -g `pkg-config --cflags libxml-2.0`
LDFLAGS := `pkg-config --libs libxml-2.0 libudev`
prefix := /usr/local

SRCS := firehose.c qdl.c sahara.c util.c patch.c program.c ufs.c
OBJS := $(SRCS:.c=.o)

KS_OUT := ks
KS_SRCS := ks.c sahara.c util.c
KS_OBJS := $(KS_SRCS:.c=.o)

default: $(OUT) $(KS_OUT)

$(OUT): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(KS_OUT): $(KS_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OUT) $(OBJS)
	rm -f $(KS_OUT) $(KS_OBJS)

install: $(OUT) $(KS_OUT)
	install -d $(DESTDIR)$(prefix)/bin
	install -m 755 $^ $(DESTDIR)$(prefix)/bin
