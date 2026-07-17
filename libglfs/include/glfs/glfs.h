#include <stdint.h>
#include "layout.h"

typedef struct {
    uint64_t inode;
    uint64_t index;
} glfs_dirent_ref_t;

typedef struct {
    uint64_t inode;
    uint32_t opencount;
} glfs_open_file_t;

typedef struct{
    uint32_t type;
    char name[256]; // POSIX max name
} glfs_readdir_entry_t;

typedef struct {
    uint64_t size;
    uint64_t inode;
    uint64_t refcount;
    uint64_t atime;
    uint64_t ctime;
    uint64_t mtime;
    uint32_t type;
    uint32_t perms;
    uint64_t uid;
    uint64_t gid;
    uint64_t rdev;
} glfs_attr_t;

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
    glfs_open_file_t open_files[512];
    uint8_t read_only;
} glfs_mount_t;

int glfs_check(glfs_backing_t* backing);
glfs_mount_t* glfs_mount(glfs_backing_t* backing, int ro);
int glfs_unmount(glfs_mount_t* mount);
int glfs_mkfs(glfs_backing_t* backing, uint64_t volume_size);

int glfs_lookup(glfs_mount_t* mount, const char* path, uint64_t* inode_number);

int glfs_open(glfs_mount_t* mount, uint64_t inode_number);
int glfs_close(glfs_mount_t* mount, uint64_t inode_number);

int glfs_readdir(glfs_mount_t* mount, uint64_t inode_number, int index, glfs_readdir_entry_t* out);
int64_t glfs_read(glfs_mount_t* mount, uint64_t inode_number, void* buffer, uint64_t offset, uint64_t size);
int64_t glfs_write(glfs_mount_t* mount, uint64_t inode_number, const void* buffer, uint64_t offset, uint64_t size);
int glfs_getattr(glfs_mount_t* mount, uint64_t inode_number, glfs_attr_t* out);
int glfs_delete(glfs_mount_t* mount, const char* path);
int glfs_create_file(glfs_mount_t* mount, const char* path);
int glfs_create_directory(glfs_mount_t* mount, const char* path);
int glfs_rename(glfs_mount_t* mount, const char* old_path, const char* new_path);
int glfs_mknod(glfs_mount_t* mount, const char* path, uint32_t type, uint64_t dev);
int glfs_link(glfs_mount_t* mount, uint64_t inode_number, const char* link);
int glfs_truncate(glfs_mount_t* mount, uint64_t inode_number, uint64_t new_size);
