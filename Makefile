QDL := qdl
RAMDUMP := qdl-ramdump
VERSION := $(or $(VERSION), $(shell git describe --dirty --always --tags 2>/dev/null), "unknown-version")

CFLAGS += -O2 -Wall -g `pkg-config --cflags libxml-2.0 libusb-1.0`
LDFLAGS += `pkg-config --libs libxml-2.0 libusb-1.0`
prefix := /usr/local

QDL_SRCS := firehose.c qdl.c sahara.c util.c patch.c program.c read.c ufs.c usb.c ux.c sparse.c
QDL_OBJS := $(QDL_SRCS:.c=.o)

RAMDUMP_SRCS := ramdump.c sahara.c usb.c util.c ux.c
RAMDUMP_OBJS := $(RAMDUMP_SRCS:.c=.o)

KS_OUT := ks
KS_SRCS := ks.c sahara.c util.c ux.c
KS_OBJS := $(KS_SRCS:.c=.o)

default: $(QDL) $(RAMDUMP) $(KS_OUT)

$(QDL): $(QDL_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(RAMDUMP): $(RAMDUMP_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(KS_OUT): $(KS_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

compile_commands.json: $(QDL_SRCS) $(KS_SRCS)
	@echo -n $^ | jq -snR "[inputs|split(\" \")[]|{directory:\"$(PWD)\", command: \"$(CC) $(CFLAGS) -c \(.)\", file:.}]" > $@

version.h::
	@echo "#define VERSION \"$(VERSION)\"" > .version.h
	@cmp -s .version.h version.h || cp .version.h version.h

util.o: version.h

clean:
	rm -f $(QDL) $(QDL_OBJS)
	rm -f $(RAMDUMP) $(RAMDUMP_OBJS)
	rm -f $(KS_OUT) $(KS_OBJS)
	rm -f compile_commands.json
	rm -f version.h .version.h

install: $(QDL) $(RAMDUMP) $(KS_OUT)
	install -d $(DESTDIR)$(prefix)/bin
	install -m 755 $^ $(DESTDIR)$(prefix)/bin
