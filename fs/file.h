#ifndef INITRAOS_FILE_H
#define INITRAOS_FILE_H

/*
 * InitraOS VFS open-file interface.
 *
 * A file object represents one open instance of a filesystem object.
 * File descriptors will reference these objects on a per-process basis.
 */

struct fs_inode;

typedef struct vfs_file vfs_file_t;

struct vfs_file
{
    struct fs_inode *inode;

    unsigned int position;
    unsigned int flags;

    void *filesystem_private;
};

#endif
