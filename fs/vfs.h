#ifndef INITRAOS_VFS_H
#define INITRAOS_VFS_H

/*
 * InitraOS Virtual Filesystem interface.
 *
 * The VFS provides a filesystem-independent interface for
 * paths, files, directories, and mounts.
 */

#include "block.h"
#include "inode.h"
#include "file.h"

typedef struct filesystem filesystem_t;

struct filesystem
{
    const char *name;

    int (*mount)(
        filesystem_t *filesystem,
        block_device_t *device
    );

    int (*unmount)(
        filesystem_t *filesystem
    );

    int (*lookup)(
        filesystem_t *filesystem,
        const char *path,
        struct fs_inode **inode
    );

    int (*create)(
        filesystem_t *filesystem,
        const char *path,
        unsigned int type,
        struct fs_inode **inode
    );

    int (*remove)(
        filesystem_t *filesystem,
        const char *path
    );

    int (*read)(
        filesystem_t *filesystem,
        struct fs_inode *inode,
        unsigned int offset,
        void *buffer,
        unsigned int size
    );

    int (*write)(
        filesystem_t *filesystem,
        struct fs_inode *inode,
        unsigned int offset,
        const void *buffer,
        unsigned int size
    );

    int (*mkdir)(
        filesystem_t *filesystem,
        const char *path
    );

    int (*rmdir)(
        filesystem_t *filesystem,
        const char *path
    );

    void *private_data;
};

/*
 * VFS lifecycle.
 */

int vfs_mount(
    filesystem_t *filesystem,
    block_device_t *device
);

int vfs_unmount(
    filesystem_t *filesystem
);

/*
 * File operations.
 */

int vfs_open(
    const char *path,
    unsigned int flags,
    vfs_file_t **file
);

int vfs_close(
    vfs_file_t *file
);

int vfs_read(
    vfs_file_t *file,
    void *buffer,
    unsigned int size
);

int vfs_write(
    vfs_file_t *file,
    const void *buffer,
    unsigned int size
);

int vfs_seek(
    vfs_file_t *file,
    unsigned int position
);

/*
 * Directory operations.
 */

int vfs_mkdir(
    const char *path
);

int vfs_rmdir(
    const char *path
);

int vfs_readdir(
    const char *path,
    void *entry,
    unsigned int entry_size
);

int vfs_unlink(
    const char *path
);

#endif
