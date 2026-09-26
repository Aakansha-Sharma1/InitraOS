#include "initrafs.h"
#include "vfs.h"

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

static unsigned int initrafs_inode_bitmap_bytes(
    unsigned int inode_count
)
{
    return (inode_count + 7U) / 8U;
}

static unsigned int initrafs_inode_bitmap_index(
    unsigned int inode
)
{
    return inode >> 3;
}

static unsigned char initrafs_inode_bitmap_mask(
    unsigned int inode
)
{
    return (unsigned char)
        (1U << (inode & 7U));
}

static int initrafs_inode_is_marked(
    const initrafs_inode_allocator_t *allocator,
    unsigned int inode
)
{
    return (
        allocator->bitmap[
            initrafs_inode_bitmap_index(inode)
        ] &
        initrafs_inode_bitmap_mask(inode)
    ) != 0;
}

static void initrafs_inode_mark_used(
    initrafs_inode_allocator_t *allocator,
    unsigned int inode
)
{
    allocator->bitmap[
        initrafs_inode_bitmap_index(inode)
    ] |=
        initrafs_inode_bitmap_mask(inode);
}

static void initrafs_inode_mark_free(
    initrafs_inode_allocator_t *allocator,
    unsigned int inode
)
{
    allocator->bitmap[
        initrafs_inode_bitmap_index(inode)
    ] &=
        (unsigned char)
        ~initrafs_inode_bitmap_mask(inode);
}

int initrafs_inode_allocator_init(
    initrafs_inode_allocator_t *allocator,
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

    if (superblock->inode_count <=
        INITRAFS_ROOT_INODE)
    {
        return 0;
    }

    required_bitmap_bytes =
        initrafs_inode_bitmap_bytes(
            superblock->inode_count
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

    allocator->total_inodes =
        superblock->inode_count;

    allocator->first_allocatable_inode =
        INITRAFS_ROOT_INODE + 1U;

    /*
     * Inode 0 is permanently unused.
     */
    initrafs_inode_mark_used(
        allocator,
        INITRAFS_UNUSED_INODE
    );

    /*
     * The root inode already exists and is reserved.
     */
    initrafs_inode_mark_used(
        allocator,
        INITRAFS_ROOT_INODE
    );

    allocator->free_inodes =
        superblock->inode_count -
        allocator->first_allocatable_inode;

    allocator->superblock =
        superblock;

    return 1;
}

unsigned int initrafs_inode_alloc(
    initrafs_inode_allocator_t *allocator
)
{
    if (allocator == 0 ||
        allocator->bitmap == 0)
    {
        return 0;
    }

    for (unsigned int inode =
             allocator->first_allocatable_inode;
         inode < allocator->total_inodes;
         inode++)
    {
        if (initrafs_inode_is_marked(
                allocator,
                inode))
        {
            continue;
        }

        initrafs_inode_mark_used(
            allocator,
            inode
        );

        allocator->free_inodes--;

        return inode;
    }

    return INITRAFS_UNUSED_INODE;
}

int initrafs_inode_free(
    initrafs_inode_allocator_t *allocator,
    unsigned int inode
)
{
    if (allocator == 0 ||
        allocator->bitmap == 0)
    {
        return 0;
    }

    /*
     * Inode 0 and the root inode are reserved.
     */
    if (inode <
            allocator->first_allocatable_inode ||
        inode >=
            allocator->total_inodes)
    {
        return 0;
    }

    /*
     * A free inode cannot be freed again.
     */
    if (!initrafs_inode_is_marked(
            allocator,
            inode))
    {
        return 0;
    }

    initrafs_inode_mark_free(
        allocator,
        inode
    );

    allocator->free_inodes++;

    return 1;
}
static int initrafs_inode_type_valid(
    unsigned int type
)
{
    switch (type)
    {
        case INODE_TYPE_FILE:
        case INODE_TYPE_DIRECTORY:
        case INODE_TYPE_SYMLINK:
        case INODE_TYPE_DEVICE:
            return 1;

        default:
            return 0;
    }
}

int initrafs_inode_init(
    struct fs_inode *inode,
    inode_number_t inode_number,
    unsigned int type,
    unsigned int mode
)
{
    if (inode == 0 ||
        inode_number == INITRAFS_UNUSED_INODE ||
        !initrafs_inode_type_valid(type))
    {
        return 0;
    }

    initrafs_zero_bytes(
        inode,
        sizeof(struct fs_inode)
    );

    inode->inode_number =
        inode_number;

    inode->type =
        type;

    inode->mode =
        mode;

    inode->flags = 0;
    inode->size = 0;

    inode->owner = 0;
    inode->group = 0;

    /*
     * Directories begin with . and ..
     * and therefore have two links.
     * Other objects begin with one link.
     */
    if (type == INODE_TYPE_DIRECTORY)
    {
        inode->link_count = 2U;
    }
    else
    {
        inode->link_count = 1U;
    }

    inode->created_time = 0;
    inode->modified_time = 0;
    inode->accessed_time = 0;

    inode->filesystem_private = 0;

    return 1;
}

int initrafs_inode_create(
    initrafs_inode_allocator_t *allocator,
    struct fs_inode *inode,
    unsigned int type,
    unsigned int mode
)
{
    unsigned int inode_number;

    if (allocator == 0 ||
        inode == 0 ||
        !initrafs_inode_type_valid(type))
    {
        return 0;
    }

    inode_number =
        initrafs_inode_alloc(
            allocator
        );

    if (inode_number ==
        INITRAFS_UNUSED_INODE)
    {
        return 0;
    }

    if (!initrafs_inode_init(
            inode,
            inode_number,
            type,
            mode))
    {
        initrafs_inode_free(
            allocator,
            inode_number
        );

        return 0;
    }

    return 1;
}

int initrafs_inode_check_permission(
    const struct fs_inode *inode,
    unsigned int user,
    unsigned int group,
    unsigned int requested
)
{
    unsigned int allowed = 0;
    unsigned int mode;

    if (inode == 0 ||
        requested == 0 ||
        (requested &
         ~(INITRAFS_PERMISSION_READ |
           INITRAFS_PERMISSION_WRITE |
           INITRAFS_PERMISSION_EXECUTE)) != 0)
    {
        return 0;
    }

    mode =
        inode->mode;

    /*
     * Owner permissions take precedence when
     * the caller owns the inode.
     */
    if (user == inode->owner)
    {
        if ((mode & 0400U) != 0)
        {
            allowed |=
                INITRAFS_PERMISSION_READ;
        }

        if ((mode & 0200U) != 0)
        {
            allowed |=
                INITRAFS_PERMISSION_WRITE;
        }

        if ((mode & 0100U) != 0)
        {
            allowed |=
                INITRAFS_PERMISSION_EXECUTE;
        }
    }
    /*
     * Otherwise use group permissions when the
     * caller belongs to the inode's group.
     */
    else if (group == inode->group)
    {
        if ((mode & 0040U) != 0)
        {
            allowed |=
                INITRAFS_PERMISSION_READ;
        }

        if ((mode & 0020U) != 0)
        {
            allowed |=
                INITRAFS_PERMISSION_WRITE;
        }

        if ((mode & 0010U) != 0)
        {
            allowed |=
                INITRAFS_PERMISSION_EXECUTE;
        }
    }
    /*
     * Everyone else uses the "other" permissions.
     */
    else
    {
        if ((mode & 0004U) != 0)
        {
            allowed |=
                INITRAFS_PERMISSION_READ;
        }

        if ((mode & 0002U) != 0)
        {
            allowed |=
                INITRAFS_PERMISSION_WRITE;
        }

        if ((mode & 0001U) != 0)
        {
            allowed |=
                INITRAFS_PERMISSION_EXECUTE;
        }
    }

    return (
        (allowed & requested) ==
        requested
    );
}

static unsigned int initrafs_name_length(
    const char *name
)
{
    if (name == 0)
    {
        return 0;
    }

    for (unsigned int length = 0;
         length <= INITRAFS_MAX_NAME_LENGTH;
         length++)
    {
        if (name[length] == 0)
        {
            return length;
        }
    }

    return 0;
}

static int initrafs_name_equals(
    const initrafs_disk_dirent_t *entry,
    const char *name,
    unsigned int name_length
)
{
    if (entry == 0 ||
        name == 0 ||
        entry->inode_number ==
            INITRAFS_UNUSED_INODE ||
        entry->name_length != name_length)
    {
        return 0;
    }

    for (unsigned int index = 0;
         index < name_length;
         index++)
    {
        if (entry->name[index] != name[index])
        {
            return 0;
        }
    }

    return 1;
}

static int initrafs_directory_name_reserved(
    const char *name,
    unsigned int name_length
)
{
    if (name_length == 1U &&
        name[0] == '.')
    {
        return 1;
    }

    if (name_length == 2U &&
        name[0] == '.' &&
        name[1] == '.')
    {
        return 1;
    }

    return 0;
}

static int initrafs_directory_type_valid(
    unsigned int type
)
{
    switch (type)
    {
        case INITRAFS_TYPE_FILE:
        case INITRAFS_TYPE_DIRECTORY:
        case INITRAFS_TYPE_SYMLINK:
        case INITRAFS_TYPE_DEVICE:
            return 1;

        default:
            return 0;
    }
}

int initrafs_directory_lookup(
    const initrafs_disk_dirent_t *entries,
    unsigned int entry_count,
    const char *name,
    inode_number_t *inode_number
)
{
    unsigned int name_length;

    if (entries == 0 ||
        entry_count == 0 ||
        name == 0 ||
        inode_number == 0)
    {
        return 0;
    }

    name_length =
        initrafs_name_length(name);

    if (name_length == 0 ||
        name_length > INITRAFS_MAX_NAME_LENGTH)
    {
        return 0;
    }

    for (unsigned int index = 0;
         index < entry_count;
         index++)
    {
        if (!initrafs_name_equals(
                &entries[index],
                name,
                name_length))
        {
            continue;
        }

        *inode_number =
            entries[index].inode_number;

        return 1;
    }

    return 0;
}

int initrafs_directory_add(
    initrafs_disk_dirent_t *entries,
    unsigned int entry_count,
    inode_number_t inode_number,
    unsigned int type,
    const char *name
)
{
    unsigned int name_length;
    unsigned int free_index = entry_count;

    if (entries == 0 ||
        entry_count == 0 ||
        inode_number == INITRAFS_UNUSED_INODE ||
        !initrafs_directory_type_valid(type) ||
        name == 0)
    {
        return 0;
    }

    name_length =
        initrafs_name_length(name);

    if (name_length == 0 ||
        name_length > INITRAFS_MAX_NAME_LENGTH ||
        initrafs_directory_name_reserved(
            name,
            name_length))
    {
        return 0;
    }

    for (unsigned int index = 0;
         index < entry_count;
         index++)
    {
        if (initrafs_name_equals(
                &entries[index],
                name,
                name_length))
        {
            /*
             * A directory cannot contain
             * duplicate names.
             */
            return 0;
        }

        if (free_index == entry_count &&
            entries[index].inode_number ==
                INITRAFS_UNUSED_INODE)
        {
            free_index = index;
        }
    }

    if (free_index == entry_count)
    {
        return 0;
    }

    initrafs_zero_bytes(
        &entries[free_index],
        sizeof(initrafs_disk_dirent_t)
    );

    entries[free_index].inode_number =
        inode_number;

    entries[free_index].type =
        type;

    entries[free_index].name_length =
        name_length;

    initrafs_copy_name(
        entries[free_index].name,
        name,
        name_length
    );

    return 1;
}

int initrafs_directory_remove(
    initrafs_disk_dirent_t *entries,
    unsigned int entry_count,
    const char *name
)
{
    unsigned int name_length;

    if (entries == 0 ||
        entry_count == 0 ||
        name == 0)
    {
        return 0;
    }

    name_length =
        initrafs_name_length(name);

    if (name_length == 0 ||
        name_length > INITRAFS_MAX_NAME_LENGTH ||
        initrafs_directory_name_reserved(
            name,
            name_length))
    {
        return 0;
    }

    for (unsigned int index = 0;
         index < entry_count;
         index++)
    {
        if (!initrafs_name_equals(
                &entries[index],
                name,
                name_length))
        {
            continue;
        }

        initrafs_zero_bytes(
            &entries[index],
            sizeof(initrafs_disk_dirent_t)
        );

        return 1;
    }

    return 0;
}
#define INITRAFS_DIRECT_BLOCK_COUNT 8U

static int initrafs_inode_logical_block_valid(
    unsigned int logical_block
)
{
    return logical_block <
        INITRAFS_DIRECT_BLOCK_COUNT;
}

int initrafs_inode_map_block(
    initrafs_disk_inode_t *inode,
    const initrafs_superblock_t *superblock,
    unsigned int logical_block,
    unsigned int physical_block
)
{
    if (inode == 0 ||
        superblock == 0)
    {
        return 0;
    }

    if (!initrafs_inode_logical_block_valid(
            logical_block))
    {
        return 0;
    }

    /*
     * Physical block 0 is not a data block.
     * The root-directory block is also reserved.
     */
    if (physical_block <=
            superblock->data_start ||
        physical_block >=
            superblock->total_blocks)
    {
        return 0;
    }

    /*
     * Do not overwrite an existing mapping.
     */
    if (inode->direct_blocks[
            logical_block] != 0U)
    {
        return 0;
    }

    inode->direct_blocks[
        logical_block] =
        physical_block;

    return 1;
}

int initrafs_inode_get_block(
    const initrafs_disk_inode_t *inode,
    unsigned int logical_block,
    unsigned int *physical_block
)
{
    if (inode == 0 ||
        physical_block == 0)
    {
        return 0;
    }

    if (!initrafs_inode_logical_block_valid(
            logical_block))
    {
        return 0;
    }

    if (inode->direct_blocks[
            logical_block] == 0U)
    {
        return 0;
    }

    *physical_block =
        inode->direct_blocks[
            logical_block];

    return 1;
}

int initrafs_inode_unmap_block(
    initrafs_disk_inode_t *inode,
    unsigned int logical_block
)
{
    if (inode == 0)
    {
        return 0;
    }

    if (!initrafs_inode_logical_block_valid(
            logical_block))
    {
        return 0;
    }

    if (inode->direct_blocks[
            logical_block] == 0U)
    {
        return 0;
    }

    inode->direct_blocks[
        logical_block] = 0U;

    return 1;
}


static int initrafs_file_inode_pair_valid(
    const struct fs_inode *inode,
    const initrafs_disk_inode_t *disk_inode
)
{
    if (inode == 0 ||
        disk_inode == 0)
    {
        return 0;
    }

    if (inode->inode_number !=
            disk_inode->inode_number ||
        inode->type !=
            INODE_TYPE_FILE ||
        disk_inode->type !=
            INITRAFS_TYPE_FILE ||
        inode->size !=
            disk_inode->size)
    {
        return 0;
    }

    return 1;
}

int initrafs_file_create(
    initrafs_inode_allocator_t *allocator,
    struct fs_inode *inode,
    initrafs_disk_inode_t *disk_inode,
    unsigned int mode
)
{
    unsigned int inode_number;

    if (allocator == 0 ||
        inode == 0 ||
        disk_inode == 0)
    {
        return 0;
    }

    inode_number =
        initrafs_inode_alloc(
            allocator
        );

    if (inode_number ==
        INITRAFS_UNUSED_INODE)
    {
        return 0;
    }

    if (!initrafs_inode_init(
            inode,
            inode_number,
            INODE_TYPE_FILE,
            mode))
    {
        initrafs_inode_free(
            allocator,
            inode_number
        );

        return 0;
    }

    initrafs_zero_bytes(
        disk_inode,
        sizeof(initrafs_disk_inode_t)
    );

    disk_inode->inode_number =
        inode_number;

    disk_inode->type =
        INITRAFS_TYPE_FILE;

    disk_inode->mode =
        mode;

    disk_inode->size = 0;

    disk_inode->owner = 0;
    disk_inode->group = 0;
    disk_inode->link_count = 1U;

    return 1;
}

static void initrafs_file_rollback_blocks(
    initrafs_disk_inode_t *disk_inode,
    initrafs_block_allocator_t *allocator,
    const unsigned int *logical_blocks,
    const unsigned int *physical_blocks,
    unsigned int count
)
{
    if (disk_inode == 0 ||
        allocator == 0 ||
        logical_blocks == 0 ||
        physical_blocks == 0)
    {
        return;
    }

    while (count > 0)
    {
        count--;

        initrafs_inode_unmap_block(
            disk_inode,
            logical_blocks[count]
        );

        initrafs_block_free(
            allocator,
            physical_blocks[count]
        );
    }
}

int initrafs_file_read(
    const struct fs_inode *inode,
    const initrafs_disk_inode_t *disk_inode,
    block_device_t *device,
    unsigned int offset,
    void *buffer,
    unsigned int size
)
{
    unsigned int available;
    unsigned int bytes_done = 0;

    unsigned char block_buffer[
        INITRAFS_BLOCK_SIZE
    ];

    if (!initrafs_file_inode_pair_valid(
            inode,
            disk_inode) ||
        device == 0 ||
        device->read == 0 ||
        buffer == 0 ||
        device->block_size !=
            INITRAFS_BLOCK_SIZE ||
        device->block_count == 0)
    {
        return -1;
    }

    if (size == 0 ||
        offset >= disk_inode->size)
    {
        return 0;
    }

    available =
        disk_inode->size - offset;

    if (size > available)
    {
        size = available;
    }

    while (bytes_done < size)
    {
        unsigned int file_offset =
            offset + bytes_done;

        unsigned int logical_block =
            file_offset /
            INITRAFS_BLOCK_SIZE;

        unsigned int block_offset =
            file_offset %
            INITRAFS_BLOCK_SIZE;

        unsigned int remaining =
            size - bytes_done;

        unsigned int chunk =
            INITRAFS_BLOCK_SIZE -
            block_offset;

        unsigned int physical_block;

        if (chunk > remaining)
        {
            chunk = remaining;
        }

        if (!initrafs_inode_get_block(
                disk_inode,
                logical_block,
                &physical_block))
        {
            return -1;
        }

        if (physical_block >=
            device->block_count)
        {
            return -1;
        }

        if (block_offset == 0 &&
            chunk == INITRAFS_BLOCK_SIZE)
        {
            if (!device->read(
                    device,
                    physical_block,
                    (unsigned char *)buffer +
                    bytes_done))
            {
                return -1;
            }
        }
        else
        {
            if (!device->read(
                    device,
                    physical_block,
                    block_buffer))
            {
                return -1;
            }

            for (unsigned int index = 0;
                 index < chunk;
                 index++)
            {
                ((unsigned char *)buffer)[
                    bytes_done + index
                ] =
                    block_buffer[
                        block_offset + index
                    ];
            }
        }

        bytes_done += chunk;
    }

    return (int)bytes_done;
}

int initrafs_file_write(
    struct fs_inode *inode,
    initrafs_disk_inode_t *disk_inode,
    initrafs_block_allocator_t *allocator,
    block_device_t *device,
    unsigned int offset,
    const void *buffer,
    unsigned int size
)
{
    const unsigned int max_file_size =
        8U * INITRAFS_BLOCK_SIZE;

    unsigned int original_size;
    unsigned int end_position;
    unsigned int first_logical;
    unsigned int last_logical;
    unsigned int bytes_done = 0;

    unsigned int new_logical_blocks[8U];
    unsigned int new_physical_blocks[8U];
    unsigned int new_block_count = 0;

    unsigned char block_buffer[
        INITRAFS_BLOCK_SIZE
    ];

    if (!initrafs_file_inode_pair_valid(
            inode,
            disk_inode) ||
        allocator == 0 ||
        device == 0 ||
        device->read == 0 ||
        device->write == 0 ||
        buffer == 0 ||
        device->block_size !=
            INITRAFS_BLOCK_SIZE ||
        device->block_count == 0)
    {
        return -1;
    }

    if (size == 0)
    {
        return 0;
    }

    if (offset > disk_inode->size)
    {
        /*
         * Sparse writes are intentionally left for
         * a later filesystem step.
         */
        return -1;
    }

    if (disk_inode->size >
        max_file_size)
    {
        return -1;
    }

    if (size >
        max_file_size - offset)
    {
        return -1;
    }

    original_size =
        disk_inode->size;

    end_position =
        offset + size;

    first_logical =
        offset / INITRAFS_BLOCK_SIZE;

    last_logical =
        (end_position - 1U) /
        INITRAFS_BLOCK_SIZE;

    /*
     * Allocate all missing blocks first.
     * If allocation cannot complete, roll back
     * every block allocated by this operation.
     */
    for (unsigned int logical =
             first_logical;
         logical <= last_logical;
         logical++)
    {
        unsigned int physical_block;

        if (initrafs_inode_get_block(
                disk_inode,
                logical,
                &physical_block))
        {
            continue;
        }

        physical_block =
            initrafs_block_alloc(
                allocator
            );

        if (physical_block == 0 ||
            physical_block >=
                device->block_count)
        {
            initrafs_file_rollback_blocks(
                disk_inode,
                allocator,
                new_logical_blocks,
                new_physical_blocks,
                new_block_count
            );

            return -1;
        }

        if (!initrafs_inode_map_block(
                disk_inode,
                allocator->superblock,
                logical,
                physical_block))
        {
            initrafs_block_free(
                allocator,
                physical_block
            );

            initrafs_file_rollback_blocks(
                disk_inode,
                allocator,
                new_logical_blocks,
                new_physical_blocks,
                new_block_count
            );

            return -1;
        }

        new_logical_blocks[
            new_block_count
        ] = logical;

        new_physical_blocks[
            new_block_count
        ] = physical_block;

        new_block_count++;
    }

    /*
     * Write every affected block.
     */
    while (bytes_done < size)
    {
        unsigned int file_offset =
            offset + bytes_done;

        unsigned int logical_block =
            file_offset /
            INITRAFS_BLOCK_SIZE;

        unsigned int block_offset =
            file_offset %
            INITRAFS_BLOCK_SIZE;

        unsigned int remaining =
            size - bytes_done;

        unsigned int chunk =
            INITRAFS_BLOCK_SIZE -
            block_offset;

        unsigned int physical_block;
        int new_block = 0;

        if (chunk > remaining)
        {
            chunk = remaining;
        }

        if (!initrafs_inode_get_block(
                disk_inode,
                logical_block,
                &physical_block) ||
            physical_block >=
                device->block_count)
        {
            initrafs_file_rollback_blocks(
                disk_inode,
                allocator,
                new_logical_blocks,
                new_physical_blocks,
                new_block_count
            );

            return -1;
        }

        for (unsigned int index = 0;
             index < new_block_count;
             index++)
        {
            if (new_logical_blocks[index] ==
                logical_block)
            {
                new_block = 1;
                break;
            }
        }

        if (block_offset == 0 &&
            chunk == INITRAFS_BLOCK_SIZE)
        {
            if (!device->write(
                    device,
                    physical_block,
                    (const unsigned char *)buffer +
                    bytes_done))
            {
                initrafs_file_rollback_blocks(
                    disk_inode,
                    allocator,
                    new_logical_blocks,
                    new_physical_blocks,
                    new_block_count
                );

                return -1;
            }
        }
        else
        {
            if (new_block)
            {
                for (unsigned int index = 0;
                     index < INITRAFS_BLOCK_SIZE;
                     index++)
                {
                    block_buffer[index] = 0;
                }
            }
            else
            {
                if (!device->read(
                        device,
                        physical_block,
                        block_buffer))
                {
                    initrafs_file_rollback_blocks(
                        disk_inode,
                        allocator,
                        new_logical_blocks,
                        new_physical_blocks,
                        new_block_count
                    );

                    return -1;
                }
            }

            for (unsigned int index = 0;
                 index < chunk;
                 index++)
            {
                block_buffer[
                    block_offset + index
                ] =
                    ((const unsigned char *)buffer)[
                        bytes_done + index
                    ];
            }

            if (!device->write(
                    device,
                    physical_block,
                    block_buffer))
            {
                initrafs_file_rollback_blocks(
                    disk_inode,
                    allocator,
                    new_logical_blocks,
                    new_physical_blocks,
                    new_block_count
                );

                return -1;
            }
        }

        bytes_done += chunk;
    }

    /*
     * Keep the in-memory and persistent inode
     * representations synchronized.
     */
    if (end_position > original_size)
    {
        disk_inode->size =
            end_position;

        inode->size =
            end_position;
    }

    return (int)bytes_done;
}


/* ---------- Directory creation / listing ---------- */

int initrafs_directory_create(
    initrafs_inode_allocator_t *allocator,
    struct fs_inode *inode,
    initrafs_disk_inode_t *disk_inode,
    initrafs_disk_dirent_t *entries,
    unsigned int entry_capacity,
    unsigned int mode,
    inode_number_t parent_inode
)
{
    unsigned int inode_number;

    if (allocator == 0 ||
        inode == 0 ||
        disk_inode == 0 ||
        entries == 0 ||
        entry_capacity < INITRAFS_ROOT_DIR_ENTRIES ||
        parent_inode == INITRAFS_UNUSED_INODE)
    {
        return 0;
    }

    inode_number =
        initrafs_inode_alloc(
            allocator
        );

    if (inode_number ==
        INITRAFS_UNUSED_INODE)
    {
        return 0;
    }

    if (!initrafs_inode_init(
            inode,
            inode_number,
            INODE_TYPE_DIRECTORY,
            mode))
    {
        initrafs_inode_free(
            allocator,
            inode_number
        );

        return 0;
    }

    initrafs_zero_bytes(
        disk_inode,
        sizeof(initrafs_disk_inode_t)
    );

    disk_inode->inode_number =
        inode_number;

    disk_inode->type =
        INITRAFS_TYPE_DIRECTORY;

    disk_inode->mode =
        mode;

    disk_inode->size =
        INITRAFS_ROOT_DIR_ENTRIES *
        sizeof(initrafs_disk_dirent_t);

    disk_inode->owner = 0;
    disk_inode->group = 0;
    disk_inode->link_count = 2U;

    if (!initrafs_root_directory_init(
            entries,
            entry_capacity))
    {
        initrafs_inode_free(
            allocator,
            inode_number
        );

        return 0;
    }

    entries[0].inode_number =
        inode_number;

    entries[1].inode_number =
        parent_inode;

    return 1;
}

int initrafs_directory_is_empty(
    const initrafs_disk_dirent_t *entries,
    unsigned int entry_count
)
{
    if (entries == 0 ||
        entry_count < INITRAFS_ROOT_DIR_ENTRIES)
    {
        return 0;
    }

    for (unsigned int index = 0;
         index < entry_count;
         index++)
    {
        if (entries[index].inode_number ==
            INITRAFS_UNUSED_INODE)
        {
            continue;
        }

        if (entries[index].name_length == 1U &&
            entries[index].name[0] == '.')
        {
            continue;
        }

        if (entries[index].name_length == 2U &&
            entries[index].name[0] == '.' &&
            entries[index].name[1] == '.')
        {
            continue;
        }

        return 0;
    }

    return 1;
}

int initrafs_directory_list(
    const initrafs_disk_dirent_t *entries,
    unsigned int entry_count,
    initrafs_disk_dirent_t *output,
    unsigned int output_capacity,
    unsigned int *listed
)
{
    unsigned int required = 0;

    if (entries == 0 ||
        output == 0 ||
        listed == 0 ||
        entry_count == 0)
    {
        return 0;
    }

    for (unsigned int index = 0;
         index < entry_count;
         index++)
    {
        if (entries[index].inode_number !=
            INITRAFS_UNUSED_INODE)
        {
            required++;
        }
    }

    if (output_capacity < required)
    {
        return 0;
    }

    unsigned int output_index = 0;

    for (unsigned int index = 0;
         index < entry_count;
         index++)
    {
        if (entries[index].inode_number ==
            INITRAFS_UNUSED_INODE)
        {
            continue;
        }

        output[output_index++] =
            entries[index];
    }

    *listed =
        output_index;

    return 1;
}


/* ---------- In-memory namespace ---------- */

static initrafs_namespace_node_t *
initrafs_namespace_find(
    const initrafs_namespace_t *namespace,
    inode_number_t inode_number
)
{
    if (namespace == 0 ||
        namespace->nodes == 0)
    {
        return 0;
    }

    for (unsigned int index = 0;
         index < namespace->node_count;
         index++)
    {
        if (namespace->nodes[index].inode == 0 ||
            namespace->nodes[index].disk_inode == 0)
        {
            continue;
        }

        if (namespace->nodes[index].inode->inode_number ==
            inode_number)
        {
            return &namespace->nodes[index];
        }
    }

    return 0;
}

int initrafs_namespace_init(
    initrafs_namespace_t *namespace,
    initrafs_namespace_node_t *nodes,
    unsigned int node_capacity,
    inode_number_t root_inode
)
{
    if (namespace == 0 ||
        nodes == 0 ||
        node_capacity == 0 ||
        root_inode == INITRAFS_UNUSED_INODE)
    {
        return 0;
    }

    initrafs_zero_bytes(
        nodes,
        node_capacity *
        sizeof(initrafs_namespace_node_t)
    );

    namespace->nodes =
        nodes;

    namespace->node_count =
        0;

    namespace->node_capacity =
        node_capacity;

    namespace->root_inode =
        root_inode;

    return 1;
}

int initrafs_namespace_register(
    initrafs_namespace_t *namespace,
    struct fs_inode *inode,
    initrafs_disk_inode_t *disk_inode,
    initrafs_disk_dirent_t *entries,
    unsigned int entry_count
)
{
    if (namespace == 0 ||
        namespace->nodes == 0 ||
        inode == 0 ||
        disk_inode == 0 ||
        inode->inode_number ==
            INITRAFS_UNUSED_INODE ||
        inode->inode_number !=
            disk_inode->inode_number ||
        inode->type !=
            disk_inode->type ||
        namespace->node_count >=
            namespace->node_capacity)
    {
        return 0;
    }

    if (inode->type ==
            INODE_TYPE_DIRECTORY &&
        (entries == 0 ||
         entry_count < INITRAFS_ROOT_DIR_ENTRIES))
    {
        return 0;
    }

    if (initrafs_namespace_find(
            namespace,
            inode->inode_number) != 0)
    {
        return 0;
    }

    initrafs_namespace_node_t *node =
        &namespace->nodes[
            namespace->node_count
        ];

    node->inode =
        inode;

    node->disk_inode =
        disk_inode;

    node->entries =
        entries;

    node->entry_count =
        entry_count;

    namespace->node_count++;

    return 1;
}

int initrafs_namespace_unregister(
    initrafs_namespace_t *namespace,
    inode_number_t inode_number
)
{
    if (namespace == 0 ||
        namespace->nodes == 0 ||
        inode_number == INITRAFS_UNUSED_INODE ||
        inode_number == namespace->root_inode)
    {
        return 0;
    }

    for (unsigned int index = 0;
         index < namespace->node_count;
         index++)
    {
        if (namespace->nodes[index].inode == 0 ||
            namespace->nodes[index].inode->inode_number !=
                inode_number)
        {
            continue;
        }

        for (unsigned int move = index;
             move + 1 < namespace->node_count;
             move++)
        {
            namespace->nodes[move] =
                namespace->nodes[move + 1];
        }

        initrafs_zero_bytes(
            &namespace->nodes[
                namespace->node_count - 1U
            ],
            sizeof(initrafs_namespace_node_t)
        );

        namespace->node_count--;

        return 1;
    }

    return 0;
}


/* ---------- Runtime instance ---------- */

int initrafs_instance_init(
    initrafs_instance_t *instance,
    block_device_t *device
)
{
    if (instance == 0 ||
        device == 0 ||
        device->block_size != INITRAFS_BLOCK_SIZE ||
        device->block_count == 0 ||
        device->block_count >
            INITRAFS_INSTANCE_MAX_BLOCKS)
    {
        return 0;
    }

    initrafs_zero_bytes(
        instance,
        sizeof(initrafs_instance_t)
    );

    if (!initrafs_superblock_init(
            &instance->superblock,
            device->block_count))
    {
        return 0;
    }

    if (!initrafs_block_allocator_init(
            &instance->block_allocator,
            &instance->superblock,
            instance->block_bitmap,
            sizeof(instance->block_bitmap)))
    {
        return 0;
    }

    if (!initrafs_inode_allocator_init(
            &instance->inode_allocator,
            &instance->superblock,
            instance->inode_bitmap,
            sizeof(instance->inode_bitmap)))
    {
        return 0;
    }

    if (!initrafs_root_inode_init(
            &instance->root_disk_inode,
            &instance->superblock))
    {
        return 0;
    }

    if (!initrafs_root_directory_init(
            instance->root_entries,
            INITRAFS_INSTANCE_DIRECTORY_ENTRIES))
    {
        return 0;
    }

    if (!initrafs_inode_init(
            &instance->root_inode,
            INITRAFS_ROOT_INODE,
            INODE_TYPE_DIRECTORY,
            INITRAFS_ROOT_MODE))
    {
        return 0;
    }

    instance->root_inode.size =
        instance->root_disk_inode.size;

    instance->root_inode.link_count =
        instance->root_disk_inode.link_count;

    if (!initrafs_namespace_init(
            &instance->namespace,
            instance->namespace_nodes,
            INITRAFS_DEFAULT_INODE_COUNT,
            INITRAFS_ROOT_INODE))
    {
        return 0;
    }

    if (!initrafs_namespace_register(
            &instance->namespace,
            &instance->root_inode,
            &instance->root_disk_inode,
            instance->root_entries,
            INITRAFS_INSTANCE_DIRECTORY_ENTRIES))
    {
        return 0;
    }

    instance->device =
        device;

    instance->mounted =
        1U;

    return 1;
}


int initrafs_instance_unmount(
    initrafs_instance_t *instance
)
{
    if (instance == 0 ||
        instance->mounted == 0)
    {
        return 0;
    }

    /*
     * No dynamic allocations belong to the instance yet.
     * Clearing the complete runtime object detaches the
     * device and releases all in-memory filesystem state.
     */
    initrafs_zero_bytes(
        instance,
        sizeof(initrafs_instance_t)
    );

    return 1;
}

/* ---------- InitraFS VFS adapter ---------- */

static initrafs_instance_t *
initrafs_vfs_instance(
    filesystem_t *filesystem
)
{
    if (filesystem == 0 ||
        filesystem->private_data == 0)
    {
        return 0;
    }

    return (initrafs_instance_t *)
        filesystem->private_data;
}


static int initrafs_vfs_mount(
    filesystem_t *filesystem,
    block_device_t *device
)
{
    initrafs_instance_t *instance =
        initrafs_vfs_instance(
            filesystem
        );

    if (instance == 0 ||
        device == 0)
    {
        return 0;
    }

    if (instance->mounted != 0)
    {
        return 0;
    }

    return initrafs_instance_init(
        instance,
        device
    );
}


static int initrafs_vfs_unmount(
    filesystem_t *filesystem
)
{
    initrafs_instance_t *instance =
        initrafs_vfs_instance(
            filesystem
        );

    if (instance == 0)
    {
        return 0;
    }

    return initrafs_instance_unmount(
        instance
    );
}


static int initrafs_vfs_lookup(
    filesystem_t *filesystem,
    const char *path,
    struct fs_inode **inode
)
{
    initrafs_instance_t *instance;
    inode_number_t inode_number;
    initrafs_namespace_node_t *node;

    instance =
        initrafs_vfs_instance(
            filesystem
        );

    if (instance == 0 ||
        instance->mounted == 0 ||
        path == 0 ||
        inode == 0)
    {
        return 0;
    }

    if (!initrafs_path_lookup(
            &instance->namespace,
            path,
            &inode_number))
    {
        return 0;
    }

    node =
        initrafs_namespace_find(
            &instance->namespace,
            inode_number
        );

    if (node == 0 ||
        node->inode == 0)
    {
        return 0;
    }

    *inode =
        node->inode;

    return 1;
}


static int initrafs_vfs_read(
    filesystem_t *filesystem,
    struct fs_inode *inode,
    unsigned int offset,
    void *buffer,
    unsigned int size
)
{
    initrafs_instance_t *instance;
    initrafs_namespace_node_t *node;

    instance =
        initrafs_vfs_instance(
            filesystem
        );

    if (instance == 0 ||
        instance->mounted == 0 ||
        inode == 0 ||
        buffer == 0)
    {
        return -1;
    }

    node =
        initrafs_namespace_find(
            &instance->namespace,
            inode->inode_number
        );

    if (node == 0 ||
        node->inode != inode ||
        node->disk_inode == 0)
    {
        return -1;
    }

    return initrafs_file_read(
        node->inode,
        node->disk_inode,
        instance->device,
        offset,
        buffer,
        size
    );
}


static int initrafs_vfs_write(
    filesystem_t *filesystem,
    struct fs_inode *inode,
    unsigned int offset,
    const void *buffer,
    unsigned int size
)
{
    initrafs_instance_t *instance;
    initrafs_namespace_node_t *node;

    instance =
        initrafs_vfs_instance(
            filesystem
        );

    if (instance == 0 ||
        instance->mounted == 0 ||
        inode == 0 ||
        buffer == 0)
    {
        return -1;
    }

    node =
        initrafs_namespace_find(
            &instance->namespace,
            inode->inode_number
        );

    if (node == 0 ||
        node->inode != inode ||
        node->disk_inode == 0)
    {
        return -1;
    }

    return initrafs_file_write(
        node->inode,
        node->disk_inode,
        &instance->block_allocator,
        instance->device,
        offset,
        buffer,
        size
    );
}


static int initrafs_vfs_readdir(
    filesystem_t *filesystem,
    const char *path,
    void *entry,
    unsigned int entry_size
)
{
    initrafs_instance_t *instance;
    struct fs_inode *inode;
    initrafs_namespace_node_t *node;
    unsigned int listed = 0;
    unsigned int capacity;

    if (filesystem == 0 ||
        path == 0 ||
        entry == 0 ||
        entry_size < sizeof(initrafs_disk_dirent_t))
    {
        return 0;
    }

    instance =
        initrafs_vfs_instance(
            filesystem
        );

    if (instance == 0 ||
        instance->mounted == 0)
    {
        return 0;
    }

    if (!initrafs_vfs_lookup(
            filesystem,
            path,
            &inode))
    {
        return 0;
    }

    if (inode->type != INODE_TYPE_DIRECTORY)
    {
        return 0;
    }

    node =
        initrafs_namespace_find(
            &instance->namespace,
            inode->inode_number
        );

    if (node == 0 ||
        node->inode != inode ||
        node->disk_inode == 0 ||
        node->entries == 0 ||
        node->entry_count == 0)
    {
        return 0;
    }

    capacity =
        entry_size /
        sizeof(initrafs_disk_dirent_t);

    if (!initrafs_directory_list(
            node->entries,
            node->entry_count,
            (initrafs_disk_dirent_t *)entry,
            capacity,
            &listed))
    {
        return 0;
    }

    return (int)listed;
}

static int initrafs_vfs_rmdir(
    filesystem_t *filesystem,
    const char *path
)
{
    initrafs_instance_t *instance;

    if (filesystem == 0 ||
        path == 0)
    {
        return 0;
    }

    instance =
        initrafs_vfs_instance(
            filesystem
        );

    if (instance == 0 ||
        instance->mounted == 0)
    {
        return 0;
    }

    return initrafs_path_remove_directory(
        &instance->namespace,
        &instance->inode_allocator,
        path
    );
}


int initrafs_vfs_init(
    filesystem_t *filesystem,
    initrafs_instance_t *instance
)
{
    if (filesystem == 0 ||
        instance == 0)
    {
        return 0;
    }

    initrafs_zero_bytes(
        filesystem,
        sizeof(filesystem_t)
    );

    filesystem->name =
        "initrafs";

    filesystem->mount =
        initrafs_vfs_mount;

    filesystem->unmount =
        initrafs_vfs_unmount;

    filesystem->lookup =
        initrafs_vfs_lookup;

    filesystem->create =
        0;

    filesystem->remove =
        0;

    filesystem->read =
        initrafs_vfs_read;

    filesystem->write =
        initrafs_vfs_write;

    filesystem->mkdir =
        0;

    filesystem->rmdir =
        initrafs_vfs_rmdir;

    filesystem->readdir =
        initrafs_vfs_readdir;

    filesystem->private_data =
        instance;

    return 1;
}

/* ---------- Path handling ---------- */

static int initrafs_path_next_component(
    const char *path,
    unsigned int *position,
    char *component
)
{
    unsigned int index = 0;
    unsigned int cursor;

    if (path == 0 ||
        position == 0 ||
        component == 0)
    {
        return 0;
    }

    cursor =
        *position;

    while (path[cursor] == '/')
    {
        cursor++;
    }

    if (path[cursor] == 0)
    {
        *position = cursor;
        return 0;
    }

    while (path[cursor] != 0 &&
           path[cursor] != '/')
    {
        if (index >=
            INITRAFS_MAX_NAME_LENGTH)
        {
            return -1;
        }

        component[index++] =
            path[cursor++];

    }

    component[index] = 0;
    *position = cursor;

    return 1;
}

int initrafs_path_lookup(
    const initrafs_namespace_t *namespace,
    const char *path,
    inode_number_t *inode_number
)
{
    unsigned int position = 0;
    inode_number_t current_inode;
    char component[
        INITRAFS_MAX_NAME_LENGTH + 1U
    ];

    if (namespace == 0 ||
        path == 0 ||
        inode_number == 0 ||
        path[0] == 0)
    {
        return 0;
    }

    current_inode =
        namespace->root_inode;

    while (1)
    {
        int result =
            initrafs_path_next_component(
                path,
                &position,
                component
            );

        if (result == 0)
        {
            *inode_number =
                current_inode;

            return 1;
        }

        if (result < 0)
        {
            return 0;
        }

        initrafs_namespace_node_t *current_node =
            initrafs_namespace_find(
                namespace,
                current_inode
            );

        if (current_node == 0 ||
            current_node->inode == 0 ||
            current_node->disk_inode == 0 ||
            current_node->inode->type !=
                INODE_TYPE_DIRECTORY ||
            current_node->disk_inode->type !=
                INITRAFS_TYPE_DIRECTORY ||
            current_node->entries == 0 ||
            current_node->entry_count == 0)
        {
            return 0;
        }

        if (component[0] == '.' &&
            component[1] == 0)
        {
            continue;
        }

        if (component[0] == '.' &&
            component[1] == '.' &&
            component[2] == 0)
        {
            if (!initrafs_directory_lookup(
                    current_node->entries,
                    current_node->entry_count,
                    "..",
                    &current_inode))
            {
                return 0;
            }

            continue;
        }

        if (!initrafs_directory_lookup(
                current_node->entries,
                current_node->entry_count,
                component,
                &current_inode))
        {
            return 0;
        }
    }
}

int initrafs_path_remove_directory(
    initrafs_namespace_t *namespace,
    initrafs_inode_allocator_t *allocator,
    const char *path
)
{
    inode_number_t child_inode;
    inode_number_t parent_inode;
    initrafs_namespace_node_t *child_node;
    initrafs_namespace_node_t *parent_node;
    const char *last_component;
    unsigned int position;
    unsigned int component_start;
    char component[
        INITRAFS_MAX_NAME_LENGTH + 1U
    ];

    if (namespace == 0 ||
        allocator == 0 ||
        path == 0 ||
        path[0] == 0)
    {
        return 0;
    }

    if (!initrafs_path_lookup(
            namespace,
            path,
            &child_inode))
    {
        return 0;
    }

    if (child_inode ==
        namespace->root_inode)
    {
        return 0;
    }

    child_node =
        initrafs_namespace_find(
            namespace,
            child_inode
        );

    if (child_node == 0 ||
        child_node->inode == 0 ||
        child_node->disk_inode == 0 ||
        child_node->inode->type !=
            INODE_TYPE_DIRECTORY ||
        child_node->entries == 0)
    {
        return 0;
    }

    if (!initrafs_directory_is_empty(
            child_node->entries,
            child_node->entry_count))
    {
        return 0;
    }

    if (!initrafs_directory_lookup(
            child_node->entries,
            child_node->entry_count,
            "..",
            &parent_inode))
    {
        return 0;
    }

    parent_node =
        initrafs_namespace_find(
            namespace,
            parent_inode
        );

    if (parent_node == 0 ||
        parent_node->inode == 0 ||
        parent_node->disk_inode == 0 ||
        parent_node->entries == 0)
    {
        return 0;
    }

    /*
     * Recover the final path component.
     */
    position = 0;
    component_start = 0;
    last_component = path;

    while (path[position] != 0)
    {
        if (path[position] == '/')
        {
            component_start =
                position + 1U;

            while (path[component_start] == '/')
            {
                component_start++;
            }

            last_component =
                &path[component_start];
        }

        position++;
    }

    /*
     * Normalize the final component through the same
     * validation used by ordinary path lookup.
     */
    position = 0;

    while (path[position] != 0)
    {
        if (path[position] == '/')
        {
            position++;
            continue;
        }

        break;
    }

    if (last_component[0] == 0)
    {
        return 0;
    }

    unsigned int temp_position = 0;
    int component_result;

    /*
     * Scan every component and keep the final one.
     */
    position = 0;
    component[0] = 0;

    while ((component_result =
                initrafs_path_next_component(
                    path,
                    &position,
                    component)) > 0)
    {
        /*
         * The final component is the one remaining when
         * the parser reaches the end of the path.
         */
        temp_position = position;
    }

    (void)temp_position;

    if (component[0] == 0 ||
        (component[0] == '.' &&
         component[1] == 0) ||
        (component[0] == '.' &&
         component[1] == '.' &&
         component[2] == 0))
    {
        return 0;
    }

    /*
     * The directory must be directly linked from its parent.
     * Find the matching parent entry by inode number.
     */
    unsigned int found = 0;

    for (unsigned int index = 0;
         index < parent_node->entry_count;
         index++)
    {
        if (parent_node->entries[index].inode_number ==
            child_inode &&
            parent_node->entries[index].type ==
                INITRAFS_TYPE_DIRECTORY)
        {
            found = 1;
            break;
        }
    }

    if (!found)
    {
        return 0;
    }

    /*
     * Remove the parent entry first.
     */
    if (!initrafs_directory_remove(
            parent_node->entries,
            parent_node->entry_count,
            component))
    {
        return 0;
    }

    /*
     * A child directory contributes one link to
     * its parent. Drop that link now.
     */
    if (parent_node->inode->link_count > 0)
    {
        parent_node->inode->link_count--;
    }

    if (parent_node->disk_inode->link_count > 0)
    {
        parent_node->disk_inode->link_count--;
    }

    if (!initrafs_namespace_unregister(
            namespace,
            child_inode))
    {
        return 0;
    }

    if (!initrafs_inode_free(
            allocator,
            child_inode))
    {
        return 0;
    }

    return 1;
}
