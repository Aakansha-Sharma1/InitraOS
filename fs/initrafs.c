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
static unsigned int initrafs_bitmap_bytes(
    unsigned int block_count
)
{
    return (block_count + 7U) / 8U;
}

static unsigned int initrafs_bitmap_index(
    unsigned int block
)
{
    return block >> 3;
}

static unsigned char initrafs_bitmap_mask(
    unsigned int block
)
{
    return (unsigned char)
        (1U << (block & 7U));
}

static int initrafs_block_is_marked(
    const initrafs_block_allocator_t *allocator,
    unsigned int block
)
{
    return (
        allocator->bitmap[
            initrafs_bitmap_index(block)
        ] &
        initrafs_bitmap_mask(block)
    ) != 0;
}

static void initrafs_block_mark_used(
    initrafs_block_allocator_t *allocator,
    unsigned int block
)
{
    allocator->bitmap[
        initrafs_bitmap_index(block)
    ] |=
        initrafs_bitmap_mask(block);
}

static void initrafs_block_mark_free(
    initrafs_block_allocator_t *allocator,
    unsigned int block
)
{
    allocator->bitmap[
        initrafs_bitmap_index(block)
    ] &=
        (unsigned char)
        ~initrafs_bitmap_mask(block);
}

int initrafs_block_allocator_init(
    initrafs_block_allocator_t *allocator,
    initrafs_superblock_t *superblock,
    unsigned char *bitmap,
    unsigned int bitmap_bytes
)
{
    unsigned int required_bitmap_bytes;

    if (allocator == 0 ||
        superblock == 0 ||
        bitmap == 0)
    {
        return 0;
    }

    if (superblock->total_blocks <=
        superblock->data_start)
    {
        return 0;
    }

    required_bitmap_bytes =
        initrafs_bitmap_bytes(
            superblock->total_blocks
        );

    if (bitmap_bytes <
        required_bitmap_bytes)
    {
        return 0;
    }

    initrafs_zero_bytes(
        bitmap,
        required_bitmap_bytes
    );

    allocator->bitmap =
        bitmap;

    allocator->bitmap_bytes =
        required_bitmap_bytes;

    allocator->total_blocks =
        superblock->total_blocks;

    allocator->first_data_block =
        superblock->data_start;

    /*
     * The first data block belongs to the
     * root directory and is therefore already used.
     */
    initrafs_block_mark_used(
        allocator,
        superblock->data_start
    );

    allocator->free_blocks =
        superblock->total_blocks -
        superblock->data_start -
        1U;

    allocator->superblock =
        superblock;

    superblock->free_block_count =
        allocator->free_blocks;

    return 1;
}

unsigned int initrafs_block_alloc(
    initrafs_block_allocator_t *allocator
)
{
    if (allocator == 0 ||
        allocator->bitmap == 0)
    {
        return 0;
    }

    for (unsigned int block =
             allocator->first_data_block;
         block < allocator->total_blocks;
         block++)
    {
        if (initrafs_block_is_marked(
                allocator,
                block))
        {
            continue;
        }

        initrafs_block_mark_used(
            allocator,
            block
        );

        allocator->free_blocks--;

        allocator->superblock
            ->free_block_count =
            allocator->free_blocks;

        return block;
    }

    return 0;
}

int initrafs_block_free(
    initrafs_block_allocator_t *allocator,
    unsigned int block
)
{
    if (allocator == 0 ||
        allocator->bitmap == 0)
    {
        return 0;
    }

    /*
     * Metadata blocks cannot be freed through
     * the data-block allocator.
     */
    if (block <
            allocator->first_data_block ||
        block >=
            allocator->total_blocks)
    {
        return 0;
    }

    /*
     * The first data block is reserved for
     * the root directory.
     */
    if (block ==
        allocator->first_data_block)
    {
        return 0;
    }

    /*
     * A block that is already free cannot
     * be freed again.
     */
    if (!initrafs_block_is_marked(
            allocator,
            block))
    {
        return 0;
    }

    initrafs_block_mark_free(
        allocator,
        block
    );

    allocator->free_blocks++;

    allocator->superblock
        ->free_block_count =
        allocator->free_blocks;

    return 1;
}
