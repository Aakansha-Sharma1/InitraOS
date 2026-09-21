#ifndef INITRAOS_INODE_H
#define INITRAOS_INODE_H

/*
 * InitraOS filesystem inode interface.
 *
 * An inode describes a filesystem object.
 * Directory names are stored separately in directory entries.
 */

typedef unsigned int inode_number_t;

enum inode_type
{
    INODE_TYPE_UNKNOWN = 0,
    INODE_TYPE_FILE,
    INODE_TYPE_DIRECTORY,
    INODE_TYPE_SYMLINK,
    INODE_TYPE_DEVICE
};

struct fs_inode
{
    inode_number_t inode_number;

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

    void *filesystem_private;
};

#endif
