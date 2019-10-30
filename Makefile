OUT := qdl

CFLAGS := -O2 -Wall -g `xml2-config --cflags`
LDFLAGS := `xml2-config --libs` -ludev
prefix := /usr/local

SRCS := firehose.c qdl.c sahara.c util.c patch.c program.c ufs.c sparse_read.c sparse.c output_stream.c backed_block.c sparse_err.c
OBJS := $(SRCS:.c=.o)

$(OUT): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OUT) $(OBJS)

install: $(OUT)
	install -D -m 755 $< $(DESTDIR)$(prefix)/bin/$<
