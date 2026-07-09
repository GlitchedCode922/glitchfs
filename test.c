#include <glfs/layout.h>
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
    results += struct_test("glfs_superblock_t", GLFS_BLOCK_SIZE, sizeof(glfs_superblock_t));
    results += struct_test("glfs_inode_t", GLFS_BLOCK_SIZE, sizeof(glfs_inode_t));
    results += struct_test("glfs_inode_continuation_t", GLFS_BLOCK_SIZE, sizeof(glfs_inode_continuation_t));
    results += struct_test("glfs_dirent_t", 256, sizeof(glfs_dirent_t));

    return results;
}
