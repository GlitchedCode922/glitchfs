#include "glfs/glfs.h"
#include "glfs/layout.h"
#include "glfs/error.h"
#include <stddef.h>
#include <stdint.h>

void* memcpy(void* dest, const void* src, size_t n);
int memcmp(const void* ptr1, const void* ptr2, size_t n);
void* memset(void* ptr, int b, size_t n);

static size_t glfs_strlen(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

int glfs_read_block(glfs_mount_t* mount, int64_t block_number, void* buffer) {
    if (block_number >= mount->superblock.block_count) return -EIO;
    uint64_t absolute_block = 17 + mount->superblock.bitmap_size + block_number - 1;
    return mount->backing.read_block(mount->backing.data, absolute_block, buffer);
}

int glfs_write_block(glfs_mount_t* mount, uint64_t block_number, void* buffer) {
    if (mount->read_only) return -EROFS;
    if (block_number >= mount->superblock.block_count) return -EIO;
    uint64_t absolute_block = 17 + mount->superblock.bitmap_size + block_number - 1;
    return mount->backing.write_block(mount->backing.data, absolute_block, buffer);
}

int glfs_check(glfs_backing_t* backing) {
    uint8_t buffer[GLFS_BLOCK_SIZE];
    int res = backing->read_block(backing->data, 16, buffer);
    if (res < 0) return res;
    glfs_superblock_t* superblock = (glfs_superblock_t*)buffer;
    if (memcmp(superblock->signature, "GlitchFS", 8) != 0) {
        return 0; // Not a GlitchedFS filesystem
    }
    return 1; // Valid GlitchedFS filesystem
}

glfs_mount_t* glfs_mount(glfs_backing_t* backing, int ro) {
    if (glfs_check(backing) <= 0) return NULL;
    glfs_mount_t* mount = backing->alloc(sizeof(glfs_mount_t));
    if (!mount) return NULL;
    mount->backing = *backing;
    mount->read_only = ro;

    // Read the superblock
    uint8_t buffer[GLFS_BLOCK_SIZE];
    int res = backing->read_block(backing->data, 16, buffer);
    if (res < 0) {
        backing->free(mount);
        return NULL;
    }
    memcpy(&mount->superblock, buffer, sizeof(glfs_superblock_t));

    // Read the block bitmap
    mount->block_bitmap = backing->alloc(mount->superblock.bitmap_size * GLFS_BLOCK_SIZE);
    if (!mount->block_bitmap) {
        backing->free(mount);
        return NULL;
    }
    for (uint64_t i = 0; i < mount->superblock.bitmap_size; i++) {
        res = backing->read_block(backing->data, 17 + i, &mount->block_bitmap[i * GLFS_BLOCK_SIZE]);
        if (res < 0) {
            backing->free(mount->block_bitmap);
            backing->free(mount);
            return NULL;
        }
    }

    return mount;
}

int glfs_mkfs(glfs_backing_t *backing, uint64_t volume_size) {
    if (volume_size < 19 * GLFS_BLOCK_SIZE) return -EINVAL;
    volume_size -= 16 * GLFS_BLOCK_SIZE; // Remove space for boot area
    volume_size -= GLFS_BLOCK_SIZE; // Remove space for superblock
    volume_size -= (volume_size % GLFS_BLOCK_SIZE); // Align to block size

    uint64_t block_count = 0;
    uint64_t bitmap_size = 0;
    while (1) {
        uint64_t next_block_count = block_count + 1;
        uint64_t next_bitmap_size = (next_block_count + 7) / 8;
        next_bitmap_size = (next_bitmap_size + GLFS_BLOCK_SIZE - 1) / GLFS_BLOCK_SIZE; // To blocks
        uint64_t total = next_block_count + next_bitmap_size;
        if (total * GLFS_BLOCK_SIZE > volume_size) {
            break;
        }
        block_count = next_block_count;
        bitmap_size = next_bitmap_size;
    }

    // Write superblock
    glfs_superblock_t superblock;
    memset(&superblock, 0, sizeof(superblock));
    memcpy(superblock.signature, "GlitchFS", 8);
    superblock.version = 1;
    superblock.block_count = block_count;
    superblock.bitmap_size = bitmap_size;
    superblock.root_inode = 1; // Root inode number
    superblock.next_free = 2; // Next free block after root inode
    int res = backing->write_block(backing->data, 16, &superblock);
    if (res < 0) return res;

    // Write block bitmap (all zeros)
    uint8_t *bitmap = backing->alloc(bitmap_size * GLFS_BLOCK_SIZE);
    if (!bitmap) {
        return -ENOMEM;
    }

    memset(bitmap, 0, bitmap_size * GLFS_BLOCK_SIZE);
    bitmap[0] = 0x01; // Mark the first block as used (for the root inode)

    for (int64_t i = 0; i < bitmap_size; i++) {
        res = backing->write_block(backing->data, 17 + i, bitmap + i * GLFS_BLOCK_SIZE);
        if (res < 0) {
            backing->free(bitmap);
            return res;
        }
    }
    backing->free(bitmap);

    // Write root inode (empty)
    glfs_inode_t root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    uint64_t sig;
    memcpy(&sig, "GLFS_INO", 8);
    sig ^= superblock.root_inode;
    memcpy(root_inode.signature, &sig, 8);
    root_inode.perms = 0755; // Default permissions for root directory
    root_inode.type = GLFS_DIR; // Directory type
    root_inode.size = 0; // Empty directory

    root_inode.uid = 0; // Root user
    root_inode.gid = 0; // Root group

    root_inode.atime = 0; // Access time
    root_inode.mtime = 0; // Modification time
    root_inode.ctime = 0; // Change time
    root_inode.refcount = 1; // One reference (the root directory itself)

    res = backing->write_block(backing->data, 17 + bitmap_size, &root_inode);
    if (res < 0) return res;
    if (mount->backing.sync) mount->backing.sync(mount->backing.data);
    return 0;
}

int glfs_block_alloc(glfs_mount_t* mount, uint64_t* block_number) {
    if (mount->read_only) return -EROFS;
    for (uint64_t i = mount->superblock.next_free == 0 ? 0 : mount->superblock.next_free - 1; i < mount->superblock.block_count; i++) {
        uint8_t byte = mount->block_bitmap[i / 8];
        if (!(byte & (1 << (i % 8)))) {
            // Found a free block
            mount->block_bitmap[i / 8] |= (1 << (i % 8)); // Mark as used
            // Write the updated bitmap back to disk
            int res = mount->backing.write_block(mount->backing.data, 17 + (i / (GLFS_BLOCK_SIZE * 8)), &mount->block_bitmap[(i / (GLFS_BLOCK_SIZE * 8)) * GLFS_BLOCK_SIZE]);
            if (res < 0) {
                mount->block_bitmap[i / 8] &= ~(1 << (i % 8));
                return res;
            }
            *block_number = i + 1; // Block numbers start at 1
            mount->superblock.next_free = *block_number + 1;
            mount->backing.write_block(mount->backing.data, 16, &mount->superblock);
            if (mount->backing.sync) mount->backing.sync(mount->backing.data);
            return 0; // Success
        }
    }

    for (uint64_t i = 0; i < mount->superblock.next_free; i++) {
        uint8_t byte = mount->block_bitmap[i / 8];
        if (!(byte & (1 << (i % 8)))) {
            // Found a free block
            mount->block_bitmap[i / 8] |= (1 << (i % 8)); // Mark as used
            // Write the updated bitmap back to disk
            int res = mount->backing.write_block(mount->backing.data, 17 + (i / (GLFS_BLOCK_SIZE * 8)), &mount->block_bitmap[(i / (GLFS_BLOCK_SIZE * 8)) * GLFS_BLOCK_SIZE]);
            if (res < 0) {
                mount->block_bitmap[i / 8] &= ~(1 << (i % 8));
                return res;
            }
            *block_number = i + 1; // Block numbers start at 1
            mount->superblock.next_free = *block_number + 1;
            mount->backing.write_block(mount->backing.data, 16, &mount->superblock);
            if (mount->backing.sync) mount->backing.sync(mount->backing.data);
            return 0; // Success
        }
    }
    return -ENOSPC; // No space left on device
}

int glfs_block_free(glfs_mount_t* mount, uint64_t block_number) {
    if (mount->read_only) return -EROFS;
    if (block_number == 0 || block_number > mount->superblock.block_count) {
        return -EINVAL; // Invalid block number
    }
    uint64_t index = block_number - 1;
    mount->block_bitmap[index / 8] &= ~(1 << (index % 8)); // Mark as free
    // Write the updated bitmap back to disk
    int res = mount->backing.write_block(mount->backing.data, 17 + (index / (GLFS_BLOCK_SIZE * 8)), &mount->block_bitmap[(index / (GLFS_BLOCK_SIZE * 8)) * GLFS_BLOCK_SIZE]);
    if (res < 0) {
        mount->block_bitmap[index / 8] |= (1 << (index % 8));
        return res;
    }
    if (block_number < mount->superblock.next_free) {
        mount->superblock.next_free = block_number;
        mount->backing.write_block(mount->backing.data, 16, &mount->superblock);
    }
    if (mount->backing.sync) mount->backing.sync(mount->backing.data);
    return 0; // Success
}

int glfs_read_inode_block_ptrs(glfs_mount_t* mount, uint64_t inode_number, uint64_t offset, uint64_t* block_ptrs, size_t size) {
    glfs_inode_t inode;
    glfs_inode_continuation_t cont;
    int res = glfs_read_block(mount, inode_number, &inode);
    if (res < 0) return res;

    size_t count = 0;
    size_t skip = (size_t)offset; // Number of pointers to skip

    const size_t inode_ptrs = (GLFS_BLOCK_SIZE - 128) / 8;
    const size_t cont_ptrs = GLFS_BLOCK_SIZE / 8 - 6;

    if (skip < inode_ptrs) {
        for (size_t i = skip; i < inode_ptrs && count < size; i++) {
            block_ptrs[count++] = inode.blocks[i];
        }
        skip = 0;
    } else {
        skip -= inode_ptrs;
    }

    if (count >= size) {
        return count;
    }

    uint64_t next_block = inode.next_inode_block;
    while (next_block != 0 && count < size) {
        res = glfs_read_block(mount, next_block, &cont);
        if (res < 0) return res;

        if (skip < cont_ptrs) {
            for (size_t i = skip; i < cont_ptrs && count < size; i++) {
                block_ptrs[count++] = cont.blocks[i];
            }
            skip = 0;
        } else {
            skip -= cont_ptrs;
        }

        next_block = cont.next_inode_block;
    }

    return count;
}

int glfs_write_inode_block_ptrs(glfs_mount_t* mount, uint64_t inode_number, uint64_t offset, uint64_t* block_ptrs, size_t size) {
    if (mount->read_only) return -EROFS;
    glfs_inode_t inode;
    glfs_inode_continuation_t cont;
    int res = glfs_read_block(mount, inode_number, &inode);
    if (res < 0) return res;

    size_t count = 0;
    size_t skip = (size_t)offset; // Number of pointers to skip

    const size_t inode_ptrs = (GLFS_BLOCK_SIZE - 128) / 8;
    const size_t cont_ptrs = GLFS_BLOCK_SIZE / 8 - 6;

    if (offset + size > inode.block_count) {
        inode.block_count = offset + size;
        int res = glfs_write_block(mount, inode_number, &inode);
        if (res < 0) return res;
    }

    if (skip < inode_ptrs) {
        for (size_t i = skip; i < inode_ptrs && count < size; i++) {
            inode.blocks[i] = block_ptrs[count++];
        }
        res = glfs_write_block(mount, inode_number, &inode);
        if (res < 0) return res;
        skip = 0;
    } else {
        skip -= inode_ptrs;
    }

    if (count >= size) {
        return count;
    }

    uint64_t previous_block = 0;
    uint64_t next_block = inode.next_inode_block;
    while (count < size) {
        if (next_block) {
            res = glfs_read_block(mount, next_block, &cont);
            if (res < 0) return res;
        } else {
            uint64_t new_block;
            res = glfs_block_alloc(mount, &new_block);
            if (res < 0) return res;
            if (previous_block) {
                cont.next_inode_block = new_block;
                res = glfs_write_block(mount, previous_block, &cont);
                if (res < 0) return res;
            } else {
                inode.next_inode_block = new_block;
                res = glfs_write_block(mount, inode_number, &inode);
                if (res < 0) return res;
            }
            next_block = new_block;
            memset(&cont, 0, GLFS_BLOCK_SIZE);
            res = glfs_write_block(mount, next_block, &cont);
            if (res < 0) return res;
        }

        if (skip < cont_ptrs) {
            for (size_t i = skip; i < cont_ptrs && count < size; i++) {
                cont.blocks[i] = block_ptrs[count++];
            }
            res = glfs_write_block(mount, next_block, &cont);
            if (res < 0) return res;
            skip = 0;
        } else {
            skip -= cont_ptrs;
        }

        previous_block = next_block;
        next_block = cont.next_inode_block;
    }

    if (mount->backing.sync) mount->backing.sync(mount->backing.data);
    return count;
}

int64_t glfs_read_inode(glfs_mount_t* mount, uint64_t inode_number, uint8_t* buffer, uint64_t offset, uint64_t size) {
    if (!buffer) return -EINVAL;
    if (size == 0) return 0;
    glfs_inode_t inode;
    int res = glfs_read_block(mount, inode_number, &inode);
    if (res < 0) return res;
    if (inode.type == GLFS_BLK || inode.type == GLFS_CHR) {
        return -ENODEV;
    }
    if (offset + size > inode.size) {
        if (offset >= inode.size) return 0;
        size = inode.size - offset;
    }
    uint64_t first_block = offset / GLFS_BLOCK_SIZE;
    uint64_t in_block_offset = offset % GLFS_BLOCK_SIZE;
    uint64_t block_count = (size + in_block_offset + GLFS_BLOCK_SIZE - 1) / GLFS_BLOCK_SIZE;
    uint64_t bytes_read = 0;
    uint64_t* block_pointers = mount->backing.alloc(block_count * 8);
    if (!block_pointers) return -ENOMEM;
    res = glfs_read_inode_block_ptrs(mount, inode_number, first_block, block_pointers, block_count);
    if (res < 0) {
        mount->backing.free(block_pointers);
        return res;
    }
    for (int i = 0; i < block_count; i++) {
        uint8_t block_buffer[GLFS_BLOCK_SIZE];
        res = glfs_read_block(mount, block_pointers[i], block_buffer);
        if (res < 0) {
            mount->backing.free(block_pointers);
            return res;
        }
        for (int j = 0; j < GLFS_BLOCK_SIZE - in_block_offset && j < size - bytes_read; j++) {
            buffer[bytes_read + j] = block_buffer[in_block_offset + j];
        }

        uint64_t copied = GLFS_BLOCK_SIZE - in_block_offset;
        if (copied > size - bytes_read) copied = size - bytes_read;
        bytes_read += copied;

        in_block_offset = 0;
    }
    mount->backing.free(block_pointers);
    return bytes_read;
}

int64_t glfs_write_inode(glfs_mount_t* mount, uint64_t inode_number, const uint8_t* buffer, uint64_t offset, uint64_t size) {
    if (mount->read_only) return -EROFS;
    if (!buffer) return -EINVAL;
    if (size == 0) return 0;
    glfs_inode_t inode;
    int res = glfs_read_block(mount, inode_number, &inode);
    if (res < 0) return res;
    if (inode.type == GLFS_BLK || inode.type == GLFS_CHR) {
        return -ENODEV;
    }
    // Allocate more blocks if required
    uint64_t blocks_required = (offset + size + GLFS_BLOCK_SIZE - 1) / GLFS_BLOCK_SIZE;
    if (blocks_required > inode.block_count) {
        uint64_t blocks_to_add = blocks_required - inode.block_count;
        uint64_t* pointers = mount->backing.alloc(blocks_to_add * 8);
        if (!pointers) return -ENOMEM;
        for (int i = 0; i < blocks_to_add; i++) {
            res = glfs_block_alloc(mount, pointers + i);
            if (res < 0) {
                for (int j = 0; j < i; j++) {
                    glfs_block_free(mount, pointers[j]);
                }
                mount->backing.free(pointers);
                return res;
            }
        }
        res = glfs_write_inode_block_ptrs(mount, inode_number, inode.block_count, pointers, blocks_to_add);
        if (res < 0) {
            for (int i = 0; i < blocks_to_add; i++) {
                glfs_block_free(mount, pointers[i]);
            }
            mount->backing.free(pointers);
            return res;
        }

        // Cannot free blocks on failure anymore, there is no way to remove pointers from inode
        mount->backing.free(pointers);
        res = glfs_read_block(mount, inode_number, &inode);
        if (res < 0) {
            return res;
        }

        inode.block_count = blocks_required;
    }
    if (offset + size > inode.size) {
        inode.size = offset + size;
    }
    res = glfs_write_block(mount, inode_number, &inode);
    if (res < 0) return res;

    uint64_t first_block = offset / GLFS_BLOCK_SIZE;
    uint64_t in_block_offset = offset % GLFS_BLOCK_SIZE;
    uint64_t block_count = (size + in_block_offset + GLFS_BLOCK_SIZE - 1) / GLFS_BLOCK_SIZE;
    uint64_t bytes_written = 0;
    uint64_t* block_pointers = mount->backing.alloc(block_count * 8);
    if (!block_pointers) return -ENOMEM;
    res = glfs_read_inode_block_ptrs(mount, inode_number, first_block, block_pointers, block_count);
    if (res < 0) {
        mount->backing.free(block_pointers);
        return res;
    }
    for (int i = 0; i < block_count; i++) {
        uint8_t block_buffer[GLFS_BLOCK_SIZE];
        res = glfs_read_block(mount, block_pointers[i], block_buffer);
        if (res < 0) {
            mount->backing.free(block_pointers);
            return res;
        }
        for (int j = 0; j < GLFS_BLOCK_SIZE - in_block_offset && j < size - bytes_written; j++) {
            block_buffer[in_block_offset + j] = buffer[bytes_written + j];
        }
        res = glfs_write_block(mount, block_pointers[i], block_buffer);
        if (res < 0) {
            mount->backing.free(block_pointers);
            return res;
        }

        uint64_t copied = GLFS_BLOCK_SIZE - in_block_offset;
        if (copied > size - bytes_written) copied = size - bytes_written;
        bytes_written += copied;

        in_block_offset = 0;
    }

    if (mount->backing.sync) mount->backing.sync(mount->backing.data);
    mount->backing.free(block_pointers);
    return bytes_written;
}

int glfs_get_dirent(glfs_mount_t* mount, const char* path, glfs_dirent_ref_t* p_dirent) {
    if (!path) return -EINVAL;
    while (*path == '/') path++;
    glfs_inode_t current;
    uint64_t inode_block = 0;
    uint64_t next_inode_block = mount->superblock.root_inode;
    int dirent_index = 0;
    int res;
    while (*path) {
        char subdir[GLFS_MAX_FILENAME_LENGTH] = {0};
        int count = 0;
        while (*path != '/' && *path && count < GLFS_MAX_FILENAME_LENGTH) {
            subdir[count++] = *path++;
        }
        if (count == 0) {
            if (*path == '/') path++;  // Consume slash
            continue;
        }
        if (count == GLFS_MAX_FILENAME_LENGTH && *path && *path != '/') return -ENAMETOOLONG;
        inode_block = next_inode_block;
        res = glfs_read_block(mount, inode_block, &current);
        if (res < 0) return res;
        if (current.type != GLFS_DIR) return -ENOENT;
        if (current.size == 0) return -ENOENT;
        if (current.size % 256 != 0) return -EIO;
        // Read directory
        glfs_dirent_t* dirents = mount->backing.alloc(current.size);
        if (!dirents) return -ENOMEM;
        res = glfs_read_inode(mount, inode_block, (uint8_t*)dirents, 0, current.size);
        if (res < 0) {
            mount->backing.free(dirents);
            return res;
        }
        if (res != current.size) return -EIO;
        int found = 0;
        for (dirent_index = 0; dirent_index < current.size / 256; dirent_index++) {
            if (memcmp(dirents[dirent_index].name, subdir, GLFS_MAX_FILENAME_LENGTH) == 0) {
                // Found
                found = 1;
                next_inode_block = dirents[dirent_index].inodeptr;
                break;
            }
        }
        mount->backing.free(dirents);
        if (!found) return -ENOENT;
    }
    p_dirent->inode = inode_block;
    p_dirent->index = dirent_index;
    return 0;
}

int glfs_readdir(glfs_mount_t* mount, const char *path, int index, glfs_readdir_entry_t *out) {
    if (!path) return -EINVAL;
    if (!out) return -EINVAL;
    if (index < 0) return -EINVAL;
    glfs_dirent_ref_t dirent_ref;
    int res = glfs_get_dirent(mount, path, &dirent_ref);
    if (res < 0) return res;
    glfs_dirent_t dir = {0};
    if (dirent_ref.inode) {
        res = glfs_read_inode(mount, dirent_ref.inode, (uint8_t*)&dir, dirent_ref.index * 256, 256);
        if (res < 0) return res;
    } else {
        dir.inodeptr = mount->superblock.root_inode;
    }
    glfs_inode_t inode;
    res = glfs_read_block(mount, dir.inodeptr, &inode);
    if (res < 0) return res;
    if (inode.type != GLFS_DIR) return -ENOTDIR;
    if (inode.size % 256 != 0) return -EIO;
    uint64_t dirent_count = inode.size / 256;
    if (index >= dirent_count) return 0;
    glfs_dirent_t dirent;
    res = glfs_read_inode(mount, dir.inodeptr, (uint8_t*)&dirent, index * 256, 256);
    if (res < 0) return res;
    res = glfs_read_block(mount, dirent.inodeptr, &inode);
    if (res < 0) return res;
    memcpy(out->name, dirent.name, GLFS_MAX_FILENAME_LENGTH);
    out->name[GLFS_MAX_FILENAME_LENGTH] = '\0'; // sizeof(dirent_t.name) = 256, GLFS max filename = 248
    out->type = inode.type;
    return 1;
}

int glfs_stat(glfs_mount_t* mount, const char *path, glfs_stat_t *out) {
    if (!path) return -EINVAL;
    if (!out) return -EINVAL;
    glfs_dirent_ref_t dirent_ref;
    int res = glfs_get_dirent(mount, path, &dirent_ref);
    if (res < 0) return res;
    glfs_dirent_t dirent = {0};
    if (dirent_ref.inode) {
        res = glfs_read_inode(mount, dirent_ref.inode, (uint8_t*)&dirent, dirent_ref.index * 256, 256);
        if (res < 0) return res;
    } else {
        dirent.inodeptr = mount->superblock.root_inode;
    }
    glfs_inode_t inode;
    res = glfs_read_block(mount, dirent.inodeptr, &inode);
    if (res < 0) return res;
    out->inode = dirent.inodeptr;
    out->type = inode.type;
    out->perms = inode.perms;
    out->uid = inode.uid;
    out->gid = inode.gid;
    out->size = inode.size;
    out->ctime = inode.ctime;
    out->mtime = inode.mtime;
    out->rdev = inode.rdev;
    return 0;
}

int64_t glfs_read(glfs_mount_t* mount, const char *path, void *buffer, uint64_t offset, uint64_t size) {
    if (!path) return -EINVAL;
    if (!buffer) return -EINVAL;
    glfs_dirent_ref_t dirent_ref;
    int res = glfs_get_dirent(mount, path, &dirent_ref);
    if (res < 0) return res;
    glfs_dirent_t dirent = {0};
    if (dirent_ref.inode) {
        res = glfs_read_inode(mount, dirent_ref.inode, (uint8_t*)&dirent, dirent_ref.index * 256, 256);
        if (res < 0) return res;
    } else {
        dirent.inodeptr = mount->superblock.root_inode;
    }
    return glfs_read_inode(mount, dirent.inodeptr, buffer, offset, size);
}

int64_t glfs_write(glfs_mount_t* mount, const char *path, const void *buffer, uint64_t offset, uint64_t size) {
    if (mount->read_only) return -EROFS;
    if (!buffer) return -EINVAL;
    if (!path) return -EINVAL;
    glfs_dirent_ref_t dirent_ref;
    int res = glfs_get_dirent(mount, path, &dirent_ref);
    if (res < 0) return res;
    glfs_dirent_t dirent = {0};
    if (dirent_ref.inode) {
        res = glfs_read_inode(mount, dirent_ref.inode, (uint8_t*)&dirent, dirent_ref.index * 256, 256);
        if (res < 0) return res;
    } else {
        dirent.inodeptr = mount->superblock.root_inode;
    }
    return glfs_write_inode(mount, dirent.inodeptr, buffer, offset, size);
}

static int _glfs_link(glfs_mount_t* mount, const char *path, uint64_t inode_number, uint64_t allow_dirs) {
    if (mount->read_only) return -EROFS;
    if (!path) return -EINVAL;
    glfs_stat_t st;
    if (glfs_stat(mount, path, &st) >= 0) return -EEXIST;
    size_t len = glfs_strlen(path);
    while (len > 0 && path[len - 1] == '/') {
        len--;
    }
    size_t slash = len;
    while (slash > 0 && path[slash - 1] != '/') {
        slash--;
    }
    char dirname[1024];
    char filename[GLFS_MAX_FILENAME_LENGTH] = {0};
    if (slash == 0) {
        dirname[0] = '\0';
    } else {
        size_t dlen = slash - 1;
        if (dlen >= 1024) return -ENAMETOOLONG;
        memcpy(dirname, path, dlen);
        dirname[dlen] = '\0';
    }
    size_t flen = len - slash;
    if (flen > GLFS_MAX_FILENAME_LENGTH) return -ENAMETOOLONG;
    if (flen == 0) return -EINVAL;
    memcpy(filename, path + slash, flen);

    glfs_dirent_ref_t dirent_ref;
    int res = glfs_get_dirent(mount, dirname, &dirent_ref);
    if (res < 0) return res;
    glfs_dirent_t dir = {0};
    if (dirent_ref.inode) {
        res = glfs_read_inode(mount, dirent_ref.inode, (uint8_t*)&dir, dirent_ref.index * 256, 256);
        if (res < 0) return res;
    } else {
        dir.inodeptr = mount->superblock.root_inode;
    }
    glfs_inode_t dir_inode;
    res = glfs_read_block(mount, dir.inodeptr, &dir_inode);
    if (res < 0) return res;
    if (dir_inode.size % 256 != 0) return -EIO;

    // Read inode
    glfs_inode_t inode = {0};
    res = glfs_read_block(mount, inode_number, &inode);
    if (res < 0) return res;

    if (inode.type == GLFS_DIR && !allow_dirs) return -EISDIR;

    // Increment inode refcount
    inode.refcount++;
    res = glfs_write_block(mount, inode_number, &inode);
    if (res < 0) return res;
    if (mount->backing.sync) mount->backing.sync(mount->backing.data);

    // Write the dirent
    glfs_dirent_t new_dirent = {0};
    new_dirent.inodeptr = inode_number;
    memcpy(new_dirent.name, filename, GLFS_MAX_FILENAME_LENGTH);
    res = glfs_write_inode(mount, dir.inodeptr, (uint8_t*)(&new_dirent), dir_inode.size, 256);
    if (res < 0) return res;
    if (mount->backing.sync) mount->backing.sync(mount->backing.data);

    return 0;
}

int glfs_mknod(glfs_mount_t* mount, const char *path, uint32_t type, uint64_t dev) {
    if (mount->read_only) return -EROFS;
    if (!path) return -EINVAL;
    uint64_t inode_number;
    glfs_inode_t inode = {0};
    int res = glfs_block_alloc(mount, &inode_number);
    if (res < 0) return res;
    uint64_t sig;
    memcpy(&sig, "GLFS_INO", 8);
    sig ^= inode_number;
    memcpy(&inode.signature, &sig, 8);
    inode.refcount = 0;
    inode.perms = 0755;
    inode.type = type;
    inode.rdev = dev;
    res = glfs_write_block(mount, inode_number, &inode);
    if (res < 0) {
        glfs_block_free(mount, inode_number);
        return res;
    }
    res = _glfs_link(mount, path, inode_number, 1);
    if (res < 0) glfs_block_free(mount, inode_number);
    return res;
}

int glfs_create_file(glfs_mount_t* mount, const char *path) {
    return glfs_mknod(mount, path, GLFS_REG, 0);
}

int glfs_create_directory(glfs_mount_t* mount, const char *path) {
    return glfs_mknod(mount, path, GLFS_DIR, 0);
}

int glfs_delete(glfs_mount_t* mount, const char *path) {
    if (mount->read_only) return -EROFS;
    if (!path) return -EINVAL;
    size_t len = glfs_strlen(path);
    while (len > 0 && path[len - 1] == '/') {
        len--;
    }
    size_t slash = len;
    while (slash > 0 && path[slash - 1] != '/') {
        slash--;
    }
    char dirname[1024];
    if (slash == 0) {
        dirname[0] = '\0';
    } else {
        size_t dlen = slash - 1;
        if (dlen >= 1024) return -ENAMETOOLONG;
        memcpy(dirname, path, dlen);
        dirname[dlen] = '\0';
    }

    glfs_dirent_ref_t dirent_ref;
    int res = glfs_get_dirent(mount, dirname, &dirent_ref);
    if (res < 0) return res;
    glfs_dirent_t dir = {0};
    if (dirent_ref.inode) {
        res = glfs_read_inode(mount, dirent_ref.inode, (uint8_t*)&dir, dirent_ref.index * 256, 256);
        if (res < 0) return res;
    } else {
        dir.inodeptr = mount->superblock.root_inode;
    }
    glfs_inode_t dir_inode;
    res = glfs_read_block(mount, dir.inodeptr, &dir_inode);
    if (res < 0) return res;
    if (dir_inode.size % 256 != 0) return -EIO;

    res = glfs_get_dirent(mount, path, &dirent_ref);
    if (res < 0) return res;
    glfs_dirent_t dirent = {0};
    if (!dirent_ref.inode) return -EINVAL; // Cannot delete root
    res = glfs_read_inode(mount, dirent_ref.inode, (uint8_t*)&dirent, dirent_ref.index * 256, 256);
    if (res < 0) return res;
    glfs_inode_t inode;
    res = glfs_read_block(mount, dirent.inodeptr, &inode);
    if (res < 0) return res;

    // Delete dirent
    glfs_dirent_t last_dirent;
    res = glfs_read_inode(mount, dir.inodeptr, (uint8_t*)&last_dirent, dir_inode.size - 256, 256);
    if (res < 0) return res;
    if (memcmp(&last_dirent, &dirent, 256)) { // If last dirent is not the one to be deleted
        res = glfs_write_inode(mount, dir.inodeptr, (uint8_t*)&last_dirent, dirent_ref.index * 256, 256);
        if (res < 0) return res;
    }
    dir_inode.size -= 256;
    res = glfs_write_block(mount, dir.inodeptr, &dir_inode);
    if (res < 0) return res;
    if (mount->backing.sync) mount->backing.sync(mount->backing.data);

    // Delete inode if refcount <= 1
    // In this section, there is no error handling
    // because the dirent is already removed,
    // and the inode can't be recovered on failure since its blocks
    // may have already been freed. The worst case is a stale inode or leaked blocks.
    if (inode.refcount <= 1) {
        glfs_block_free(mount, dirent.inodeptr);
        for (int i = 0; i < sizeof(inode.blocks) / 8 && i < inode.block_count; i++) {
            if (inode.blocks[i] == 0) continue;
            glfs_block_free(mount, inode.blocks[i]);
        }
        glfs_inode_continuation_t cont;
        cont.next_inode_block = inode.next_inode_block;
        while (cont.next_inode_block) {
            uint64_t current = cont.next_inode_block;
            res = glfs_read_block(mount, current, &cont);
            if (res < 0) return res;
            for (int i = 0; i < sizeof(cont.blocks) / 8; i++) {
                if (cont.blocks[i] == 0) continue;
                glfs_block_free(mount, cont.blocks[i]);
            }
            glfs_block_free(mount, current);
        }
        inode = (glfs_inode_t){0};
        glfs_write_block(mount, dirent.inodeptr, &inode);
    } else {
        inode.refcount--;
        glfs_write_block(mount, dirent.inodeptr, &inode);
    }
    if (mount->backing.sync) mount->backing.sync(mount->backing.data);

    return 0;
}

int glfs_rename(glfs_mount_t* mount, const char *old_path, const char *new_path) {
    if (!old_path) return -EINVAL;
    if (!new_path) return -EINVAL;
    if (mount->read_only) return -EROFS;
    glfs_dirent_ref_t dirent_ref;
    int res = glfs_get_dirent(mount, old_path, &dirent_ref);
    if (res < 0) return res;
    glfs_dirent_t old_dirent = {0};
    if (dirent_ref.inode) {
        res = glfs_read_inode(mount, dirent_ref.inode, (uint8_t*)&old_dirent, dirent_ref.index * 256, 256);
        if (res < 0) return res;
    } else {
        old_dirent.inodeptr = mount->superblock.root_inode;
    }
    res = _glfs_link(mount, new_path, old_dirent.inodeptr, 1);
    if (res < 0) return res;
    res = glfs_delete(mount, old_path);
    if (res < 0) glfs_delete(mount, new_path);
    return res;
}

int glfs_link(glfs_mount_t *mount, const char *path, const char *link) {
    if (mount->read_only) return -EROFS;
    if (!path) return -EINVAL;
    glfs_dirent_ref_t dirent_ref;
    int res = glfs_get_dirent(mount, path, &dirent_ref);
    if (res < 0) return res;
    glfs_dirent_t dirent = {0};
    if (dirent_ref.inode) {
        res = glfs_read_inode(mount, dirent_ref.inode, (uint8_t*)&dirent, dirent_ref.index * 256, 256);
        if (res < 0) return res;
    } else {
        dirent.inodeptr = mount->superblock.root_inode;
    }
    return _glfs_link(mount, link, dirent.inodeptr, 0);
}
