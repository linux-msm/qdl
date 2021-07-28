MAKEFLAGS += --silent
.PHONY: qdl

qdl: 
	cmake -B build -S .
	cmake --build build --clean-first
	mv build/qdl_src/qdl qdl
