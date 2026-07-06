CC = gcc
CFLAGS =

all: build/mkfs build/test

build:
	mkdir -p build

build/mkfs: mkfs.c layout.h | build
	$(CC) $(CFLAGS) -o build/mkfs mkfs.c

build/test: test.c layout.h | build
	$(CC) $(CFLAGS) -o build/test test.c

install: build/mkfs
	@mkdir -p /usr/local/bin
	install -m 755 build/mkfs /usr/local/bin/mkfs.glfs

clean:
	rm -rf build
