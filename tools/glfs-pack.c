#include <errno.h>
#include <glfs/glfs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

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
}

int join_path(char* out, size_t out_size, const char* a, const char* b) {
    size_t alen = strlen(a);
    if (alen + 1 + strlen(b) + 1 > out_size) return -1;
    strcpy(out, a);
    if (alen > 0 && out[alen - 1] != '/') {
        out[alen] = '/';
        out[alen + 1] = '\0';
    }
    strcat(out, b);
    return 0;
}

int walk(glfs_mount_t* mount, const char* host_path, const char* glfs_path) {
    DIR* dir = opendir(host_path);
    if (dir == NULL) {
        perror("opendir");
        return 1;
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
        strcmp(entry->d_name, "..") == 0) continue;

        char child[4096];
        int res = join_path(child, 4096, host_path, entry->d_name);
        if (res < 0) {
            fprintf(stderr, "Error: %s\n", strerror(ENAMETOOLONG));
            return 1;
        }

        char glfs_child[4096];
        res = join_path(glfs_child, 4096, glfs_path, entry->d_name);
        if (res < 0) {
            fprintf(stderr, "Error: %s\n", strerror(ENAMETOOLONG));
            return 1;
        }

        struct stat st;
        if (stat(child, &st) < 0) continue;
        if (S_ISDIR(st.st_mode)) {
            res = glfs_create_directory(mount, glfs_child);
            if (res < 0) {
                fprintf(stderr, "Error: %s\n", strerror(-res));
                return 1;
            }
            res = walk(mount, child, glfs_child);
            if (res != 0) return res;
        } else if (S_ISREG(st.st_mode)) {
            res = glfs_create_file(mount, glfs_child);
            if (res < 0) {
                fprintf(stderr, "Error: %s\n", strerror(-res));
                return 1;
            }
            FILE* file = fopen(child, "rb");
            uint8_t* buffer = malloc(st.st_size);
            if (!buffer) {
                perror("malloc");
                return 1;
            }
            res = fread(buffer, 1, st.st_size, file);
            if (res != st.st_size) {
                fprintf(stderr, "Read error\n");
                return 1;
            }
            res = glfs_write(mount, glfs_child, buffer, 0, st.st_size);
            if (res < 0) {
                fprintf(stderr, "Error: %s\n", strerror(-res));
                return 1;
            }
            fclose(file);
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <directory> <output_file>\n", argv[0]);
        return 1;
    }

    const char *directory = argv[1];
    const char *output_file = argv[2];

    FILE *fp = fopen(output_file, "r+b");
    if (!fp) {
        perror("Failed to open output file");
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

    fseek(fp, 0, SEEK_END);
    uint64_t size = ftell(fp);

    if (size < 19 * GLFS_BLOCK_SIZE) {
        fprintf(stderr, "Output file too small to create a filesystem\n");
        return -EINVAL;
    }

    int res = glfs_mkfs(&backing, size);
    if (res < 0) {
        errno = -res;
        perror("mkfs");
        fclose(fp);
        return 1;
    }

    glfs_mount_t* mount = glfs_mount(&backing, 0);
    if (!mount) {
        fprintf(stderr, "Couldn't mount image");
        return 1;
    }

    res = walk(mount, directory, "");
    fclose(fp);
    return res;
}
