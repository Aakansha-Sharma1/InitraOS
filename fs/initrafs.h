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
    unsigned int total_inodes;
    unsigned int first_allocatable_inode;
    unsigned int free_inodes;

    initrafs_superblock_t *superblock;

} initrafs_inode_allocator_t;

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

int initrafs_inode_allocator_init(
    initrafs_inode_allocator_t *allocator,
    initrafs_superblock_t *superblock,
    unsigned char *bitmap,
    unsigned int bitmap_bytes
);

unsigned int initrafs_inode_alloc(
    initrafs_inode_allocator_t *allocator
);

int initrafs_inode_free(
    initrafs_inode_allocator_t *allocator,
    unsigned int inode
);

int initrafs_inode_init(
    struct fs_inode *inode,
    inode_number_t inode_number,
    unsigned int type,
    unsigned int mode
);

int initrafs_inode_create(
    initrafs_inode_allocator_t *allocator,
    struct fs_inode *inode,
    unsigned int type,
    unsigned int mode
);

int initrafs_directory_lookup(
    const initrafs_disk_dirent_t *entries,
    unsigned int entry_count,
    const char *name,
    inode_number_t *inode_number
);

int initrafs_directory_add(
    initrafs_disk_dirent_t *entries,
    unsigned int entry_count,
    inode_number_t inode_number,
    unsigned int type,
    const char *name
);

int initrafs_directory_remove(
    initrafs_disk_dirent_t *entries,
    unsigned int entry_count,
    const char *name
);

int initrafs_inode_map_block(
    initrafs_disk_inode_t *inode,
    const initrafs_superblock_t *superblock,
    unsigned int logical_block,
    unsigned int physical_block
);

int initrafs_inode_get_block(
    const initrafs_disk_inode_t *inode,
    unsigned int logical_block,
    unsigned int *physical_block
);

int initrafs_inode_unmap_block(
    initrafs_disk_inode_t *inode,
    unsigned int logical_block
);

#endif
int initrafs_file_create(
    initrafs_inode_allocator_t *allocator,
    struct fs_inode *inode,
    initrafs_disk_inode_t *disk_inode,
    unsigned int mode
);

int initrafs_file_read(
    const struct fs_inode *inode,
    const initrafs_disk_inode_t *disk_inode,
    block_device_t *device,
    unsigned int offset,
    void *buffer,
    unsigned int size
);

int initrafs_file_write(
    struct fs_inode *inode,
    initrafs_disk_inode_t *disk_inode,
    initrafs_block_allocator_t *allocator,
    block_device_t *device,
    unsigned int offset,
    const void *buffer,
    unsigned int size
);
