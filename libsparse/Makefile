OUT := libsparse.a
CXXFLAGS := -O2 -Wall -I include -std=c++11

SRCS := backed_block.cpp output_file.cpp sparse.cpp sparse_crc32.cpp sparse_err.cpp sparse_read.cpp stringprintf.cpp
OBJS := $(SRCS:.cpp=.o)

all: $(OUT)

$(OUT): $(OBJS)
	$(AR) rcs $(OUT) $(OBJS) 

clean:
	rm -f $(OUT) $(OBJS)

