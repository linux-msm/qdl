OUT := qdl

CFLAGS := -O2 -Wall -g `pkg-config --cflags libxml-2.0` -I/usr/include/android
LDFLAGS := `pkg-config --libs libxml-2.0 libudev` -L/usr/lib/x86_64-linux-gnu/android -Wl,-rpath=/usr/lib/x86_64-linux-gnu/android -lsparse
prefix := /usr/local

SRCS := firehose.c qdl.c sahara.c util.c patch.c program.c ufs.c
OBJS := $(SRCS:.c=.o)

$(OUT): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OUT) $(OBJS)

install: $(OUT)
	install -D -m 755 $< $(DESTDIR)$(prefix)/bin/$<
