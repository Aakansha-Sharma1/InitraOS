#ifndef INITRAOS_INITRAFS_H
#define INITRAOS_INITRAFS_H

#include "fs.h"
#include "format.h"

#define INITRAFS_DEFAULT_INODE_COUNT 128U

#define INITRAFS_ROOT_MODE        0755U
#define INITRAFS_ROOT_LINK_COUNT  2U
#define INITRAFS_ROOT_DIR_ENTRIES 2U

/*
 * Runtime instance limits.
 *
 * The first runtime instance implementation supports
 * the current 16 MiB / 32768-block disk geometry.
 */
#define INITRAFS_INSTANCE_MAX_BLOCKS 32768U
#define INITRAFS_INSTANCE_BLOCK_BITMAP_BYTES \
    ((INITRAFS_INSTANCE_MAX_BLOCKS + 7U) / 8U)
#define INITRAFS_INSTANCE_INODE_BITMAP_BYTES \
    ((INITRAFS_DEFAULT_INODE_COUNT + 7U) / 8U)
#define INITRAFS_INSTANCE_DIRECTORY_ENTRIES \
    (INITRAFS_BLOCK_SIZE / sizeof(initrafs_disk_dirent_t))

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


/* ---------- Superblock / Root ---------- */

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


/* ---------- Block allocator ---------- */

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


/* ---------- Inode allocator ---------- */

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


/* ---------- In-memory inode ---------- */

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


/* ---------- Directory entries ---------- */

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


/* ---------- Direct file block mapping ---------- */

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


/* ---------- File creation / I/O ---------- */

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


/* ---------- Directory creation / listing ---------- */

int initrafs_directory_create(
    initrafs_inode_allocator_t *allocator,
    struct fs_inode *inode,
    initrafs_disk_inode_t *disk_inode,
    initrafs_disk_dirent_t *entries,
    unsigned int entry_capacity,
    unsigned int mode,
    inode_number_t parent_inode
);

int initrafs_directory_is_empty(
    const initrafs_disk_dirent_t *entries,
    unsigned int entry_count
);

int initrafs_directory_list(
    const initrafs_disk_dirent_t *entries,
    unsigned int entry_count,
    initrafs_disk_dirent_t *output,
    unsigned int output_capacity,
    unsigned int *listed
);


/* ---------- In-memory namespace ---------- */

typedef struct
{
    struct fs_inode *inode;
    initrafs_disk_inode_t *disk_inode;

    initrafs_disk_dirent_t *entries;
    unsigned int entry_count;

} initrafs_namespace_node_t;

typedef struct
{
    initrafs_namespace_node_t *nodes;

    unsigned int node_count;
    unsigned int node_capacity;

    inode_number_t root_inode;

} initrafs_namespace_t;

int initrafs_namespace_init(
    initrafs_namespace_t *namespace,
    initrafs_namespace_node_t *nodes,
    unsigned int node_capacity,
    inode_number_t root_inode
);

int initrafs_namespace_register(
    initrafs_namespace_t *namespace,
    struct fs_inode *inode,
    initrafs_disk_inode_t *disk_inode,
    initrafs_disk_dirent_t *entries,
    unsigned int entry_count
);

int initrafs_namespace_unregister(
    initrafs_namespace_t *namespace,
    inode_number_t inode_number
);


/* ---------- Runtime instance ---------- */

typedef struct
{
    block_device_t *device;

    initrafs_superblock_t superblock;

    initrafs_block_allocator_t block_allocator;
    initrafs_inode_allocator_t inode_allocator;

    unsigned char block_bitmap[
        INITRAFS_INSTANCE_BLOCK_BITMAP_BYTES
    ];

    unsigned char inode_bitmap[
        INITRAFS_INSTANCE_INODE_BITMAP_BYTES
    ];

    struct fs_inode root_inode;
    initrafs_disk_inode_t root_disk_inode;
    initrafs_disk_dirent_t root_entries[
        INITRAFS_INSTANCE_DIRECTORY_ENTRIES
    ];

    initrafs_namespace_node_t namespace_nodes[
        INITRAFS_DEFAULT_INODE_COUNT
    ];
    initrafs_namespace_t namespace;

    unsigned int mounted;

} initrafs_instance_t;

int initrafs_instance_init(
    initrafs_instance_t *instance,
    block_device_t *device
);

int initrafs_instance_unmount(
    initrafs_instance_t *instance
);


/* ---------- InitraFS VFS adapter ---------- */

int initrafs_vfs_init(
    filesystem_t *filesystem,
    initrafs_instance_t *instance
);


/* ---------- Path handling ---------- */

int initrafs_path_lookup(
    const initrafs_namespace_t *namespace,
    const char *path,
    inode_number_t *inode_number
);

int initrafs_path_remove_directory(
    initrafs_namespace_t *namespace,
    initrafs_inode_allocator_t *allocator,
    const char *path
);

#endif
