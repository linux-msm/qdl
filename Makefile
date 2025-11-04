VERSION := $(or $(VERSION), $(shell git describe --dirty --always --tags 2>/dev/null), "unknown-version")

CFLAGS += -Wall `pkg-config --cflags libxml-2.0 libusb-1.0`
LDFLAGS += `pkg-config --libs libxml-2.0 libusb-1.0`
ifeq ($(OS),Windows_NT)
LDFLAGS += -lws2_32
endif
prefix := /usr/local

# Default build type
BUILD_TYPE ?= debug
BUILD_DIR = build/$(BUILD_TYPE)
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin
GEN_DIR = $(BUILD_DIR)/gen

QDL := qdl
QDL_SRCS := firehose.c io.c qdl.c sahara.c util.c patch.c program.c read.c sha2.c sim.c ufs.c usb.c ux.c oscompat.c vip.c sparse.c gpt.c
QDL_OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(QDL_SRCS:.c=.o)))

RAMDUMP := qdl-ramdump
RAMDUMP_SRCS := ramdump.c sahara.c io.c sim.c usb.c util.c ux.c oscompat.c
RAMDUMP_OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(RAMDUMP_SRCS:.c=.o)))

KS_OUT := ks
KS_SRCS := ks.c sahara.c util.c ux.c oscompat.c
KS_OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(KS_SRCS:.c=.o)))

CHECKPATCH_SOURCES := $(shell find . -type f \( -name "*.c" -o -name "*.h" -o -name "*.sh" \) ! -name "sha2.c" ! -name "sha2.h" ! -name "*version.h" ! -name "list.h")
CHECKPATCH_ROOT := https://raw.githubusercontent.com/torvalds/linux/v6.15/scripts
CHECKPATCH_URL := $(CHECKPATCH_ROOT)/checkpatch.pl
CHECKPATCH_SP_URL := $(CHECKPATCH_ROOT)/spelling.txt
CHECKPATCH := ./.scripts/checkpatch.pl
CHECKPATCH_SP := ./.scripts/spelling.txt

default: debug
all: debug release

release:
	$(MAKE) BUILD_TYPE=release CFLAGS='$(CFLAGS) -O2 -Ibuild/release/gen' _all_internal

debug:
	$(MAKE) BUILD_TYPE=debug CFLAGS='$(CFLAGS) -O0 -g -Ibuild/debug/gen' _all_internal

# Inner target that actually builds things with the chosen BUILD_TYPE
_all_internal: dirs $(BIN_DIR)/$(QDL) $(BIN_DIR)/$(RAMDUMP) $(BIN_DIR)/$(KS_OUT)

dirs:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR) $(GEN_DIR)

$(OBJ_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/$(QDL): $(QDL_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(BIN_DIR)/$(RAMDUMP): $(RAMDUMP_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(BIN_DIR)/$(KS_OUT): $(KS_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

compile_commands.json: $(QDL_SRCS) $(KS_SRCS)
	@echo -n $^ | jq -snR "[inputs|split(\" \")[]|{directory:\"$(PWD)\", command: \"$(CC) $(CFLAGS) -c \(.)\", file:.}]" > $@

$(GEN_DIR)/version.h:
	@echo "#define VERSION \"$(VERSION)\"" > $(GEN_DIR)/.version.h
	@cmp -s $(GEN_DIR)/.version.h $(GEN_DIR)/version.h || cp $(GEN_DIR)/.version.h $(GEN_DIR)/version.h

$(OBJ_DIR)/util.o: $(GEN_DIR)/version.h

clean:
	rm -rf build
	rm -f compile_commands.json
	rm -f version.h .version.h
	rm -f $(CHECKPATCH)
	rm -f $(CHECKPATCH_SP)
	if [ -d .scripts ]; then rmdir .scripts; fi

install: release
	install -d $(DESTDIR)$(prefix)/bin
	install -m 755 $(BIN_DIR)/$(QDL) $(BIN_DIR)/$(RAMDUMP) $(BIN_DIR)/$(KS_OUT) $(DESTDIR)$(prefix)/bin

tests: debug
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
