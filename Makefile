CC = gcc

all: build/mkfs build/test build/libglfs.a

build:
	mkdir -p build
	mkdir -p build/libglfs

build/mkfs: mkfs.c | build
	$(CC) $(CFLAGS) -o build/mkfs -Ilibglfs/include mkfs.c

build/test: test.c | build
	$(CC) $(CFLAGS) -o build/test -Ilibglfs/include test.c

LIBGLFS_SOURCE = $(shell find libglfs/src -name '*.c')
LIBGLFS_OBJECTS = $(patsubst libglfs/src/%.c, build/libglfs/%.o, $(LIBGLFS_SOURCE))

build/libglfs.a: $(LIBGLFS_OBJECTS)
	$(AR) rcs $@ $^

build/libglfs/%.o: libglfs/src/%.c | build
	$(CC) $(CFLAGS) -Ilibglfs/include -c -o $@ $<

install: build/mkfs
	@mkdir -p /usr/local/bin
	install -m 755 build/mkfs /usr/local/bin/mkfs.glfs

clean:
	rm -rf build
