#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <glfs/glfs.h>

glfs_mount_t* mount;

int64_t read_block(void* fp, uint64_t block_number, void* buffer) {
    fseek(fp, block_number * GLFS_BLOCK_SIZE, SEEK_SET);
    size_t res = fread(buffer, GLFS_BLOCK_SIZE, 1, fp);
    if (res != 1) {
        if (ferror(fp)) {
            return -errno;
        } else {
            return -EIO;
        }
    }
    return res;
}

int64_t write_block(void* fp, uint64_t block_number, const void* buffer) {
    fseek(fp, block_number * GLFS_BLOCK_SIZE, SEEK_SET);
    size_t res = fwrite(buffer, GLFS_BLOCK_SIZE, 1, fp);
    if (res != 1) {
        if (ferror(fp)) {
            return -errno;
        } else {
            return -EIO;
        }
    }
    return res;
}

void glfs_sync(void* fp) {
    fflush(fp);
    fsync(fileno(fp));
}

int glfs_fuse_getattr(const char* path, struct stat* st, struct fuse_file_info* fi) {
    glfs_attr_t attr;
    uint64_t handle;
    if (fi && fi->fh) {
        handle = fi->fh;
    } else {
        int res = glfs_lookup(mount, path, &handle);
        if (res < 0) return res;
    }
    int res = glfs_getattr(mount, handle, &attr);
    if (res < 0) return res;
    st->st_nlink = attr.refcount;
    st->st_atim = (struct timespec){attr.atime};
    st->st_ctim = (struct timespec){attr.ctime};
    st->st_mtim = (struct timespec){attr.mtime};
    st->st_uid = attr.uid;
    st->st_gid = attr.gid;
    st->st_rdev = attr.rdev;
    st->st_ino = handle;
    st->st_size = attr.size;
    mode_t mode = attr.perms;
    switch (attr.type) {
        case GLFS_REG: mode |= S_IFREG; break;
        case GLFS_DIR: mode |= S_IFDIR; break;
        case GLFS_BLK: mode |= S_IFBLK; break;
        case GLFS_CHR: mode |= S_IFCHR; break;
    }
    st->st_mode = mode;
    return 0;
}

int glfs_fuse_open(const char* path, struct fuse_file_info *fi) {
    int res = glfs_lookup(mount, path, &fi->fh);
    if (res < 0) return res;
    res = glfs_open(mount, fi->fh);
    if (res < 0) return res;
    return 0;
}

int glfs_fuse_opendir(const char* path, struct fuse_file_info *fi) {
    int res = glfs_lookup(mount, path, &fi->fh);
    if (res < 0) return res;
    glfs_attr_t attr;
    res = glfs_getattr(mount, fi->fh, &attr);
    if (res < 0) return res;
    if (attr.type != GLFS_DIR) return -ENOTDIR;
    res = glfs_open(mount, fi->fh);
    if (res < 0) return res;
    return 0;
}

int glfs_fuse_release(const char* path, struct fuse_file_info *fi) {
    int res = glfs_close(mount, fi->fh);
    if (res < 0) return res;
    return 0;
}

int glfs_fuse_releasedir(const char* path, struct fuse_file_info *fi) {
    int res = glfs_close(mount, fi->fh);
    if (res < 0) return res;
    return 0;
}

int glfs_fuse_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags) {
    uint64_t handle;
    if (fi && fi->fh) {
        handle = fi->fh;
    } else {
        int res = glfs_lookup(mount, path, &handle);
        if (res < 0) return res;
    }
    printf("path: %s, handle: %lu\n", path, handle);
    if (offset <= 0) {
        if (filler(buf, ".", NULL, 1, 0)) return 0;
        offset++;
    }
    if (offset <= 1) {
        if (filler(buf, "..", NULL, 2, 0)) return 0;
        offset++;
    }
    int i = offset - 2;
    int output = 1;
    while (output) {
        glfs_readdir_entry_t dirent = {0};
        output = glfs_readdir(mount, handle, i, &dirent);
        if (output < 0) return output;
        if (output == 0) break;
        i += output;
        if (filler(buf, dirent.name, NULL, i + 2, 0)) return 0;
    }
    return 0;
}

int glfs_fuse_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    return glfs_read(mount, fi->fh, buf, offset, size);
}

int glfs_fuse_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    return glfs_write(mount, fi->fh, buf, offset, size);
}

int glfs_fuse_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
    int res = glfs_create_file(mount, path);
    if (res < 0) return res;
    res = glfs_lookup(mount, path, &fi->fh);
    if (res < 0) return res;
    res = glfs_open(mount, fi->fh);
    if (res < 0) return res;
    return 0;
}

int glfs_fuse_mkdir(const char* path, mode_t mode) {
    return glfs_create_directory(mount, path);
}

int glfs_fuse_link(const char* from, const char* to) {
    uint64_t handle;
    int res = glfs_lookup(mount, from, &handle);
    if (res < 0) return res;
    return glfs_link(mount, handle, to);
}

int glfs_fuse_unlink(const char *path) {
    uint64_t handle;
    int res = glfs_lookup(mount, path, &handle);
    if (res < 0) return res;
    glfs_attr_t attr;
    res = glfs_getattr(mount, handle, &attr);
    if (res < 0) return res;
    if (attr.type == GLFS_DIR) return -EISDIR;
    return glfs_delete(mount, path);
}

int glfs_fuse_rmdir(const char *path) {
    uint64_t handle;
    int res = glfs_lookup(mount, path, &handle);
    if (res < 0) return res;
    glfs_attr_t attr;
    res = glfs_getattr(mount, handle, &attr);
    if (res < 0) return res;
    if (attr.type != GLFS_DIR) return -ENOTDIR;
    if (attr.size > 0) return -ENOTEMPTY;
    return glfs_delete(mount, path);
}

int glfs_fuse_rename(const char *from, const char *to, unsigned int flags) {
    if (flags != 0) return -EINVAL;
    return glfs_rename(mount, from, to);
}

int glfs_fuse_mknod(const char *path, mode_t mode, dev_t rdev) {
    uint32_t type;

    switch (mode & S_IFMT) {
        case S_IFREG:
            type = GLFS_REG;
            break;
        case S_IFDIR:
            type = GLFS_DIR;
            break;
        case S_IFBLK:
            type = GLFS_BLK;
            break;
        case S_IFCHR:
            type = GLFS_CHR;
            break;
        default:
            return -EINVAL;
    }

    return glfs_mknod(mount, path, type, rdev);
}

int glfs_fuse_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    uint64_t handle;
    if (fi && fi->fh) {
        handle = fi->fh;
    } else {
        int res = glfs_lookup(mount, path, &handle);
        if (res < 0) return res;
    }
    return glfs_truncate(mount, handle, size);
}

void glfs_fuse_destroy(void *private_data) {
    glfs_unmount(mount);
}

struct fuse_operations glfs_ops = {
    .getattr = glfs_fuse_getattr,
    .open = glfs_fuse_open,
    .release = glfs_fuse_release,
    .opendir = glfs_fuse_opendir,
    .releasedir = glfs_fuse_releasedir,
    .read = glfs_fuse_read,
    .write = glfs_fuse_write,
    .readdir = glfs_fuse_readdir,
    .create = glfs_fuse_create,
    .mkdir = glfs_fuse_mkdir,
    .link = glfs_fuse_link,
    .unlink = glfs_fuse_unlink,
    .rmdir = glfs_fuse_rmdir,
    .mknod = glfs_fuse_mknod,
    .rename = glfs_fuse_rename,
    .destroy = glfs_fuse_destroy,
    .truncate = glfs_fuse_truncate,
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: %s file mountpoint\n", argv[0]);
        return 1;
    }
    char* backing_file = argv[1];
    FILE *fp = fopen(backing_file, "r+b");
    if (!fp) {
        perror("Failed to open backing file/device");
        return 1;
    }

    glfs_backing_t backing = {
        .data = fp,
        .read_block = read_block,
        .write_block = write_block,
        .sync = glfs_sync,
        .alloc = malloc,
        .free = free,
    };

    mount = glfs_mount(&backing, 0);
    if (!mount) {
        printf("Failed to mount filesystem\n");
        return 1;
    }

    argv[1] = "-s";
    return fuse_main(argc, argv, &glfs_ops, NULL);
}
