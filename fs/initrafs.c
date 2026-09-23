#include "initrafs.h"
#include "format.h"

int initrafs_superblock_init(
    initrafs_superblock_t *superblock,
    unsigned int total_blocks
)
{
    unsigned int inode_table_start =
        INITRAFS_SUPERBLOCK_BLOCK + 1U;

    unsigned int inode_table_blocks =
        (
            INITRAFS_DEFAULT_INODE_COUNT *
            sizeof(initrafs_disk_inode_t) +
            INITRAFS_BLOCK_SIZE - 1U
        ) / INITRAFS_BLOCK_SIZE;

    unsigned int data_start =
        inode_table_start + inode_table_blocks;

    if (superblock == 0 ||
        total_blocks <= data_start)
    {
        return 0;
    }

    superblock->magic =
        INITRAFS_MAGIC;

    superblock->version =
        INITRAFS_VERSION;

    superblock->block_size =
        INITRAFS_BLOCK_SIZE;

    superblock->total_blocks =
        total_blocks;

    superblock->inode_count =
        INITRAFS_DEFAULT_INODE_COUNT;

    superblock->free_block_count =
        total_blocks - data_start;

    superblock->inode_table_start =
        inode_table_start;

    superblock->inode_table_blocks =
        inode_table_blocks;

    superblock->data_start =
        data_start;

    superblock->root_inode =
        INITRAFS_ROOT_INODE;

    superblock->filesystem_flags =
        INITRAFS_FLAG_CLEAN;

    return 1;
}

int initrafs_superblock_validate(
    const initrafs_superblock_t *superblock
)
{
    unsigned int expected_inode_table_blocks =
        (
            INITRAFS_DEFAULT_INODE_COUNT *
            sizeof(initrafs_disk_inode_t) +
            INITRAFS_BLOCK_SIZE - 1U
        ) / INITRAFS_BLOCK_SIZE;

    unsigned int expected_data_start =
        INITRAFS_SUPERBLOCK_BLOCK +
        1U +
        expected_inode_table_blocks;

    if (superblock == 0)
    {
        return 0;
    }

    if (superblock->magic != INITRAFS_MAGIC ||
        superblock->version != INITRAFS_VERSION ||
        superblock->block_size != INITRAFS_BLOCK_SIZE)
    {
        return 0;
    }

    if (superblock->inode_count !=
            INITRAFS_DEFAULT_INODE_COUNT ||
        superblock->inode_table_start !=
            INITRAFS_SUPERBLOCK_BLOCK + 1U ||
        superblock->inode_table_blocks !=
            expected_inode_table_blocks ||
        superblock->data_start !=
            expected_data_start)
    {
        return 0;
    }

    if (superblock->total_blocks <=
            superblock->data_start ||
        superblock->root_inode !=
            INITRAFS_ROOT_INODE ||
        superblock->free_block_count >
            superblock->total_blocks -
            superblock->data_start)
    {
        return 0;
    }

    if ((superblock->filesystem_flags &
         INITRAFS_FLAG_CLEAN) == 0)
    {
        return 0;
    }

    return 1;
}