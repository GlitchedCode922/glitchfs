#pragma once
#include <stdint.h>

#define GLFS_BLOCK_SIZE 4096 // Size of a block in bytes (4KiB)

#define PACKED __attribute__((packed))

typedef struct {
    char signature[8]; // "GlitchFS"
    uint64_t version; // File system version
    uint64_t block_count; // Count of blocks in the file system
    uint64_t bitmap_size; // Size of block bitmap in blocks
    uint64_t root_inode; // Inode number of the root directory
    uint64_t next_free; // Next free block number
    uint8_t reserved[GLFS_BLOCK_SIZE - 6 * 8]; // Alignment padding
} PACKED glfs_superblock_t;

enum {
    GLFS_REG = 1,
    GLFS_DIR = 2,
    GLFS_BLK = 3,
    GLFS_CHR = 4,
};

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

#define GLFS_MAX_FILENAME_LENGTH (256 - 8) // Make dirents 256 bytes long
typedef struct {
    uint64_t inodeptr;
    char name[GLFS_MAX_FILENAME_LENGTH];
} PACKED glfs_dirent_t;

#undef PACKED
