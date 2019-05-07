OUT := qdl
NBDKIT := nbdkit-qdl-plugin.so

CFLAGS := -O2 -Wall -g `xml2-config --cflags` -fPIC
LDFLAGS := `xml2-config --libs` -ludev
prefix := /usr/local

COMMON_SRCS := firehose.c json.c qdl.c sahara.c util.c patch.c program.c ufs.c usb.c

QDL_SRCS := qdl.c
QDL_OBJS := $(COMMON_SRCS:.c=.o) $(QDL_SRCS:.c=.o)

$(OUT): $(QDL_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

NBDKIT_SRCS := nbdkit-qdl-plugin.c
NBDKIT_OBJS := $(COMMON_SRCS:.c=.o) $(NBDKIT_SRCS:.c=.o)

$(NBDKIT): LDFLAGS += -shared
$(NBDKIT): $(NBDKIT_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY: lib
lib: $(NBDKIT)

.PHONY: all
all: $(OUT) $(NBDKIT)

clean:
	rm -f $(OUT) $(QDL_OBJS) $(NBDKIT_OBJS)

install: $(OUT)
	install -D -m 755 $< $(DESTDIR)$(prefix)/bin/$<
