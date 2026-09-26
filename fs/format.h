#ifndef INITRAOS_FS_FORMAT_H
#define INITRAOS_FS_FORMAT_H

/*
 * InitraFS on-disk format
 *
 * This file defines the fixed layout of filesystem metadata.
 * It does not perform disk I/O.
 */

#define INITRAFS_MAGIC        0x494E4653U
#define INITRAFS_VERSION      1U

/*
 * Filesystem blocks are initially based on the
 * underlying logical storage block size.
 *
 * For the first InitraFS implementation we use
 * 512-byte blocks so the format maps cleanly to
 * disk sectors.
 */
#define INITRAFS_BLOCK_SIZE   512U

/*
 * Filesystem-relative block containing the superblock.
 */
#define INITRAFS_SUPERBLOCK_BLOCK 0U

/*
 * Inode numbering.
 *
 * Inode 0 is unused.
 * Inode 1 is the root directory.
 */
#define INITRAFS_UNUSED_INODE 0U
#define INITRAFS_ROOT_INODE   1U

/*
 * Maximum filename stored in a directory entry.
 */
#define INITRAFS_MAX_NAME_LENGTH 48U

/*
 * Filesystem object types.
 */
#define INITRAFS_TYPE_UNKNOWN   0U
#define INITRAFS_TYPE_FILE      1U
#define INITRAFS_TYPE_DIRECTORY 2U
#define INITRAFS_TYPE_SYMLINK   3U
#define INITRAFS_TYPE_DEVICE   4U

/*
 * Filesystem flags.
 */
#define INITRAFS_FLAG_CLEAN     0x00000001U
#define INITRAFS_FLAG_READONLY  0x00000002U

/*
 * Inode flags.
 *
 * System-protected objects cannot be removed through
 * normal filesystem operations.
 */
#define INITRAFS_INODE_FLAG_SYSTEM_PROTECTED 0x00000001U

/*
 * On-disk superblock.
 *
 * No pointers are stored here.
 * Every field must remain a fixed-width value because
 * this structure is written directly to storage.
 */
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
    unsigned int root_inode;

    unsigned int filesystem_flags;

    unsigned char reserved[
        INITRAFS_BLOCK_SIZE - (11U * sizeof(unsigned int))
    ];

} __attribute__((packed)) initrafs_disk_superblock_t;

/*
 * On-disk inode.
 *
 * This is deliberately separate from fs_inode in inode.h.
 * fs_inode is the in-memory representation.
 * This structure is the persistent representation.
 */
typedef struct
{
    unsigned int inode_number;

    unsigned int type;
    unsigned int mode;
    unsigned int flags;

    unsigned int size;

    unsigned int owner;
    unsigned int group;
    unsigned int link_count;

    unsigned int created_time;
    unsigned int modified_time;
    unsigned int accessed_time;

    unsigned int direct_blocks[8];

    unsigned int indirect_block;

    unsigned char reserved[12];

} __attribute__((packed)) initrafs_disk_inode_t;

/*
 * Fixed-size on-disk directory entry.
 *
 * The filename is stored directly in the directory entry.
 */
typedef struct
{
    unsigned int inode_number;
    unsigned int type;
    unsigned int name_length;

    char name[INITRAFS_MAX_NAME_LENGTH];

} __attribute__((packed)) initrafs_disk_dirent_t;

/*
 * Compile-time format checks.
 */
typedef char initrafs_superblock_size_check[
    sizeof(initrafs_disk_superblock_t) == INITRAFS_BLOCK_SIZE
        ? 1
        : -1
];

#endif