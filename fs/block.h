#ifndef INITRAOS_BLOCK_H
#define INITRAOS_BLOCK_H

/*
 * InitraOS block-device interface.
 *
 * Filesystems use logical blocks rather than assuming a particular
 * storage device or transport.
 */

typedef struct block_device block_device_t;

typedef int (*block_read_fn)(
    block_device_t *device,
    unsigned int block,
    void *buffer
);

typedef int (*block_write_fn)(
    block_device_t *device,
    unsigned int block,
    const void *buffer
);

struct block_device
{
    unsigned int block_size;
    unsigned int block_count;

    block_read_fn read;
    block_write_fn write;

    void *private_data;
};

#endif
