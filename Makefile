OUT := qdl

CFLAGS := -O2 -Wall -g `xml2-config --cflags` -I libsparse/include
LDFLAGS := `xml2-config --libs` -ludev -lz
prefix := /usr/local

SRCS := firehose.c qdl.c sahara.c util.c patch.c program.c ufs.c
OBJS := $(SRCS:.c=.o)

$(OUT): $(OBJS) libsparse/libsparse.a
	$(CXX) -o $@ $^ $(LDFLAGS)

libsparse/libsparse.a:
	$(MAKE) -C libsparse

clean:
	rm -f $(OUT) $(OBJS)
	$(MAKE) -C libsparse clean

install: $(OUT)
	install -D -m 755 $< $(DESTDIR)$(prefix)/bin/$<
