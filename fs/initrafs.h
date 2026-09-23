#ifndef INITRAOS_INITRAFS_H
#define INITRAOS_INITRAFS_H

#include "fs.h"
#include "format.h"

#define INITRAFS_DEFAULT_INODE_COUNT 128U

#define INITRAFS_ROOT_MODE        0755U
#define INITRAFS_ROOT_LINK_COUNT  2U
#define INITRAFS_ROOT_DIR_ENTRIES 2U

typedef struct
{
    unsigned char *bitmap;

    unsigned int bitmap_bytes;
    unsigned int total_blocks;
    unsigned int first_data_block;
    unsigned int free_blocks;

    initrafs_superblock_t *superblock;

} initrafs_block_allocator_t;

int initrafs_superblock_init(
    initrafs_superblock_t *superblock,
    unsigned int total_blocks
);

int initrafs_superblock_validate(
    const initrafs_superblock_t *superblock
);

int initrafs_root_inode_init(
    initrafs_disk_inode_t *inode,
    const initrafs_superblock_t *superblock
);

int initrafs_root_directory_init(
    initrafs_disk_dirent_t *entries,
    unsigned int entry_count
);

int initrafs_block_allocator_init(
    initrafs_block_allocator_t *allocator,
    initrafs_superblock_t *superblock,
    unsigned char *bitmap,
    unsigned int bitmap_bytes
);

unsigned int initrafs_block_alloc(
    initrafs_block_allocator_t *allocator
);

int initrafs_block_free(
    initrafs_block_allocator_t *allocator,
    unsigned int block
);

#endif