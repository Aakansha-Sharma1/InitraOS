#ifndef INITRAOS_FS_H
#define INITRAOS_FS_H

/*
 * InitraOS filesystem abstraction.
 *
 * A filesystem implementation provides these operations to the VFS.
 * InitraFS will implement this interface first.
 */

#include "block.h"
#include "inode.h"

typedef struct filesystem filesystem_t;

typedef struct
{
    unsigned int magic;
    unsigned int version;
    unsigned int block_size;
    unsigned int total_blocks;

    unsigned int inode_count;
    unsigned int free_block_count;

    unsigned int inode_table_start;
    unsigned int inode_table_blocks;

    unsigned int data_start;
    inode_number_t root_inode;

    unsigned int filesystem_flags;
} initrafs_superblock_t;

/*
 * Directory entry metadata.
 *
 * The filename is stored as part of the directory entry rather
 * than inside the inode.
 */
typedef struct
{
    inode_number_t inode_number;
    unsigned int type;
    unsigned int name_length;
} initrafs_dirent_t;

/*
 * Filesystem implementation registration.
 */

int filesystem_register(
    filesystem_t *filesystem
);

int filesystem_unregister(
    filesystem_t *filesystem
);

#endif
