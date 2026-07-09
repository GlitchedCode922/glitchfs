#include <stdint.h>
#include "layout.h"

typedef struct {
    void* data;
    int64_t (*read_block)(void* data, uint64_t block_number, void* buffer); // Absolute block R/W, no translation, 0-based
    int64_t (*write_block)(void* data, uint64_t block_number, const void* buffer); // Absolute block R/W, no translation, 0-based
    void (*sync)(void* data);
    void *(*alloc)(uint64_t size);
    void (*free)(void* ptr);
} glfs_backing_t;

typedef struct {
    glfs_superblock_t superblock;
    glfs_backing_t backing;
    uint8_t* block_bitmap;
    uint8_t read_only;
} glfs_mount_t;

typedef struct {
    uint64_t inode;
    uint64_t index;
} glfs_dirent_ref_t;

typedef struct{
    uint32_t type;
    char name[256]; // POSIX max name
} glfs_readdir_entry_t;

typedef struct {
    uint64_t size;
    uint64_t inode;
    uint64_t ctime;
    uint64_t mtime;
    uint32_t type;
    uint32_t perms;
    uint64_t uid;
    uint64_t gid;
    uint64_t rdev;
} glfs_stat_t;

int glfs_readdir(glfs_mount_t* mount, const char* path, int index, glfs_readdir_entry_t* out);
int64_t glfs_read(glfs_mount_t* mount, const char* path, void* buffer, uint64_t offset, uint64_t size);
int64_t glfs_write(glfs_mount_t* mount, const char *path, const void* buffer, uint64_t offset, uint64_t size);
int glfs_delete(glfs_mount_t* mount, const char* path);
int glfs_create_file(glfs_mount_t* mount, const char* path);
int glfs_create_directory(glfs_mount_t* mount, const char* path);
int glfs_rename(glfs_mount_t* mount, const char* old_path, const char* new_path);
int glfs_stat(glfs_mount_t* mount, const char* path, glfs_stat_t* out);
int glfs_mknod(glfs_mount_t* mount, const char* path, uint32_t type, uint64_t dev);
int glfs_link(glfs_mount_t* mount, const char* path, const char* link);
