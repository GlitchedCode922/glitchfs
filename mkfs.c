#include "layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <output_file>\n", argv[0]);
        return 1;
    }

    const char *output_file = argv[1];

    FILE *fp = fopen(output_file, "r+b");
    if (!fp) {
        perror("Failed to open output file");
        return 1;
    }

    // Get block count from file size
    fseek(fp, 0, SEEK_END);
    uint64_t file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    file_size -= 16 * GLFS_BLOCK_SIZE; // Remove space for boot area
    file_size -= GLFS_BLOCK_SIZE; // Remove space for superblock
    file_size -= (file_size % GLFS_BLOCK_SIZE); // Align to block size

    uint64_t block_count = 0;
    uint64_t bitmap_size = 0;
    while (1) {
        uint64_t next_block_count = block_count + 1;
        uint64_t next_bitmap_size = (next_block_count + 7) / 8;
        next_bitmap_size = (next_bitmap_size + GLFS_BLOCK_SIZE - 1) / GLFS_BLOCK_SIZE; // To blocks
        uint64_t total = next_block_count + next_bitmap_size;
        if (total * GLFS_BLOCK_SIZE > file_size) {
            break;
        }
        block_count = next_block_count;
        bitmap_size = next_bitmap_size;
    }

    if (block_count == 0) {
        fprintf(stderr, "File size too small to create a file system\n");
        fclose(fp);
        return 1;
    }

    // Initialize superblock
    superblock_t superblock;
    memset(&superblock, 0, sizeof(superblock));
    memcpy(superblock.signature, "GlitchFS", 8);
    superblock.version = 1;
    superblock.block_count = block_count;
    superblock.bitmap_size = bitmap_size;
    superblock.root_inode = 1; // Root inode number
    superblock.next_free = 2; // Next free block after root inode

    // Write superblock to file
    fseek(fp, 16 * GLFS_BLOCK_SIZE, SEEK_SET); // Skip boot area
    if (fwrite(&superblock, sizeof(superblock), 1, fp) != 1) {
        perror("Failed to write superblock");
        fclose(fp);
        return 1;
    }

    // Write block bitmap (all zeros)
    uint8_t *bitmap = calloc(superblock.bitmap_size * GLFS_BLOCK_SIZE, 1);

    if (!bitmap) {
        perror("Failed to allocate memory for block bitmap");
        fclose(fp);
        return 1;
    }

    bitmap[0] = 0x01; // Mark the first block as used (for the root inode)

    if (fwrite(bitmap, superblock.bitmap_size * GLFS_BLOCK_SIZE, 1, fp) != 1) {
        perror("Failed to write block bitmap");
        fclose(fp);
        free(bitmap);
        return 1;
    }
    free(bitmap);

    // Write root inode (empty)
    inode_t root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    uint64_t sig;
    memcpy(&sig, "GLFS_INO", 8);
    sig ^= superblock.root_inode;
    memcpy(root_inode.header.signature, &sig, 8);
    root_inode.header.mode = S_IFDIR | 0777; // Directory
    root_inode.header.size = 0; // Empty directory

    root_inode.header.uid = 0; // Root user
    root_inode.header.gid = 0; // Root group

    root_inode.header.atime = 0; // Access time
    root_inode.header.mtime = 0; // Modification time
    root_inode.header.ctime = 0; // Change time
    root_inode.header.refcount = 1; // One reference (the root directory itself)

    fseek(fp, (16 + 1 + superblock.bitmap_size) * GLFS_BLOCK_SIZE, SEEK_SET); // Skip boot area, superblock, and bitmap
    if (fwrite(&root_inode, sizeof(inode_t), 1, fp) != 1) {
        perror("Failed to write root inode");
        fclose(fp);
        return 1;
    }

    fclose(fp);
    return 0;
}
