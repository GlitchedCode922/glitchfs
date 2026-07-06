#include "layout.h"
#include <stdio.h>

int struct_test(char* name, size_t expected_size, size_t actual_size) {
    printf("Size of %s: %zu bytes\n", name, actual_size);
    if (expected_size != actual_size) {
        printf("Size mismatch for %s: expected %zu bytes, got %zu bytes\n", name, expected_size, actual_size);
        return 1;
    }
    return 0;
}

int main() {
    int results = 0;
    results += struct_test("superblock_t", GLFS_BLOCK_SIZE, sizeof(superblock_t));
    results += struct_test("inode_hdr_t", 128, sizeof(inode_hdr_t));
    results += struct_test("inode_t", GLFS_BLOCK_SIZE, sizeof(inode_t));
    results += struct_test("inode_continuation_t", GLFS_BLOCK_SIZE, sizeof(inode_continuation_t));
    results += struct_test("dirent_t", 256, sizeof(dirent_t));

    return results;
}
