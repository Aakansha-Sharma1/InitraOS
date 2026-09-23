#include "initrafs.h"

static void initrafs_zero_bytes(
    void *address,
    unsigned int size
)
{
    unsigned char *bytes =
        (unsigned char *)address;

    for (unsigned int index = 0;
         index < size;
         index++)
    {
        bytes[index] = 0;
    }
}

static void initrafs_copy_name(
    char *destination,
    const char *source,
    unsigned int length
)
{
    for (unsigned int index = 0;
         index < length;
         index++)
    {
        destination[index] =
            source[index];
    }
}

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
        inode_table_start +
        inode_table_blocks;

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

int initrafs_root_inode_init(
    initrafs_disk_inode_t *inode,
    const initrafs_superblock_t *superblock
)
{
    if (inode == 0 ||
        superblock == 0 ||
        superblock->root_inode !=
            INITRAFS_ROOT_INODE ||
        superblock->data_start >=
            superblock->total_blocks)
    {
        return 0;
    }

    initrafs_zero_bytes(
        inode,
        sizeof(initrafs_disk_inode_t)
    );

    inode->inode_number =
        INITRAFS_ROOT_INODE;

    inode->type =
        INITRAFS_TYPE_DIRECTORY;

    inode->mode =
        INITRAFS_ROOT_MODE;

    inode->size =
        INITRAFS_ROOT_DIR_ENTRIES *
        sizeof(initrafs_disk_dirent_t);

    inode->owner = 0;
    inode->group = 0;

    inode->link_count =
        INITRAFS_ROOT_LINK_COUNT;

    inode->direct_blocks[0] =
        superblock->data_start;

    return 1;
}

int initrafs_root_directory_init(
    initrafs_disk_dirent_t *entries,
    unsigned int entry_count
)
{
    if (entries == 0 ||
        entry_count < INITRAFS_ROOT_DIR_ENTRIES)
    {
        return 0;
    }

    initrafs_zero_bytes(
        entries,
        entry_count *
        sizeof(initrafs_disk_dirent_t)
    );

    entries[0].inode_number =
        INITRAFS_ROOT_INODE;

    entries[0].type =
        INITRAFS_TYPE_DIRECTORY;

    entries[0].name_length =
        1U;

    initrafs_copy_name(
        entries[0].name,
        ".",
        1U
    );

    entries[1].inode_number =
        INITRAFS_ROOT_INODE;

    entries[1].type =
        INITRAFS_TYPE_DIRECTORY;

    entries[1].name_length =
        2U;

    initrafs_copy_name(
        entries[1].name,
        "..",
        2U
    );

    return 1;
}