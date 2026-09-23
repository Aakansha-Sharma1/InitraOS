#ifndef INITRAOS_INITRAFS_H
#define INITRAOS_INITRAFS_H

#include "fs.h"

#define INITRAFS_DEFAULT_INODE_COUNT 128U

int initrafs_superblock_init(
    initrafs_superblock_t *superblock,
    unsigned int total_blocks
);

int initrafs_superblock_validate(
    const initrafs_superblock_t *superblock
);

#endif