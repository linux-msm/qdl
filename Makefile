QDL := qdl
RAMDUMP := qdl-ramdump
VERSION := $(or $(VERSION), $(shell git describe --dirty --always --tags 2>/dev/null), "unknown-version")

PKG_CONFIG ?= pkg-config
CFLAGS += -O2 -Wall -g `$(PKG_CONFIG) --cflags libxml-2.0 libusb-1.0`
LDFLAGS += `$(PKG_CONFIG) --libs libxml-2.0 libusb-1.0`
ifeq ($(OS),Windows_NT)
LDFLAGS += -lws2_32
endif
prefix := /usr/local

QDL_SRCS := firehose.c io.c qdl.c sahara.c util.c patch.c program.c read.c sha2.c sim.c ufs.c usb.c ux.c oscompat.c vip.c sparse.c gpt.c
QDL_OBJS := $(QDL_SRCS:.c=.o)

RAMDUMP_SRCS := ramdump.c sahara.c io.c sim.c usb.c util.c ux.c oscompat.c
RAMDUMP_OBJS := $(RAMDUMP_SRCS:.c=.o)

KS_OUT := ks
KS_SRCS := ks.c sahara.c util.c ux.c oscompat.c
KS_OBJS := $(KS_SRCS:.c=.o)

CHECKPATCH_SOURCES := $(shell find . -type f \( -name "*.c" -o -name "*.h" -o -name "*.sh" \) ! -name "sha2.c" ! -name "sha2.h" ! -name "*version.h" ! -name "list.h")
CHECKPATCH_ROOT := https://raw.githubusercontent.com/torvalds/linux/v6.15/scripts
CHECKPATCH_URL := $(CHECKPATCH_ROOT)/checkpatch.pl
CHECKPATCH_SP_URL := $(CHECKPATCH_ROOT)/spelling.txt
CHECKPATCH := ./.scripts/checkpatch.pl
CHECKPATCH_SP := ./.scripts/spelling.txt

MANPAGES := ks.1 qdl-ramdump.1 qdl.1

default: $(QDL) $(RAMDUMP) $(KS_OUT)

$(QDL): $(QDL_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(RAMDUMP): $(RAMDUMP_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(KS_OUT): $(KS_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

compile_commands.json: $(QDL_SRCS) $(KS_SRCS)
	@echo -n $^ | jq -snR "[inputs|split(\" \")[]|{directory:\"$(PWD)\", command: \"$(CC) $(CFLAGS) -c \(.)\", file:.}]" > $@

manpages: $(KS_OUT) $(RAMDUMP) $(QDL)
	help2man -N -n "KS" -o ks.1 ./ks
	help2man -N -n "Qualcomm Download" -o qdl.1 ./qdl
	help2man -N -n "Qualcomm Download Ramdump" -o qdl-ramdump.1 ./qdl-ramdump

version.h::
	@echo "#define VERSION \"$(VERSION)\"" > .version.h
	@cmp -s .version.h version.h || cp .version.h version.h

util.o: version.h

clean:
	rm -f $(QDL) $(QDL_OBJS)
	rm -f $(RAMDUMP) $(RAMDUMP_OBJS)
	rm -f $(KS_OUT) $(KS_OBJS)
	rm -f $(MANPAGES)
	rm -f compile_commands.json
	rm -f version.h .version.h
	rm -f $(CHECKPATCH)
	rm -f $(CHECKPATCH_SP)
	if [ -d .scripts ]; then rmdir .scripts; fi

install: $(QDL) $(RAMDUMP) $(KS_OUT)
	install -d $(DESTDIR)$(prefix)/bin
	install -m 755 $^ $(DESTDIR)$(prefix)/bin

tests: default
tests:
	@./tests/run_tests.sh

# Target to download checkpatch.pl if not present
$(CHECKPATCH):
	@echo "Downloading checkpatch.pl..."
	@mkdir -p $(dir $(CHECKPATCH))
	@curl -sSfL $(CHECKPATCH_URL) -o $(CHECKPATCH)
	@curl -sSfL $(CHECKPATCH_SP_URL) -o $(CHECKPATCH_SP)
	@chmod +x $(CHECKPATCH)

check: $(CHECKPATCH)
	@echo "Running checkpatch on source files (excluding sha2.c and sha2.h)..."
	@for file in $(CHECKPATCH_SOURCES); do \
		perl $(CHECKPATCH) --no-tree -f $$file || exit 1; \
	done

check-cached: $(CHECKPATCH)
	@echo "Running checkpatch on staged changes..."
	@git diff --cached -- . | perl $(CHECKPATCH) --no-tree -
