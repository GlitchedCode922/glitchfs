# GlitchFS

GlitchFS is a 64-bit, block-based filesystem designed to be simple enough for hobby OSes, while being scalable to very large disks.

## Design goals

- Simple on-disk format
- Easy to implement on a custom OS
- 64-bit addressing
- Unified blocks and allocator (can be used for both inodes and data)
- Fast file access
- Scalable layout

## Disk layout

Note: The size of a block is fixed at 4KiB.

- Boot area (16 blocks)
- Superblock (1 block)
- Bitmap (variable sized)
- Data blocks

### Superblock

The superblock is the entry point to the filesystem. It contains required information to parse the filesystem.

```c
typedef struct {
    char signature[8]; // "GlitchFS"
    uint64_t version; // File system version
    uint64_t block_count; // Count of blocks in the file system
    uint64_t bitmap_size; // Size of block bitmap in blocks
    uint64_t root_inode; // Inode number of the root directory
    uint64_t next_free; // Next free block number
    uint8_t reserved[GLFS_BLOCK_SIZE - 6 * 8]; // Alignment padding
} PACKED glfs_superblock_t;
```

WARNING: `next_free` is for optimization. If it is invalid, the bitmap should be scanned instead.

### Bitmap and allocation

The bitmap is located after the superblock. It contains 1 bit per block. The bitmap is padded to a block boundary at the end. A free block is marked with a `0` and an allocated one with an `1`. Block numbers start from 1 (the block after the bitmap).

### Inodes

An inode represents a file, directory or special file. It contains the file's metadata and its block pointers. If the inode block is full, it is expanded on one or more continuation blocks, which contain block pointers and links to the next inode blocks (next, +4, +16, +64, +256 and +1024). `skip_N` pointers are optional and can contain a value of 0 instead of an actual skip. For `next_inode_block`, a value of 0 marks the end of the list. The inode number is equal to its first block number. A directory inode contains dirents instead of file data. After being deleted, the signature of an inode must be cleared.

```c
enum {
    GLFS_FILE = 1,
    GLFS_DIR = 2,
    GLFS_BLOCK = 3,
    GLFS_CHAR = 4,
}; // File types

typedef struct {
    char signature[8]; // "GLFS_INO" xor inode number
    uint32_t perms; // Unix permissions
    uint32_t type; // File type
    uint64_t size; // Size of the file in bytes
    uint32_t uid; // User ID of the file owner
    uint32_t gid; // Group ID of the file owner
    uint64_t atime; // Last access time
    uint64_t mtime; // Last modification time
    uint64_t ctime; // Last status change time
    uint64_t refcount; // Reference count (number of hard links)
    uint64_t next_inode_block; // Block number of the next inode block
    uint64_t rdev; // Device ID (for special files)
    uint64_t reserved[5]; // For future use, and to pad header to 128 bytes
    uint64_t block_count; // Number of blocks allocated to the file
    // Header end
    uint64_t blocks[(GLFS_BLOCK_SIZE - 128) / 8]; // Array of block pointers
} PACKED glfs_inode_t;

typedef struct {
    uint64_t next_inode_block;
    uint64_t skip_4;
    uint64_t skip_16;
    uint64_t skip_64;
    uint64_t skip_256;
    uint64_t skip_1024;
    uint64_t blocks[GLFS_BLOCK_SIZE / 8 - 6]; // Array of block pointers
} PACKED glfs_inode_continuation_t;
```

### Dirents

A dirent stores a path segment, and an inode number. Dirents with an `inodeptr` equal to 0 are invalid. To delete a dirent, replace it with another one (shift or move last) and truncate the inode. Dirents have a fixed size of 256 bytes. Single and double dot entries are not stored.

```c
#define MAX_FILENAME_LENGTH (256 - 8) // Make dirents 256 bytes long
typedef struct {
    uint64_t inodeptr;
    char name[MAX_FILENAME_LENGTH];
} PACKED glfs_dirent_t;
```

## Maximum disk and file sizes

Because blocks are addressed using 64-bit numbers, there can be 2^64 data blocks in a drive, meaning 2^76 bytes, or 64 ZiB (ignoring maximum drive LBA of 2^64).

The maximum size of a file = the size of the drive - filesystem metadata, since there is no limit on the blocks a file can contain.

## Reliability and recovery

Recovery is, by design, not built into the mount or write operations. A `fsck` tool could search for stale inodes using their signatures, and follow existing dirents to fix bitmap corruption. The superblock contains minimal information, so it is possible to rebuild.

## Implementation

In this repository, there is a library to parse GlitchFS. It is very portable, using a block device abstraction, no global state and only freestanding C code. The environment has to provide `memset()`, `memcpy()`, `memcmp()` and `memmove()`, functions to read and write a 4096 byte sized buffer in an offset, (optionally) flush, and memory allocate/free. Look at tools/glfs-pack.c for an example on how to use the library.

## Status

This filesystem is currently experimental. On-disk layout is partially stable. It may change, but structure sizes and field offsets will remain the same.
