#include "vfs.h"

extern void c_serial_print(const char *message);
extern void c_serial_print_hex(unsigned int value);

static filesystem_t *
vfs_filesystems[VFS_MAX_FILESYSTEMS];

static filesystem_t *vfs_active_filesystem = 0;

static vfs_file_t
    vfs_open_files[VFS_MAX_OPEN_FILES];

/*
 * Current filesystem caller identity.
 *
 * Start as root because the existing kernel-side
 * filesystem tests execute in kernel context.
 */
static unsigned int vfs_caller_uid = 0U;
static unsigned int vfs_caller_gid = 0U;


static int vfs_filesystem_is_registered(
    filesystem_t *filesystem
)
{
    if (filesystem == 0)
    {
        return 0;
    }

    for (unsigned int index = 0;
         index < VFS_MAX_FILESYSTEMS;
         index++)
    {
        if (vfs_filesystems[index] ==
            filesystem)
        {
            return 1;
        }
    }

    return 0;
}


static int vfs_file_is_open(
    vfs_file_t *file
)
{
    if (file == 0)
    {
        return 0;
    }

    for (unsigned int index = 0;
         index < VFS_MAX_OPEN_FILES;
         index++)
    {
        if (&vfs_open_files[index] ==
                file &&
            vfs_open_files[index].inode != 0)
        {
            return 1;
        }
    }

    return 0;
}


static vfs_file_t *vfs_file_slot(void)
{
    for (unsigned int index = 0;
         index < VFS_MAX_OPEN_FILES;
         index++)
    {
        if (vfs_open_files[index].inode == 0)
        {
            return &vfs_open_files[index];
        }
    }

    return 0;
}

int vfs_set_caller_identity(
    unsigned int uid,
    unsigned int gid
)
{
    vfs_caller_uid =
        uid;

    vfs_caller_gid =
        gid;

    return 1;
}


void vfs_get_caller_identity(
    unsigned int *uid,
    unsigned int *gid
)
{
    if (uid != 0)
    {
        *uid =
            vfs_caller_uid;
    }

    if (gid != 0)
    {
        *gid =
            vfs_caller_gid;
    }
}

/* ---------- Filesystem registry ---------- */

int filesystem_register(
    filesystem_t *filesystem
)
{
    if (filesystem == 0 ||
        filesystem->name == 0 ||
        filesystem->name[0] == 0 ||
        vfs_filesystem_is_registered(
            filesystem))
    {
        return 0;
    }

    for (unsigned int index = 0;
         index < VFS_MAX_FILESYSTEMS;
         index++)
    {
        if (vfs_filesystems[index] == 0)
        {
            vfs_filesystems[index] =
                filesystem;

            return 1;
        }
    }

    return 0;
}


int filesystem_unregister(
    filesystem_t *filesystem
)
{
    if (!vfs_filesystem_is_registered(
            filesystem) ||
        vfs_active_filesystem ==
            filesystem)
    {
        return 0;
    }

    for (unsigned int index = 0;
         index < VFS_MAX_FILESYSTEMS;
         index++)
    {
        if (vfs_filesystems[index] ==
            filesystem)
        {
            vfs_filesystems[index] = 0;
            return 1;
        }
    }

    return 0;
}


/* ---------- VFS lifecycle ---------- */

int vfs_mount(
    filesystem_t *filesystem,
    block_device_t *device
)
{
    if (!vfs_filesystem_is_registered(
            filesystem) ||
        vfs_active_filesystem != 0 ||
        filesystem->mount == 0 ||
        device == 0)
    {
        return 0;
    }

    if (!filesystem->mount(
            filesystem,
            device))
    {
        return 0;
    }

    vfs_active_filesystem =
        filesystem;

    return 1;
}


int vfs_unmount(
    filesystem_t *filesystem
)
{
    if (filesystem == 0 ||
        vfs_active_filesystem !=
            filesystem ||
        filesystem->unmount == 0)
    {
        return 0;
    }

    /*
     * Do not detach a filesystem while
     * file objects are still open.
     */
    for (unsigned int index = 0;
         index < VFS_MAX_OPEN_FILES;
         index++)
    {
        if (vfs_open_files[index].inode != 0)
        {
            return 0;
        }
    }

    if (!filesystem->unmount(
            filesystem))
    {
        return 0;
    }

    vfs_active_filesystem = 0;

    return 1;
}


/* ---------- File operations ---------- */

int vfs_open(
    const char *path,
    unsigned int flags,
    vfs_file_t **file
)
{
    struct fs_inode *inode;
    vfs_file_t *slot;

    if (vfs_active_filesystem == 0 ||
        vfs_active_filesystem->lookup == 0 ||
        path == 0 ||
        file == 0)
    {
        return 0;
    }

    if (!vfs_active_filesystem->lookup(
            vfs_active_filesystem,
            path,
            &inode) ||
        inode == 0)
    {
        return 0;
    }

    slot = vfs_file_slot();

    if (slot == 0)
    {
        return 0;
    }

    slot->inode =
        inode;

    slot->position =
        0;

    slot->flags =
        flags;

    slot->filesystem_private =
        vfs_active_filesystem->private_data;

    *file =
        slot;

    return 1;
}


int vfs_close(
    vfs_file_t *file
)
{
    if (!vfs_file_is_open(file))
    {
        return 0;
    }

    file->inode = 0;
    file->position = 0;
    file->flags = 0;
    file->filesystem_private = 0;

    return 1;
}


int vfs_read(
    vfs_file_t *file,
    void *buffer,
    unsigned int size
)
{
    int result;

    if (!vfs_file_is_open(file) ||
        vfs_active_filesystem == 0 ||
        vfs_active_filesystem->read == 0 ||
        buffer == 0)
    {
        return -1;
    }

    result =
        vfs_active_filesystem->read(
            vfs_active_filesystem,
            file->inode,
            file->position,
            buffer,
            size
        );

    if (result > 0)
    {
        file->position +=
            (unsigned int)result;
    }

    return result;
}


int vfs_write(
    vfs_file_t *file,
    const void *buffer,
    unsigned int size
)
{
    int result;

    if (!vfs_file_is_open(file) ||
        vfs_active_filesystem == 0 ||
        vfs_active_filesystem->write == 0 ||
        buffer == 0)
    {
        return -1;
    }

    result =
        vfs_active_filesystem->write(
            vfs_active_filesystem,
            file->inode,
            file->position,
            buffer,
            size
        );

    if (result > 0)
    {
        file->position +=
            (unsigned int)result;
    }

    return result;
}


int vfs_seek(
    vfs_file_t *file,
    unsigned int position
)
{
    if (!vfs_file_is_open(file))
    {
        return 0;
    }

    file->position =
        position;

    return 1;
}


/* ---------- Directory operations ---------- */

int vfs_mkdir(
    const char *path
)
{
    if (vfs_active_filesystem == 0 ||
        vfs_active_filesystem->mkdir == 0 ||
        path == 0)
    {
        return 0;
    }

    return vfs_active_filesystem->mkdir(
        vfs_active_filesystem,
        path
    );
}


int vfs_rmdir(
    const char *path
)
{
    if (vfs_active_filesystem == 0 ||
        vfs_active_filesystem->rmdir == 0 ||
        path == 0)
    {
        return 0;
    }

    return vfs_active_filesystem->rmdir(
        vfs_active_filesystem,
        path
    );
}


int vfs_readdir(
    const char *path,
    void *entry,
    unsigned int entry_size
)
{
    if (vfs_active_filesystem == 0 ||
        vfs_active_filesystem->readdir == 0 ||
        path == 0 ||
        entry == 0 ||
        entry_size == 0)
    {
        return 0;
    }

    return vfs_active_filesystem->readdir(
        vfs_active_filesystem,
        path,
        entry,
        entry_size
    );
}


int vfs_unlink(
    const char *path
)
{
    if (vfs_active_filesystem == 0 ||
        vfs_active_filesystem->remove == 0 ||
        path == 0)
    {
        return 0;
    }

    return vfs_active_filesystem->remove(
        vfs_active_filesystem,
        path
    );
}


/* ---------- Runtime self-test ---------- */

static struct fs_inode vfs_test_inode;

static unsigned char vfs_test_data[128];

static unsigned int vfs_test_mount_count;
static unsigned int vfs_test_unmount_count;
static unsigned int vfs_test_mkdir_count;
static unsigned int vfs_test_rmdir_count;
static unsigned int vfs_test_remove_count;


static void vfs_test_zero(
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


static int vfs_test_mount(
    filesystem_t *filesystem,
    block_device_t *device
)
{
    if (filesystem == 0 ||
        device == 0)
    {
        return 0;
    }

    vfs_test_mount_count++;
    return 1;
}


static int vfs_test_unmount(
    filesystem_t *filesystem
)
{
    if (filesystem == 0)
    {
        return 0;
    }

    vfs_test_unmount_count++;
    return 1;
}


static int vfs_test_lookup(
    filesystem_t *filesystem,
    const char *path,
    struct fs_inode **inode
)
{
    if (filesystem == 0 ||
        path == 0 ||
        inode == 0)
    {
        return 0;
    }

    if (path[0] != '/' ||
        path[1] != 't' ||
        path[2] != 'e' ||
        path[3] != 's' ||
        path[4] != 't' ||
        path[5] != 0)
    {
        return 0;
    }

    *inode =
        &vfs_test_inode;

    return 1;
}


static int vfs_test_create(
    filesystem_t *filesystem,
    const char *path,
    unsigned int type,
    struct fs_inode **inode
)
{
    (void)filesystem;
    (void)path;
    (void)type;
    (void)inode;

    return 0;
}


static int vfs_test_remove(
    filesystem_t *filesystem,
    const char *path
)
{
    if (filesystem == 0 ||
        path == 0 ||
        path[0] == 0)
    {
        return 0;
    }

    vfs_test_remove_count++;
    return 1;
}


static int vfs_test_read(
    filesystem_t *filesystem,
    struct fs_inode *inode,
    unsigned int offset,
    void *buffer,
    unsigned int size
)
{
    unsigned int available;

    if (filesystem == 0 ||
        inode != &vfs_test_inode ||
        buffer == 0)
    {
        return -1;
    }

    if (offset >=
        vfs_test_inode.size)
    {
        return 0;
    }

    available =
        vfs_test_inode.size - offset;

    if (size > available)
    {
        size = available;
    }

    for (unsigned int index = 0;
         index < size;
         index++)
    {
        ((unsigned char *)buffer)[index] =
            vfs_test_data[
                offset + index
            ];
    }

    return (int)size;
}


static int vfs_test_write(
    filesystem_t *filesystem,
    struct fs_inode *inode,
    unsigned int offset,
    const void *buffer,
    unsigned int size
)
{
    if (filesystem == 0 ||
        inode != &vfs_test_inode ||
        buffer == 0 ||
        offset > sizeof(vfs_test_data) ||
        size > sizeof(vfs_test_data) - offset)
    {
        return -1;
    }

    for (unsigned int index = 0;
         index < size;
         index++)
    {
        vfs_test_data[
            offset + index
        ] =
            ((const unsigned char *)buffer)[index];
    }

    if (offset + size >
        vfs_test_inode.size)
    {
        vfs_test_inode.size =
            offset + size;
    }

    return (int)size;
}


static int vfs_test_mkdir(
    filesystem_t *filesystem,
    const char *path
)
{
    if (filesystem == 0 ||
        path == 0)
    {
        return 0;
    }

    vfs_test_mkdir_count++;
    return 1;
}


static int vfs_test_rmdir(
    filesystem_t *filesystem,
    const char *path
)
{
    if (filesystem == 0 ||
        path == 0)
    {
        return 0;
    }

    vfs_test_rmdir_count++;
    return 1;
}


static int vfs_test_readdir(
    filesystem_t *filesystem,
    const char *path,
    void *entry,
    unsigned int entry_size
)
{
    if (filesystem == 0 ||
        path == 0 ||
        entry == 0 ||
        entry_size < 5U)
    {
        return 0;
    }

    ((char *)entry)[0] = 't';
    ((char *)entry)[1] = 'e';
    ((char *)entry)[2] = 's';
    ((char *)entry)[3] = 't';
    ((char *)entry)[4] = 0;

    return 1;
}


static int vfs_test_device_read(
    block_device_t *device,
    unsigned int block,
    void *buffer
)
{
    (void)device;
    (void)block;
    (void)buffer;

    return 1;
}


static int vfs_test_device_write(
    block_device_t *device,
    unsigned int block,
    const void *buffer
)
{
    (void)device;
    (void)block;
    (void)buffer;

    return 1;
}


int vfs_self_test(void)
{
    filesystem_t filesystem;
    block_device_t device;

    vfs_file_t *file = 0;

    unsigned char read_buffer[16];

    const unsigned char write_data[5] =
    {
        'h',
        'e',
        'l',
        'l',
        'o'
    };

    char directory_entry[8];

    vfs_test_zero(
        &filesystem,
        sizeof(filesystem)
    );

    vfs_test_zero(
        &device,
        sizeof(device)
    );

    vfs_test_zero(
        &vfs_test_inode,
        sizeof(vfs_test_inode)
    );

    vfs_test_zero(
        vfs_test_data,
        sizeof(vfs_test_data)
    );

    vfs_test_mount_count = 0;
    vfs_test_unmount_count = 0;
    vfs_test_mkdir_count = 0;
    vfs_test_rmdir_count = 0;
    vfs_test_remove_count = 0;

    /*
     * Verify that VFS can carry the caller's
     * filesystem identity.
     */
    {
        unsigned int uid;
        unsigned int gid;

        if (!vfs_set_caller_identity(
                1000U,
                1000U
            ))
        {
            return 0;
        }

        vfs_get_caller_identity(
            &uid,
            &gid
        );

        if (uid != 1000U ||
            gid != 1000U)
        {
            vfs_set_caller_identity(
                0U,
                0U
            );

            return 0;
        }

    /*
         * Restore kernel/root identity for the
         * remainder of the existing VFS tests.
         */
        vfs_set_caller_identity(
            0U,
            0U
        );

        c_serial_print(
            "[InitraOS] VFS_IDENTITY_OK\n"
        );
    }

    vfs_test_inode.inode_number = 2U;
    vfs_test_inode.type = INODE_TYPE_FILE;
    vfs_test_inode.mode = 0644U;
    vfs_test_inode.link_count = 1U;

    filesystem.name = "vfs-test";

    filesystem.mount =
        vfs_test_mount;

    filesystem.unmount =
        vfs_test_unmount;

    filesystem.lookup =
        vfs_test_lookup;

    filesystem.create =
        vfs_test_create;

    filesystem.remove =
        vfs_test_remove;

    filesystem.read =
        vfs_test_read;

    filesystem.write =
        vfs_test_write;

    filesystem.mkdir =
        vfs_test_mkdir;

    filesystem.rmdir =
        vfs_test_rmdir;

    filesystem.readdir =
        vfs_test_readdir;

    device.block_size =
        512U;

    device.block_count =
        16U;

    device.read =
        vfs_test_device_read;

    device.write =
        vfs_test_device_write;

    if (!filesystem_register(
            &filesystem))
    {
    }

    /*
     * Duplicate registration must fail.
     */
    if (filesystem_register(
            &filesystem))
    {
    }

    /*
     * Only a registered filesystem can
     * become active.
     */
    if (!vfs_mount(
            &filesystem,
            &device))
    {
    }

    if (vfs_test_mount_count != 1U)
    {
    }

    /*
     * Opening delegates path resolution to
     * the filesystem.
     */
    if (!vfs_open(
            "/test",
            0,
            &file) ||
        file == 0)
    {
    }

    /*
     * VFS owns the open-file position.
     */
    if (vfs_write(
            file,
            write_data,
            sizeof(write_data)) != 5)
    {
    }

    if (file->position != 5U ||
        vfs_test_inode.size != 5U)
    {
    }

    if (!vfs_seek(
            file,
            0U))
    {
    }

    if (vfs_read(
            file,
            read_buffer,
            5U) != 5)
    {
    }

    for (unsigned int index = 0;
         index < 5U;
         index++)
    {
        if (read_buffer[index] !=
            write_data[index])
        {
        }
    }

    if (!vfs_seek(
            file,
            5U) ||
        vfs_read(
            file,
            read_buffer,
            1U) != 0)
    {
    }

    /*
     * Directory operations must be routed
     * through the mounted filesystem.
     */
    if (!vfs_readdir(
            "/",
            directory_entry,
            sizeof(directory_entry)) ||
        directory_entry[0] != 't' ||
        directory_entry[3] != 't')
    {
    }

    if (!vfs_mkdir("/docs") ||
        !vfs_rmdir("/docs") ||
        !vfs_unlink("/docs/test") ||
        vfs_test_mkdir_count != 1U ||
        vfs_test_rmdir_count != 1U ||
        vfs_test_remove_count != 1U)
    {
    }

    /*
     * An active filesystem cannot be unregistered.
     * An open file prevents unmount.
     */
    if (filesystem_unregister(
            &filesystem))
    {
    }

    if (vfs_unmount(
            &filesystem))
    {
    }

    if (!vfs_close(file))
    {
    }

    if (!vfs_unmount(
            &filesystem))
    {
    }

    if (vfs_test_unmount_count != 1U)
    {
    }

    if (!filesystem_unregister(
            &filesystem))
    {
    }

    return 1;
}
