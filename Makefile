OUT := qdl

CFLAGS := -O2 -Wall -g `xml2-config --cflags`
LDFLAGS := `xml2-config --libs`

SRCS := firehose.c qdl.c sahara.c util.c patch.c program.c
OBJS := $(SRCS:.c=.o)

$(OUT): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OUT) $(OBJS)
