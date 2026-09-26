#include "syscall.h"
#include "program.h"
#include "fs/initrafs.h"
#include "fs/vfs.h"
#include "security.h"

#define KEYBOARD_BUFFER_SIZE 128

#define TASK_READY     0
#define TASK_RUNNING   1
#define TASK_BLOCKED   2
#define TASK_FINISHED  3

#define TASK_KERNEL    0
#define TASK_USER      3

extern char cpu_vendor[13];
extern char __kernel_end;

extern void c_print_string(const char *message);
extern void c_serial_print(const char *message);
extern void c_serial_print_hex(unsigned int value);

extern void enter_user_mode(
    unsigned int entry,
    unsigned int stack_top
);

extern void user_mode_entry(void);
extern unsigned char user_mode_code_start[];
extern unsigned char user_mode_code_end[];
extern unsigned char user_secaudit_code_start[];
extern unsigned char user_secaudit_code_end[];
extern void enable_long_mode(void);

static void paging_init(void);
static void paging_enable(void);
static void page_directory_init(void);
static void page_tables_init(void);
static void frame_paging_test(void);
static void dynamic_page_test(void);
static void heap_paging_test(void);
static void *heap_alloc(unsigned int size);
static void heap_free(void *address);
static void *heap_alloc_paged(unsigned int size);
static void *heap_alloc_paged_aligned(unsigned int size);
static void heap_dynamic_test(void);
static void page_protection_test(void);
static void page_user_protection_test(void);
static void user_stack_test(void);
static void user_region_test(void);
static void process_create_test(void);
static void process_isolation_test(void);
static void process_permission_isolation_test(void);
static void process_instance_isolation_test(void);
static void process_task_link_test(void);
static void syscall_dispatcher_test(void);
static void syscall_memory_test(void);
static void initrafs_superblock_test(void);
static void initrafs_root_test(void);
static void initrafs_block_allocator_test(void);
static void initrafs_inode_allocator_test(void);
static void initrafs_inode_create_test(void);
static void initrafs_directory_test(void);
static void initrafs_inode_block_mapping_test(void);
static void initrafs_file_io_test(void);
static void initrafs_directory_path_test(void);
static void initrafs_instance_test(void);
static void initrafs_vfs_test(void);
static void initrafs_vfs_rmdir_test(void);
static void vfs_test(void);
static void security_core_test(void);
static void security_audit_syscall_test(void);
static void security_resource_access_test(void);


static int page_map(
    unsigned int virtual_address,
    unsigned int physical_address,
    unsigned int flags
);

static int page_unmap(
    unsigned int virtual_address
);

static int heap_map_page(unsigned int virtual_address);
static int heap_unmap_page(unsigned int virtual_address);

static unsigned int page_get_physical(
    unsigned int virtual_address
);

    unsigned int syscall_dispatcher(
    unsigned int syscall_number,
    unsigned int arg1,
    unsigned int arg2,
    unsigned int arg3,
    unsigned int arg4,
    unsigned int arg5
);

/* ---------- Heap ---------- */

typedef struct heap_block
{
    unsigned int size;
    unsigned int free;
    struct heap_block *next;
} heap_block_t;

static int heap_blocks_adjacent(
    heap_block_t *first,
    heap_block_t *second
);

static void *heap_realloc(
    void *address,
    unsigned int size
);

static unsigned int heap_pointer = 0;
static unsigned int heap_limit = 0x80000;
static unsigned int heap_paging_enabled = 0;

static heap_block_t *heap_first_block = 0;

#define HEAP_PAGED_BASE   0x00030000
#define HEAP_PAGED_LIMIT  0x00080000
#define HEAP_PAGE_SIZE    0x1000
#define HEAP_PAGED_MAX_BLOCKS \
    ((HEAP_PAGED_LIMIT - HEAP_PAGED_BASE) / HEAP_PAGE_SIZE)

typedef struct paged_heap_block
{
    unsigned int address;
    unsigned int size;
    unsigned int free;
} paged_heap_block_t;

static paged_heap_block_t
    paged_heap_blocks[HEAP_PAGED_MAX_BLOCKS];

static paged_heap_block_t *paged_heap_find(
    unsigned int address
);

static unsigned int align_up_4k(unsigned int address)
{
    return (address + 0xFFF) & ~0xFFF;
}


/* ---------- Physical Frame Allocator ---------- */

#define E820_ENTRIES_ADDRESS 0x00005004
#define E820_ENTRY_SIZE      24
#define E820_MAX_ENTRIES     32
#define E820_USABLE          1

#define FRAME_SIZE           0x1000
#define FRAME_ALLOC_START    0x00200000
#define FRAME_TRACK_LIMIT    0x08000000
#define FRAME_BITMAP_BYTES   4096

#define USER_CODE_BASE       0x00100000
#define USER_STACK_BASE      0x007FF000

#define USER_CODE_SIZE       0x00001000
#define USER_STACK_SIZE      0x00001000

typedef struct memory_region
{
    unsigned int base;
    unsigned int size;
    unsigned int flags;
} memory_region_t;

#define MEMORY_REGION_USER       0x01
#define MEMORY_REGION_WRITABLE   0x02
#define MEMORY_REGION_EXECUTABLE 0x04

static memory_region_t user_code_region =
{
    USER_CODE_BASE,
    USER_CODE_SIZE,
    MEMORY_REGION_USER |
    MEMORY_REGION_EXECUTABLE
};

static memory_region_t user_stack_region =
{
    USER_STACK_BASE,
    USER_STACK_SIZE,
    MEMORY_REGION_USER |
    MEMORY_REGION_WRITABLE
};

static unsigned char frame_bitmap[FRAME_BITMAP_BYTES];

static unsigned int frame_bitmap_index(unsigned int frame)
{
    return (frame >> 12) >> 3;
}

static unsigned char frame_bitmap_mask(unsigned int frame)
{
    return (unsigned char)(1u << ((frame >> 12) & 7));
}

static int frame_is_tracked(unsigned int frame)
{
    if (frame < FRAME_ALLOC_START ||
        frame >= FRAME_TRACK_LIMIT ||
        (frame & (FRAME_SIZE - 1)) != 0)
        return 0;

    return (frame_bitmap[frame_bitmap_index(frame)] &
            frame_bitmap_mask(frame)) != 0;
}

static void frame_mark_used(unsigned int frame)
{
    frame_bitmap[frame_bitmap_index(frame)] |=
        frame_bitmap_mask(frame);
}

static void frame_mark_free(unsigned int frame)
{
    frame_bitmap[frame_bitmap_index(frame)] &=
        (unsigned char)~frame_bitmap_mask(frame);
}

static int frame_in_usable_e820(unsigned int frame)
{
    for (unsigned int index = 0;
         index < E820_MAX_ENTRIES;
         index++)
    {
        unsigned int entry =
            E820_ENTRIES_ADDRESS +
            index * E820_ENTRY_SIZE;

        unsigned int base = *(unsigned int *)(entry + 0);
        unsigned int base_high = *(unsigned int *)(entry + 4);
        unsigned int length = *(unsigned int *)(entry + 8);
        unsigned int length_high = *(unsigned int *)(entry + 12);
        unsigned int type = *(unsigned int *)(entry + 16);

        if (type != E820_USABLE ||
            base_high != 0 ||
            length_high != 0 ||
            length < FRAME_SIZE)
            continue;

        if (frame >= base &&
            frame - base <= length - FRAME_SIZE)
            return 1;
    }

    return 0;
}

static int frame_is_reserved(unsigned int frame)
{
    unsigned int kernel_start = 0x00008800;
    unsigned int kernel_end =
        align_up_4k((unsigned int)&__kernel_end);

    if (frame < FRAME_ALLOC_START)
        return 1;

    if (frame < kernel_end &&
        frame + FRAME_SIZE > kernel_start)
        return 1;

    if (frame == USER_CODE_BASE ||
        frame == USER_STACK_BASE)
        return 1;

    return 0;
}

static unsigned int frame_alloc(void)
{
    for (unsigned int frame = FRAME_ALLOC_START;
         frame < FRAME_TRACK_LIMIT;
         frame += FRAME_SIZE)
    {
        if (!frame_in_usable_e820(frame) ||
            frame_is_reserved(frame) ||
            frame_is_tracked(frame))
            continue;

        frame_mark_used(frame);
        return frame;
    }

    return 0;
}

static int frame_free(unsigned int frame)
{
    if (!frame_is_tracked(frame))
        return 0;

    if (!frame_in_usable_e820(frame) ||
        frame_is_reserved(frame))
        return 0;

    frame_mark_free(frame);
    return 1;
}

static void frame_allocator_test(void)
{
    unsigned int frame1 = frame_alloc();
    unsigned int frame2 = frame_alloc();

    if (frame1 == 0 || frame2 == 0 ||
        (frame1 & (FRAME_SIZE - 1)) != 0 ||
        (frame2 & (FRAME_SIZE - 1)) != 0 ||
        frame1 == frame2)
    {
        c_serial_print("[InitraOS] FRAME_ALLOC_FAIL\n");
        return;
    }

    c_serial_print("[InitraOS] FRAME_ALLOC_OK\n");
    c_serial_print("[InitraOS] FRAME1: 0x");
    c_serial_print_hex(frame1);
    c_serial_print("\n[InitraOS] FRAME2: 0x");
    c_serial_print_hex(frame2);
    c_serial_print("\n");

    if (!frame_is_tracked(frame1) ||
        !frame_is_tracked(frame2))
    {
        c_serial_print("[InitraOS] FRAME_TRACK_FAIL\n");
        return;
    }

    c_serial_print("[InitraOS] FRAME_TRACK_OK\n");

    if (!frame_free(frame1) || frame_is_tracked(frame1))
    {
        c_serial_print("[InitraOS] FRAME_FREE_FAIL\n");
        return;
    }

    c_serial_print("[InitraOS] FRAME_FREE_OK\n");

    unsigned int reused = frame_alloc();
    if (reused != frame1 || !frame_is_tracked(reused))
    {
        c_serial_print("[InitraOS] FRAME_REUSE_FAIL\n");
        return;
    }

    c_serial_print("[InitraOS] FRAME_REUSE_OK\n");
    frame_free(reused);
    frame_free(frame2);
}

static void frame_protection_test(void)
{
    unsigned int kernel_frame = align_up_4k(0x00008800);
    unsigned int low_frame = 0x00001000;
    unsigned int probe = frame_alloc();

    if (!frame_is_reserved(low_frame) ||
        !frame_is_reserved(kernel_frame) ||
        !frame_is_reserved(USER_CODE_BASE) ||
        !frame_is_reserved(USER_STACK_BASE) ||
        probe == 0)
    {
        if (probe != 0)
            frame_free(probe);
        c_serial_print("[InitraOS] FRAME_PROTECT_FAIL\n");
        return;
    }

    frame_free(probe);
    c_serial_print("[InitraOS] FRAME_PROTECT_OK\n");
}

static void frame_validation_test(void)
{
    unsigned int frame = frame_alloc();

    if (frame == 0 ||
        (frame & (FRAME_SIZE - 1)) != 0 ||
        frame < FRAME_ALLOC_START ||
        frame >= FRAME_TRACK_LIMIT ||
        !frame_in_usable_e820(frame) ||
        !frame_is_tracked(frame))
    {
        c_serial_print("[InitraOS] FRAME_VALIDATION_FAIL\n");
        return;
    }

    if (!frame_free(frame) || frame_is_tracked(frame))
    {
        c_serial_print("[InitraOS] FRAME_VALIDATION_FAIL\n");
        return;
    }

    c_serial_print("[InitraOS] FRAME_VALIDATION_OK\n");
}


/* ---------- Heap Allocation ---------- */

static void *heap_alloc_paged(unsigned int size)
{
    if (size == 0)
    {
        return 0;
    }

    size = (size + 3) & ~3;
    /*
     * Reuse a previously freed paged allocation first.
     */
    for (unsigned int index = 0;
         index < HEAP_PAGED_MAX_BLOCKS;
         index++)
    {
        paged_heap_block_t *slot =
            &paged_heap_blocks[index];

        if (slot->free != 1 ||
            slot->address == 0)
        {
            continue;
        }

        unsigned int address =
            slot->address;

        unsigned int block_start =
            address -
            sizeof(heap_block_t);

        unsigned int end =
            address +
            size;

        if (block_start < HEAP_PAGED_BASE ||
            end < address ||
            end > HEAP_PAGED_LIMIT)
        {
            continue;
        }

        /*
         * The old allocation was freed and its pages
         * were unmapped. Map every page required by
         * the new allocation.
         */
        unsigned int mapped_page_count = 0;

        for (unsigned int page =
                 block_start &
                 ~(HEAP_PAGE_SIZE - 1);
             page < end;
             page += HEAP_PAGE_SIZE)
        {
            if (!heap_map_page(page))
            {
                /*
                 * Roll back pages mapped during this
                 * reuse attempt.
                 */
                for (unsigned int cleanup_page =
                         block_start &
                         ~(HEAP_PAGE_SIZE - 1);
                     cleanup_page <
                     (block_start &
                      ~(HEAP_PAGE_SIZE - 1)) +
                     mapped_page_count *
                         HEAP_PAGE_SIZE;
                     cleanup_page += HEAP_PAGE_SIZE)
                {
                    heap_unmap_page(
                        cleanup_page
                    );
                }

                return 0;
            }

            mapped_page_count++;
        }

        slot->size = size;
        slot->free = 0;

        return (void *)address;
    }

    /*
     * No reusable allocation was found.
     * Allocate at the end of the current heap.
     */
    paged_heap_block_t *slot = 0;

    for (unsigned int index = 0;
         index < HEAP_PAGED_MAX_BLOCKS;
         index++)
    {
        if (paged_heap_blocks[index].free == 1 ||
            paged_heap_blocks[index].address == 0)
        {
            slot = &paged_heap_blocks[index];
            break;
        }
    }

    if (slot == 0)
    {
        return 0;
    }

    unsigned int block_size =
        sizeof(heap_block_t) + size;

    unsigned int start =
        align_up_4k(heap_pointer);

    if (start < HEAP_PAGED_BASE)
    {
        start = HEAP_PAGED_BASE;
    }

    unsigned int end =
        start + block_size;

    if (end < start ||
        end > HEAP_PAGED_LIMIT)
    {
        return 0;
    }

    unsigned int mapped_page_count = 0;

    for (unsigned int page =
             start & ~(HEAP_PAGE_SIZE - 1);
         page < end;
         page += HEAP_PAGE_SIZE)
    {
        if (!heap_map_page(page))
        {
            for (unsigned int cleanup_page =
                     start & ~(HEAP_PAGE_SIZE - 1);
                 cleanup_page <
                 start +
                 mapped_page_count *
                     HEAP_PAGE_SIZE;
                 cleanup_page += HEAP_PAGE_SIZE)
            {
                heap_unmap_page(cleanup_page);
            }

            return 0;
        }

        mapped_page_count++;
    }

    unsigned int address =
        start + sizeof(heap_block_t);

    slot->address = address;
    slot->size = size;
    slot->free = 0;

    heap_pointer = end;

    return (void *)address;
}

static void *heap_alloc_paged_aligned(unsigned int size)
{
    if (size == 0)
    {
        return 0;
    }

    size = (size + 3) & ~3;

    paged_heap_block_t *slot = 0;

    for (unsigned int index = 0;
         index < HEAP_PAGED_MAX_BLOCKS;
         index++)
    {
        if (paged_heap_blocks[index].free == 1 ||
            paged_heap_blocks[index].address == 0)
        {
            slot = &paged_heap_blocks[index];
            break;
        }
    }

    if (slot == 0)
    {
        return 0;
    }

    /*
     * Keep the normal heap header immediately before
     * the returned address, but align the returned
     * payload itself to a 4 KiB boundary.
     */
    unsigned int address =
        align_up_4k(
            heap_pointer +
            sizeof(heap_block_t)
        );

    unsigned int block_start =
        address -
        sizeof(heap_block_t);

    unsigned int end =
        address + size;

    if (block_start < HEAP_PAGED_BASE)
    {
        return 0;
    }

    if (end < address ||
        end > HEAP_PAGED_LIMIT)
    {
        return 0;
    }

    unsigned int mapped_page_count = 0;

    for (unsigned int page =
             block_start & ~(HEAP_PAGE_SIZE - 1);
         page < end;
         page += HEAP_PAGE_SIZE)
    {
        if (!heap_map_page(page))
        {
            for (unsigned int cleanup_page =
                     block_start &
                     ~(HEAP_PAGE_SIZE - 1);
                 cleanup_page <
                     block_start +
                     mapped_page_count *
                         HEAP_PAGE_SIZE;
                 cleanup_page += HEAP_PAGE_SIZE)
            {
                heap_unmap_page(cleanup_page);
            }

            return 0;
        }

        mapped_page_count++;
    }

    heap_block_t *block =
        (heap_block_t *)block_start;

    block->size = size;
    block->free = 0;
    block->next = 0;

    slot->address = address;
    slot->size = size;
    slot->free = 0;

    heap_pointer = end;

    return (void *)address;
}


static void *heap_alloc(unsigned int size)
{
    if (heap_paging_enabled)
{
        return heap_alloc_paged(size);
    }
    if (size == 0)
    {
        return 0;
    }

    /* Align allocation size to 4 bytes */
    size = (size + 3) & ~3;

    /* Look for a previously freed block */
    heap_block_t *current =
        heap_first_block;

    while (current != 0)
    {
        if (current->free == 1 &&
            current->size >= size)
        {
            if (current->size >=
                size + sizeof(heap_block_t) + 1)
            {
                heap_block_t *new_block =
                    (heap_block_t *)
                    ((unsigned int)(current + 1) + size);

                new_block->size =
                    current->size -
                    size -
                    sizeof(heap_block_t);

                new_block->free = 1;
                new_block->next = current->next;

                current->size = size;
                current->next = new_block;
            }

            current->free = 0;

            return (void *)(current + 1);
        }

        current = current->next;
    }

    /* No suitable free block found, allocate new memory */
    if (heap_pointer > heap_limit ||
        heap_limit - heap_pointer <
            sizeof(heap_block_t) ||
        size > (heap_limit - heap_pointer) -
               sizeof(heap_block_t))
    {
        return 0;
    }

    heap_block_t *block =
        (heap_block_t *)heap_pointer;

    block->size = size;
    block->free = 0;
    block->next = 0;

    if (heap_first_block == 0)
    {
        heap_first_block = block;
    }
    else
    {
        current = heap_first_block;

        while (current->next != 0)
        {
            current = current->next;
        }

        current->next = block;
    }

    heap_pointer += sizeof(heap_block_t);

    unsigned int address =
        heap_pointer;

    heap_pointer += size;

    return (void *)address;
}

static paged_heap_block_t *paged_heap_find(
    unsigned int address
)
{
    for (unsigned int index = 0;
         index < HEAP_PAGED_MAX_BLOCKS;
         index++)
    {
        if (paged_heap_blocks[index].address == address)
        {
            return &paged_heap_blocks[index];
        }
    }

    return 0;
}

static void heap_dynamic_test(void)
{
    const unsigned int magic1 = 0x95D1A001;
    const unsigned int magic2 = 0x95D1A002;

    unsigned int address1 =
        (unsigned int)heap_alloc(64);

    if (address1 == 0)
    {
        c_serial_print(
            "[InitraOS] HEAP_DYNAMIC_FAIL\n");
        return;
    }

    volatile unsigned int *value1 =
        (volatile unsigned int *)address1;

    *value1 = magic1;

    if (*value1 != magic1)
    {
        c_serial_print(
            "[InitraOS] HEAP_DYNAMIC_FAIL\n");
        return;
    }

    unsigned int virtual_page1 =
        address1 & ~(HEAP_PAGE_SIZE - 1);

    unsigned int frame1 =
        page_get_physical(virtual_page1);

    if (frame1 == 0 ||
        !frame_is_tracked(frame1))
    {
        c_serial_print(
            "[InitraOS] HEAP_DYNAMIC_FAIL\n");
        return;
    }

    unsigned int address2 =
        (unsigned int)heap_realloc(
            (void *)address1,
            128);

    if (address2 == 0 ||
        address2 == address1)
    {
        c_serial_print(
            "[InitraOS] HEAP_DYNAMIC_FAIL\n");
        return;
    }

    volatile unsigned int *value2 =
        (volatile unsigned int *)address2;

    if (*value2 != magic1)
    {
        c_serial_print(
            "[InitraOS] HEAP_DYNAMIC_FAIL\n");
        return;
    }

    unsigned int virtual_page2 =
        address2 & ~(HEAP_PAGE_SIZE - 1);

    unsigned int frame2 =
        page_get_physical(virtual_page2);

    if (frame2 == 0 ||
        !frame_is_tracked(frame2))
    {
        c_serial_print(
            "[InitraOS] HEAP_DYNAMIC_FAIL\n");
        return;
    }

    /*
     * The old allocation must have been released.
     */
    if (page_get_physical(virtual_page1) != 0 ||
        frame_is_tracked(frame1))
    {
        c_serial_print(
            "[InitraOS] HEAP_DYNAMIC_FAIL\n");
        return;
    }

    *value2 = magic2;

    if (*value2 != magic2)
    {
        c_serial_print(
            "[InitraOS] HEAP_DYNAMIC_FAIL\n");
        return;
    }

    heap_free((void *)address2);

    if (page_get_physical(virtual_page2) != 0 ||
        frame_is_tracked(frame2))
    {
        c_serial_print(
            "[InitraOS] HEAP_DYNAMIC_FAIL\n");
        return;
    }

    c_serial_print(
        "[InitraOS] HEAP_DYNAMIC_OK\n");
}

static int heap_blocks_adjacent(
    heap_block_t *first,
    heap_block_t *second
)
{
    unsigned int first_end =
        (unsigned int)(first + 1) +
        first->size;

    return first_end ==
           (unsigned int)second;
}

static void heap_free(void *address)
{
    if (address == 0)
    {
        return;
    }

    /*
     * Check paged heap metadata first.
     */
    paged_heap_block_t *paged =
        paged_heap_find((unsigned int)address);

    if (paged != 0)
    {
        if (paged->free == 1)
        {
            return;
        }

        unsigned int paged_block_start =
            (unsigned int)address -
            sizeof(heap_block_t);

        unsigned int paged_block_end =
            paged_block_start +
            sizeof(heap_block_t) +
            paged->size;

        paged->free = 1;

        for (unsigned int page =
                 paged_block_start &
                 ~(HEAP_PAGE_SIZE - 1);
             page < paged_block_end;
             page += HEAP_PAGE_SIZE)
        {
            heap_unmap_page(page);
        }

        return;
    }

    /*
     * Legacy non-paged heap.
     */
    heap_block_t *current =
        heap_first_block;

    while (current != 0)
    {
        if ((void *)(current + 1) == address)
        {
            /* Prevent double free */
            if (current->free == 1)
            {
                return;
            }

            current->free = 1;

            /* Merge with the next block if it is also free */
            if (current->next != 0 &&
                current->next->free == 1 &&
                heap_blocks_adjacent(
                    current,
                    current->next))
            {
                current->size +=
                    sizeof(heap_block_t) +
                    current->next->size;

                current->next =
                    current->next->next;
            }

            /* Find the previous block */
            heap_block_t *previous =
                heap_first_block;

            while (previous != 0 &&
                   previous->next != current)
            {
                previous = previous->next;
            }

            /* Merge with the previous block if it is also free */
            if (previous != 0 &&
                previous->free == 1 &&
                heap_blocks_adjacent(
                    previous,
                    current))
            {
                previous->size +=
                    sizeof(heap_block_t) +
                    current->size;

                previous->next =
                    current->next;
            }

            return;
        }

        current = current->next;
    }
}

static void *heap_realloc(
    void *address,
    unsigned int size
)
{
    if (address == 0)
    {
        return heap_alloc(size);
    }

    if (size == 0)
    {
        heap_free(address);
        return 0;
    }

    /* Keep allocation alignment consistent with heap_alloc */
    size = (size + 3) & ~3;

    /*
     * Check paged heap metadata first.
     */
    paged_heap_block_t *paged =
        paged_heap_find((unsigned int)address);

    if (paged != 0)
    {
        if (paged->free == 1)
        {
            return 0;
        }

        /*
         * Existing paged block is already large enough.
         */
        if (paged->size >= size)
        {
            return address;
        }

        /*
         * Allocate a new larger paged block.
         */
        void *new_address =
            heap_alloc(size);

        if (new_address == 0)
        {
            return 0;
        }

        /*
         * Copy the old contents.
         */
        unsigned char *source =
            (unsigned char *)address;

        unsigned char *destination =
            (unsigned char *)new_address;

        for (unsigned int i = 0;
             i < paged->size;
             i++)
        {
            destination[i] =
                source[i];
        }

        heap_free(address);

        return new_address;
    }

    /*
     * Legacy non-paged heap.
     */
    heap_block_t *current =
        heap_first_block;

    while (current != 0)
    {
        if ((void *)(current + 1) == address)
        {
            /* Existing block is already large enough */
            if (current->size >= size)
            {
                return address;
            }

            /* Allocate a new larger block */
            void *new_address =
                heap_alloc(size);

            if (new_address == 0)
            {
                return 0;
            }

            /* Copy the old contents */
            unsigned char *source =
                (unsigned char *)address;

            unsigned char *destination =
                (unsigned char *)new_address;

            for (unsigned int i = 0;
                 i < current->size;
                 i++)
            {
                destination[i] =
                    source[i];
            }

            heap_free(address);

            return new_address;
        }

        current = current->next;
    }

    return 0;
}

/* ---------- Process Address Space ---------- */

#define PROCESS_SPACE_START 0x00100000
#define PROCESS_SPACE_END   0x00800000

#define USER_STACK_TOP      0x00800000
#define VGA_MEMORY_PAGE     0x000B8000
#define KERNEL_TEST_ADDRESS 0x00008800

#define PAGE_PRESENT        0x001
#define PAGE_WRITABLE       0x002
#define PAGE_USER           0x004

#define PAGE_TABLE_COUNT    4

#define E820_COUNT_ADDRESS  0x00005000

static unsigned int page_directory[1024]
    __attribute__((aligned(4096)));

static unsigned int page_tables[PAGE_TABLE_COUNT][1024]
    __attribute__((aligned(4096)));

/* ---------- Process Architecture ---------- */

typedef struct process_address_space
{
    unsigned int *page_directory;
    unsigned int (*page_tables)[1024];
    unsigned int page_table_count;
} process_address_space_t;

static process_address_space_t *address_space_create(void);

struct task;

typedef struct process process_t;

#define INITRAOS_ROOT_UID    0U
#define INITRAOS_ROOT_GID    0U

#define INITRAOS_DEFAULT_UID 1000U
#define INITRAOS_DEFAULT_GID 1000U

struct process
{
    unsigned int pid;
    unsigned int state;
    unsigned int privilege;

    unsigned int uid;
    unsigned int gid;

    process_address_space_t *address_space;

    struct task *task;
};

static process_t *process_create(void);
static struct task *task_create_user(
    process_t *process,
    unsigned int entry_point
);

static unsigned int next_process_id = 1;

static process_t *process_create(void)
{
    process_t *process =
        (process_t *)heap_alloc(
            sizeof(process_t)
        );

    if (process == 0)
    {
        return 0;
    }

    process->pid =
        next_process_id++;

    process->state =
        TASK_READY;

    process->privilege =
        TASK_USER;

    process->uid =
        INITRAOS_DEFAULT_UID;

    process->gid =
        INITRAOS_DEFAULT_GID;

    process->address_space =
        address_space_create();

    if (process->address_space == 0)
    {
        heap_free(process);
        return 0;
    }

    process->task =
        task_create_user(
            process,
            USER_CODE_BASE
        );

    if (process->task == 0)
    {
        heap_free(process->address_space);
        heap_free(process);
        return 0;
    }

    return process;
}

/* ---------- Task Architecture ---------- */

typedef struct task_context task_context_t;

typedef struct task
{
    unsigned int id;
    unsigned int state;
    unsigned int privilege;

    process_t *process;

    unsigned int esp;
    unsigned int ebp;

    unsigned int stack_base;

    task_context_t *context;

    struct task *next;
} task_t;

static void task_destroy(task_t *task);
static void process_destroy(process_t *process);

static void process_attach_task(
    process_t *process,
    struct task *task
)
{
    if (process == 0 ||
        task == 0)
    {
        return;
    }

    task->process =
        process;
}

struct task_context
{
    unsigned int eax;
    unsigned int ebx;
    unsigned int ecx;
    unsigned int edx;
    unsigned int esi;
    unsigned int edi;
    unsigned int ebp;
    unsigned int esp;
    unsigned int eip;
    unsigned int eflags;
    unsigned int privilege;
};


extern void task_switch(
    task_context_t *old_context,
    task_context_t *new_context
);

extern void scheduler_tick(void);
extern unsigned char kernel_autoboot_mode;
static unsigned int next_task_id = 1;

static task_t *current_task = 0;
static task_t *task_list = 0;

static task_context_t kernel_context;
static task_context_t test_context;


/*
 * Forward declaration.
 *
 * task_create_user() uses this function before
 * its full definition below.
 */
static void task_set_state(
    task_t *task,
    unsigned int state
);


/* ---------- Paging ---------- */

static process_address_space_t kernel_address_space;

static void page_directory_init(void)
{
    for (unsigned int entry = 0; entry < 1024; entry++)
        page_directory[entry] = 0;
}

static void page_tables_init(void)
{
    for (unsigned int table = 0;
         table < PAGE_TABLE_COUNT;
         table++)
    {
        for (unsigned int entry = 0;
             entry < 1024;
             entry++)
        {
            unsigned int address =
                ((table * 1024) + entry) * 0x1000;

            page_tables[table][entry] =
                address | PAGE_PRESENT | PAGE_WRITABLE;
        }
    }
}

static void address_space_init(
    process_address_space_t *address_space,
    unsigned int *directory,
    unsigned int (*tables)[1024],
    unsigned int table_count
);

static void paging_init(void)
{
    page_directory_init();
    page_tables_init();

    for (unsigned int table = 0;
         table < PAGE_TABLE_COUNT;
         table++)
    {
        page_directory[table] =
            (unsigned int)page_tables[table] |
            PAGE_PRESENT | PAGE_WRITABLE;
    }

    page_directory[0] |= PAGE_USER;
    page_directory[1] |= PAGE_USER;

    page_tables[0][VGA_MEMORY_PAGE >> 12] |= PAGE_USER;
    page_tables[0][(USER_CODE_BASE >> 12) & 0x3FF] |= PAGE_USER;
    page_tables[1][(USER_STACK_BASE >> 12) & 0x3FF] |= PAGE_USER;

    page_tables[0][KERNEL_TEST_ADDRESS >> 12] =
        KERNEL_TEST_ADDRESS | PAGE_PRESENT | PAGE_WRITABLE;

    address_space_init(
    &kernel_address_space,
    page_directory,
    page_tables,
    PAGE_TABLE_COUNT
);
}

static void address_space_init(
    process_address_space_t *address_space,
    unsigned int *directory,
    unsigned int (*tables)[1024],
    unsigned int table_count
)
{
    if (address_space == 0)
    {
        return;
    }

    address_space->page_directory = directory;
    address_space->page_tables = tables;
    address_space->page_table_count = table_count;
}

static process_address_space_t *address_space_create(void)
{
    process_address_space_t *address_space =
        (process_address_space_t *)heap_alloc(
            sizeof(process_address_space_t)
        );

    if (address_space == 0)
    {
        return 0;
    }

    /*
     * Page directories and page tables must themselves
     * be 4 KiB aligned because they will eventually be
     * loaded into CR3.
     */
    address_space->page_directory =
        (unsigned int *)heap_alloc_paged_aligned(
            1024 * sizeof(unsigned int)
        );

    if (address_space->page_directory == 0)
    {
        heap_free(address_space);
        return 0;
    }

    for (unsigned int entry = 0;
         entry < 1024;
         entry++)
    {
        address_space->page_directory[entry] = 0;
    }

    address_space->page_tables =
        (unsigned int (*)[1024])
        heap_alloc_paged_aligned(
            PAGE_TABLE_COUNT *
            1024 *
            sizeof(unsigned int)
        );

    if (address_space->page_tables == 0)
    {
        heap_free(address_space->page_directory);
        heap_free(address_space);
        return 0;
    }

    for (unsigned int table = 0;
         table < PAGE_TABLE_COUNT;
         table++)
    {
        for (unsigned int entry = 0;
             entry < 1024;
             entry++)
        {
            address_space->page_tables[table][entry] = 0;
        }

        address_space->page_directory[table] =
            (unsigned int)address_space->page_tables[table] |
            PAGE_PRESENT |
            PAGE_WRITABLE;
    }

    address_space->page_table_count =
        PAGE_TABLE_COUNT;

    return address_space;
}

static void paging_enable(void)
{
    unsigned int directory =
        (unsigned int)kernel_address_space.page_directory;

    __asm__ volatile (
        "mov %0, %%cr3\n"
        "mov %%cr0, %%eax\n"
        "or $0x80000000, %%eax\n"
        "mov %%eax, %%cr0\n"
        :
        : "r"(directory)
        : "eax", "memory"
    );
}

static void address_space_create_test(void)
{
    c_serial_print(
        "[InitraOS] ADDRESS_SPACE_CREATE_START\n");

    process_address_space_t *address_space =
        address_space_create();

    if (address_space == 0)
    {
        c_serial_print(
            "[InitraOS] ADDRESS_SPACE_CREATE_FAIL_ALLOC\n");
        return;
    }

    c_serial_print(
        "[InitraOS] ADDRESS_SPACE_CREATE_ALLOC_OK\n");

    if (address_space->page_directory == 0 ||
        address_space->page_tables == 0 ||
        address_space->page_table_count != PAGE_TABLE_COUNT)
    {
        c_serial_print(
            "[InitraOS] ADDRESS_SPACE_CREATE_FAIL_META\n");
        return;
    }

    c_serial_print(
        "[InitraOS] ADDRESS_SPACE_CREATE_META_OK\n");

    for (unsigned int table = 0;
         table < PAGE_TABLE_COUNT;
         table++)
    {
        unsigned int expected =
            ((unsigned int)address_space->page_tables[table] &
             0xFFFFF000) |
            PAGE_PRESENT |
            PAGE_WRITABLE;

        if (address_space->page_directory[table] != expected)
        {
            c_serial_print(
                "[InitraOS] ADDRESS_SPACE_CREATE_FAIL_DIRECTORY\n");

            c_serial_print_hex(
                (unsigned int)address_space->page_tables[table]);

            c_serial_print_hex(
                address_space->page_directory[table]);

            c_serial_print_hex(
                expected);

            return;
        }
    }

    c_serial_print(
        "[InitraOS] ADDRESS_SPACE_CREATE_DIRECTORY_OK\n");

    for (unsigned int table = 0;
         table < PAGE_TABLE_COUNT;
         table++)
    {
        if (address_space->page_tables[table][0] != 0)
        {
            c_serial_print(
                "[InitraOS] ADDRESS_SPACE_CREATE_FAIL_TABLES\n");
            return;
        }
    }

    c_serial_print(
        "[InitraOS] ADDRESS_SPACE_CREATE_TABLES_OK\n");

    heap_free(address_space->page_tables);
    heap_free(address_space->page_directory);
    heap_free(address_space);

    c_serial_print(
        "[InitraOS] ADDRESS_SPACE_CREATE_OK\n");
}

static int page_map_in_address_space(
    process_address_space_t *address_space,
    unsigned int virtual_address,
    unsigned int physical_address,
    unsigned int flags);

static int page_unmap_in_address_space(
    process_address_space_t *address_space,
    unsigned int virtual_address);

static unsigned int page_get_physical_in_address_space(
    process_address_space_t *address_space,
    unsigned int virtual_address);

static void process_isolation_test(void)
{
    const unsigned int test_virtual =
        0x00400000;

    const unsigned int frame_a =
        0x00200000;

    const unsigned int frame_b =
        0x00201000;

    process_address_space_t *space_a =
        address_space_create();

    process_address_space_t *space_b =
        address_space_create();

    if (space_a == 0 ||
        space_b == 0)
    {
        c_serial_print(
            "[InitraOS] PROCESS_ISOLATION_FAIL\n");
        return;
    }

    if (space_a->page_directory ==
            space_b->page_directory ||
        space_a->page_tables ==
            space_b->page_tables)
    {
        c_serial_print(
            "[InitraOS] PROCESS_ISOLATION_FAIL\n");
        return;
    }

    c_serial_print(
        "[InitraOS] PROCESS_ISOLATION_TABLES_OK\n");

    if (!page_map_in_address_space(
            space_a,
            test_virtual,
            frame_a,
            PAGE_PRESENT |
            PAGE_WRITABLE |
            PAGE_USER))
    {
        c_serial_print(
            "[InitraOS] PROCESS_ISOLATION_FAIL\n");
        return;
    }

    if (!page_map_in_address_space(
            space_b,
            test_virtual,
            frame_b,
            PAGE_PRESENT |
            PAGE_WRITABLE |
            PAGE_USER))
    {
        c_serial_print(
            "[InitraOS] PROCESS_ISOLATION_FAIL\n");
        return;
    }

    if (page_get_physical_in_address_space(
            space_a,
            test_virtual) != frame_a)
    {
        c_serial_print(
            "[InitraOS] PROCESS_ISOLATION_FAIL\n");
        return;
    }

    if (page_get_physical_in_address_space(
            space_b,
            test_virtual) != frame_b)
    {
        c_serial_print(
            "[InitraOS] PROCESS_ISOLATION_FAIL\n");
        return;
    }

    if (frame_a == frame_b)
    {
        c_serial_print(
            "[InitraOS] PROCESS_ISOLATION_FAIL\n");
        return;
    }

    c_serial_print(
        "[InitraOS] PROCESS_ISOLATION_MAPPING_OK\n");

    if (!page_unmap_in_address_space(
            space_a,
            test_virtual))
    {
        c_serial_print(
            "[InitraOS] PROCESS_ISOLATION_FAIL\n");
        return;
    }

    if (page_get_physical_in_address_space(
            space_a,
            test_virtual) != 0)
    {
        c_serial_print(
            "[InitraOS] PROCESS_ISOLATION_FAIL\n");
        return;
    }

    if (page_get_physical_in_address_space(
            space_b,
            test_virtual) != frame_b)
    {
        c_serial_print(
            "[InitraOS] PROCESS_ISOLATION_FAIL\n");
        return;
    }

    c_serial_print(
        "[InitraOS] PROCESS_ISOLATION_UNMAP_OK\n");

    heap_free(space_a->page_tables);
    heap_free(space_a->page_directory);
    heap_free(space_a);

    heap_free(space_b->page_tables);
    heap_free(space_b->page_directory);
    heap_free(space_b);

    c_serial_print(
        "[InitraOS] PROCESS_ISOLATION_OK\n");
}

static void process_permission_isolation_test(void)
{
    const unsigned int user_virtual =
        0x00400000;

    const unsigned int kernel_virtual =
        0x00401000;

    const unsigned int user_frame_a =
        0x00200000;

    const unsigned int user_frame_b =
        0x00201000;

    const unsigned int kernel_frame_a =
        0x00202000;

    const unsigned int kernel_frame_b =
        0x00203000;

    process_address_space_t *space_a =
        address_space_create();

    process_address_space_t *space_b =
        address_space_create();

    if (space_a == 0 ||
        space_b == 0)
    {
        c_serial_print(
            "[InitraOS] PROCESS_PERMISSION_ISOLATION_FAIL\n");
        return;
    }

    if (!page_map_in_address_space(
            space_a,
            user_virtual,
            user_frame_a,
            PAGE_PRESENT |
            PAGE_WRITABLE |
            PAGE_USER))
    {
        c_serial_print(
            "[InitraOS] PROCESS_PERMISSION_ISOLATION_FAIL\n");
        return;
    }

    if (!page_map_in_address_space(
            space_b,
            user_virtual,
            user_frame_b,
            PAGE_PRESENT |
            PAGE_WRITABLE |
            PAGE_USER))
    {
        c_serial_print(
            "[InitraOS] PROCESS_PERMISSION_ISOLATION_FAIL\n");
        return;
    }

    if (!page_map_in_address_space(
            space_a,
            kernel_virtual,
            kernel_frame_a,
            PAGE_PRESENT |
            PAGE_WRITABLE))
    {
        c_serial_print(
            "[InitraOS] PROCESS_PERMISSION_ISOLATION_FAIL\n");
        return;
    }

    if (!page_map_in_address_space(
            space_b,
            kernel_virtual,
            kernel_frame_b,
            PAGE_PRESENT |
            PAGE_WRITABLE))
    {
        c_serial_print(
            "[InitraOS] PROCESS_PERMISSION_ISOLATION_FAIL\n");
        return;
    }

    unsigned int user_index =
        (user_virtual >> 12) & 0x3FF;

    unsigned int kernel_index =
        (kernel_virtual >> 12) & 0x3FF;

    unsigned int user_entry_a =
    space_a->page_tables[1][user_index];

    unsigned int user_entry_b =
    space_b->page_tables[1][user_index];

    unsigned int kernel_entry_a =
    space_a->page_tables[1][kernel_index];

    unsigned int kernel_entry_b =
    space_b->page_tables[1][kernel_index];

    if ((user_entry_a & PAGE_USER) == 0 ||
        (user_entry_b & PAGE_USER) == 0)
    {
        c_serial_print(
            "[InitraOS] PROCESS_PERMISSION_ISOLATION_FAIL\n");
        return;
    }

    if ((kernel_entry_a & PAGE_USER) != 0 ||
        (kernel_entry_b & PAGE_USER) != 0)
    {
        c_serial_print(
            "[InitraOS] PROCESS_PERMISSION_ISOLATION_FAIL\n");
        return;
    }

    if ((user_entry_a & 0xFFFFF000) != user_frame_a ||
        (user_entry_b & 0xFFFFF000) != user_frame_b)
    {
        c_serial_print(
            "[InitraOS] PROCESS_PERMISSION_ISOLATION_FAIL\n");
        return;
    }

    if ((kernel_entry_a & 0xFFFFF000) != kernel_frame_a ||
        (kernel_entry_b & 0xFFFFF000) != kernel_frame_b)
    {
        c_serial_print(
            "[InitraOS] PROCESS_PERMISSION_ISOLATION_FAIL\n");
        return;
    }

    c_serial_print(
        "[InitraOS] PROCESS_PERMISSION_ISOLATION_OK\n");

    heap_free(space_a->page_tables);
    heap_free(space_a->page_directory);
    heap_free(space_a);

    heap_free(space_b->page_tables);
    heap_free(space_b->page_directory);
    heap_free(space_b);
}

static void process_create_test(void)
{
    c_serial_print(
        "[InitraOS] PROCESS_CREATE_START\n");

    process_t *process =
        process_create();

    if (process == 0)
    {
        c_serial_print(
            "[InitraOS] PROCESS_CREATE_FAIL_ALLOC\n");
        return;
    }

    if (process->pid == 0)
    {
        c_serial_print(
            "[InitraOS] PROCESS_CREATE_FAIL_PID\n");

        process_destroy(process);
        return;
    }

    c_serial_print(
        "[InitraOS] PROCESS_CREATE_PID_OK\n");

    if (process->state != TASK_READY ||
        process->privilege != TASK_USER)
    {
        c_serial_print(
            "[InitraOS] PROCESS_CREATE_FAIL_STATE\n");

        process_destroy(process);
        return;
    }

    c_serial_print(
        "[InitraOS] PROCESS_CREATE_STATE_OK\n");

    if (process->uid != INITRAOS_DEFAULT_UID ||
        process->gid != INITRAOS_DEFAULT_GID)
    {
        c_serial_print(
            "[InitraOS] PROCESS_CREATE_FAIL_IDENTITY\n");

        process_destroy(process);
        return;
    }

    c_serial_print(
        "[InitraOS] PROCESS_CREATE_IDENTITY_OK\n");

    if (process->address_space == 0)
    {
        c_serial_print(
            "[InitraOS] PROCESS_CREATE_FAIL_ADDRESS_SPACE\n");

        process_destroy(process);
        return;
    }

    if (process->address_space->page_directory == 0 ||
        process->address_space->page_tables == 0 ||
        process->address_space->page_table_count !=
            PAGE_TABLE_COUNT)
    {
        c_serial_print(
            "[InitraOS] PROCESS_CREATE_FAIL_ADDRESS_SPACE_META\n");

        process_destroy(process);
        return;
    }

    c_serial_print(
        "[InitraOS] PROCESS_CREATE_ADDRESS_SPACE_OK\n");

    if (process->task == 0)
    {
        c_serial_print(
            "[InitraOS] PROCESS_CREATE_FAIL_TASK\n");

        process_destroy(process);
        return;
    }

    if (process->task->process != process)
    {
        c_serial_print(
            "[InitraOS] PROCESS_CREATE_FAIL_TASK_LINK\n");

        process_destroy(process);
        return;
    }

    if (process->task->privilege != TASK_USER ||
        process->task->state != TASK_READY)
    {
        c_serial_print(
            "[InitraOS] PROCESS_CREATE_FAIL_TASK_STATE\n");

        process_destroy(process);
        return;
    }

    c_serial_print(
        "[InitraOS] PROCESS_CREATE_TASK_OK\n");

    c_serial_print(
        "[InitraOS] PROCESS_CREATE_OK\n");

    process_destroy(process);
}

static void process_instance_isolation_test(void)
{
    c_serial_print(
        "[InitraOS] PROCESS_INSTANCE_ISOLATION_START\n");

    process_t *process_a =
        process_create();

    process_t *process_b =
        process_create();

    if (process_a == 0 ||
        process_b == 0)
    {
        c_serial_print(
            "[InitraOS] PROCESS_INSTANCE_ISOLATION_FAIL\n");
        return;
    }

    if (process_a->address_space == 0 ||
        process_b->address_space == 0)
    {
        c_serial_print(
            "[InitraOS] PROCESS_INSTANCE_ISOLATION_FAIL\n");
        return;
    }

    if (process_a->address_space ==
        process_b->address_space)
    {
        c_serial_print(
            "[InitraOS] PROCESS_INSTANCE_ISOLATION_FAIL\n");
        return;
    }

    if (process_a->address_space->page_directory ==
        process_b->address_space->page_directory)
    {
        c_serial_print(
            "[InitraOS] PROCESS_INSTANCE_ISOLATION_FAIL\n");
        return;
    }

    if (process_a->address_space->page_tables ==
        process_b->address_space->page_tables)
    {
        c_serial_print(
            "[InitraOS] PROCESS_INSTANCE_ISOLATION_FAIL\n");
        return;
    }

    c_serial_print(
        "[InitraOS] PROCESS_INSTANCE_ISOLATION_PROCESS_OK\n");

    if (process_a->task == 0 ||
        process_b->task == 0)
    {
        c_serial_print(
            "[InitraOS] PROCESS_INSTANCE_ISOLATION_FAIL\n");
        return;
    }

    if (process_a->task ==
        process_b->task)
    {
        c_serial_print(
            "[InitraOS] PROCESS_INSTANCE_ISOLATION_FAIL\n");
        return;
    }

    c_serial_print(
        "[InitraOS] PROCESS_INSTANCE_ISOLATION_TASK_OK\n");

    heap_free(
        process_a->address_space->page_tables
    );

    heap_free(
        process_a->address_space->page_directory
    );

    heap_free(
        process_a->address_space
    );

    heap_free(
        process_a
    );

    heap_free(
        process_b->address_space->page_tables
    );

    heap_free(
        process_b->address_space->page_directory
    );

    heap_free(
        process_b->address_space
    );

    heap_free(
        process_b
    );

    c_serial_print(
        "[InitraOS] PROCESS_INSTANCE_ISOLATION_OK\n");
}

static void process_task_link_test(void)
{
    c_serial_print(
        "[InitraOS] PROCESS_TASK_LINK_START\n");

    process_t *process =
        process_create();

    if (process == 0)
    {
        c_serial_print(
            "[InitraOS] PROCESS_TASK_LINK_FAIL_PROCESS\n");
        return;
    }

    if (process->task == 0)
    {
        c_serial_print(
            "[InitraOS] PROCESS_TASK_LINK_FAIL_TASK\n");

        process_destroy(process);
        return;
    }

    if (process->task->process != process)
    {
        c_serial_print(
            "[InitraOS] PROCESS_TASK_LINK_FAIL_LINK\n");

        process_destroy(process);
        return;
    }

    if (process->pid == 0)
    {
        c_serial_print(
            "[InitraOS] PROCESS_TASK_LINK_FAIL_PID\n");

        process_destroy(process);
        return;
    }

    c_serial_print(
        "[InitraOS] PROCESS_TASK_LINK_OK\n");

    process_destroy(process);
}

static void page_invalidate(unsigned int virtual_address)
{
    __asm__ volatile (
        "invlpg (%0)"
        :
        : "r"(virtual_address)
        : "memory"
    );
}

static int page_map_in_address_space(
    process_address_space_t *address_space,
    unsigned int virtual_address,
    unsigned int physical_address,
    unsigned int flags
)
{
    unsigned int directory_index =
        (virtual_address >> 22) & 0x3FF;

    unsigned int table_index =
        (virtual_address >> 12) & 0x3FF;

    if (address_space == 0 ||
        address_space->page_tables == 0)
    {
        return 0;
    }

    if (directory_index >= address_space->page_table_count)
    {
        return 0;
    }

    address_space->page_tables[directory_index][table_index] =
        (physical_address & 0xFFFFF000) |
        (flags & 0xFFF);

    page_invalidate(virtual_address);

    return 1;
}

static int page_map(
    unsigned int virtual_address,
    unsigned int physical_address,
    unsigned int flags
)
{
    return page_map_in_address_space(
        &kernel_address_space,
        virtual_address,
        physical_address,
        flags
    );
}

static int page_unmap_in_address_space(
    process_address_space_t *address_space,
    unsigned int virtual_address
)
{
    unsigned int directory_index =
        (virtual_address >> 22) & 0x3FF;

    unsigned int table_index =
        (virtual_address >> 12) & 0x3FF;

    if (address_space == 0 ||
        address_space->page_tables == 0)
    {
        return 0;
    }

    if (directory_index >= address_space->page_table_count)
    {
        return 0;
    }

    address_space->page_tables[directory_index][table_index] = 0;

    page_invalidate(virtual_address);

    return 1;
}

static int page_unmap(
    unsigned int virtual_address
)
{
    return page_unmap_in_address_space(
        &kernel_address_space,
        virtual_address
    );
}

static int page_map_new_frame(unsigned int virtual_address,
                              unsigned int flags,
                              unsigned int *physical_address)
{
    unsigned int frame = frame_alloc();

    if (frame == 0)
        return 0;

    if (!page_map(virtual_address, frame, flags))
    {
        frame_free(frame);
        return 0;
    }

    if (physical_address != 0)
        *physical_address = frame;

    return 1;
}

static void frame_paging_test(void)
{
    const unsigned int test_virtual = 0x00400000;
    const unsigned int magic = 0x1A17A05;
    unsigned int frame = 0;
    volatile unsigned int *test_page =
        (volatile unsigned int *)test_virtual;

    if (!page_map_new_frame(
            test_virtual,
            PAGE_PRESENT | PAGE_WRITABLE,
            &frame))
    {
        c_serial_print("[InitraOS] FRAME_PAGING_FAIL\n");
        return;
    }

    if (!frame_is_tracked(frame))
    {
        page_unmap(test_virtual);
        frame_free(frame);
        c_serial_print("[InitraOS] FRAME_PAGING_FAIL\n");
        return;
    }

    *test_page = magic;

    if (*test_page != magic)
    {
        page_unmap(test_virtual);
        frame_free(frame);
        c_serial_print("[InitraOS] FRAME_PAGING_FAIL\n");
        return;
    }

    if (!page_unmap(test_virtual) ||
        !frame_free(frame) ||
        frame_is_tracked(frame))
    {
        c_serial_print("[InitraOS] FRAME_PAGING_FAIL\n");
        return;
    }

    c_serial_print("[InitraOS] FRAME_PAGING_OK\n");
}

static void dynamic_page_test(void)
{
    const unsigned int page1_virtual = 0x00500000;
    const unsigned int page2_virtual = 0x00501000;

    const unsigned int magic1 = 0xD94A0001;
    const unsigned int magic2 = 0xD94A0002;

    unsigned int frame1 = 0;
    unsigned int frame2 = 0;

    volatile unsigned int *page1 =
        (volatile unsigned int *)page1_virtual;

    volatile unsigned int *page2 =
        (volatile unsigned int *)page2_virtual;

    if (!page_map_new_frame(
            page1_virtual,
            PAGE_PRESENT | PAGE_WRITABLE,
            &frame1) ||
        !page_map_new_frame(
            page2_virtual,
            PAGE_PRESENT | PAGE_WRITABLE,
            &frame2))
    {
        if (frame1 != 0)
        {
            page_unmap(page1_virtual);
            frame_free(frame1);
        }

        if (frame2 != 0)
        {
            page_unmap(page2_virtual);
            frame_free(frame2);
        }

        c_serial_print("[InitraOS] DYNAMIC_PAGE_FAIL\n");
        return;
    }

    if (frame1 == frame2 ||
        !frame_is_tracked(frame1) ||
        !frame_is_tracked(frame2))
    {
        page_unmap(page1_virtual);
        page_unmap(page2_virtual);
        frame_free(frame1);
        frame_free(frame2);

        c_serial_print("[InitraOS] DYNAMIC_PAGE_FAIL\n");
        return;
    }

    *page1 = magic1;
    *page2 = magic2;

    if (*page1 != magic1 ||
        *page2 != magic2)
    {
        page_unmap(page1_virtual);
        page_unmap(page2_virtual);
        frame_free(frame1);
        frame_free(frame2);

        c_serial_print("[InitraOS] DYNAMIC_PAGE_FAIL\n");
        return;
    }

    if (!page_unmap(page1_virtual) ||
        !page_unmap(page2_virtual) ||
        !frame_free(frame1) ||
        !frame_free(frame2) ||
        frame_is_tracked(frame1) ||
        frame_is_tracked(frame2))
    {
        c_serial_print("[InitraOS] DYNAMIC_PAGE_FAIL\n");
        return;
    }

    c_serial_print("[InitraOS] DYNAMIC_PAGE_OK\n");
}

static void page_protection_test(void)
{
    const unsigned int test_virtual = 0x00502000;

    unsigned int frame = 0;

    if (!page_map_new_frame(
            test_virtual,
            PAGE_PRESENT,
            &frame))
    {
        c_serial_print(
            "[InitraOS] PAGE_PROTECTION_FAIL\n");
        return;
    }

    unsigned int directory_index =
        (test_virtual >> 22) & 0x3FF;

    unsigned int table_index =
        (test_virtual >> 12) & 0x3FF;

    unsigned int entry =
    kernel_address_space.page_tables[directory_index][table_index];

    if (frame == 0 ||
        !frame_is_tracked(frame) ||
        (entry & PAGE_PRESENT) == 0 ||
        (entry & PAGE_WRITABLE) != 0)
    {
        page_unmap(test_virtual);

        if (frame != 0)
        {
            frame_free(frame);
        }

        c_serial_print(
            "[InitraOS] PAGE_PROTECTION_FAIL\n");
        return;
    }

    if (!page_unmap(test_virtual) ||
        !frame_free(frame) ||
        frame_is_tracked(frame))
    {
        c_serial_print(
            "[InitraOS] PAGE_PROTECTION_FAIL\n");
        return;
    }

    c_serial_print(
        "[InitraOS] PAGE_PROTECTION_OK\n");
}


static void page_user_protection_test(void)
{
    unsigned int user_code_entry =
    kernel_address_space.page_tables[0][(USER_CODE_BASE >> 12) & 0x3FF];

    unsigned int user_stack_entry =
    kernel_address_space.page_tables[1][(USER_STACK_BASE >> 12) & 0x3FF];

    unsigned int kernel_entry =
    kernel_address_space.page_tables[0][KERNEL_TEST_ADDRESS >> 12];

    if ((user_code_entry &
         (PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER)) !=
        (PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER))
    {
        c_serial_print(
            "[InitraOS] PAGE_USER_PROTECTION_FAIL\n");
        return;
    }

    if ((user_stack_entry &
         (PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER)) !=
        (PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER))
    {
        c_serial_print(
            "[InitraOS] PAGE_USER_PROTECTION_FAIL\n");
        return;
    }

    if ((kernel_entry &
         (PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER)) !=
        (PAGE_PRESENT | PAGE_WRITABLE))
    {
        c_serial_print(
            "[InitraOS] PAGE_USER_PROTECTION_FAIL\n");
        return;
    }

    c_serial_print(
        "[InitraOS] PAGE_USER_PROTECTION_OK\n");
}

static void heap_paging_test(void)
{
    const unsigned int heap_page_virtual = 0x00600000;
    const unsigned int magic = 0x95A11E01;

    unsigned int frame = 0;

    volatile unsigned int *heap_page =
        (volatile unsigned int *)heap_page_virtual;

    if (!page_map_new_frame(
            heap_page_virtual,
            PAGE_PRESENT | PAGE_WRITABLE,
            &frame))
    {
        c_serial_print("[InitraOS] HEAP_PAGING_FAIL\n");
        return;
    }

    if (frame == 0 ||
        !frame_is_tracked(frame))
    {
        page_unmap(heap_page_virtual);

        if (frame != 0)
        {
            frame_free(frame);
        }

        c_serial_print("[InitraOS] HEAP_PAGING_FAIL\n");
        return;
    }

    *heap_page = magic;

    if (*heap_page != magic)
    {
        page_unmap(heap_page_virtual);
        frame_free(frame);

        c_serial_print("[InitraOS] HEAP_PAGING_FAIL\n");
        return;
    }

    if (!page_unmap(heap_page_virtual) ||
        !frame_free(frame) ||
        frame_is_tracked(frame))
    {
        c_serial_print("[InitraOS] HEAP_PAGING_FAIL\n");
        return;
    }

    c_serial_print("[InitraOS] HEAP_PAGING_OK\n");
}

static int heap_map_page(unsigned int virtual_address)
{
    unsigned int frame = 0;

    if ((virtual_address & (HEAP_PAGE_SIZE - 1)) != 0)
    {
        return 0;
    }

    if (virtual_address < HEAP_PAGED_BASE ||
        virtual_address >= HEAP_PAGED_LIMIT)
    {
        return 0;
    }

    return page_map_new_frame(
        virtual_address,
        PAGE_PRESENT | PAGE_WRITABLE,
        &frame
    );
}

static int heap_unmap_page(unsigned int virtual_address)
{
    unsigned int directory_index =
        (virtual_address >> 22) & 0x3FF;

    unsigned int table_index =
        (virtual_address >> 12) & 0x3FF;

    if ((virtual_address & (HEAP_PAGE_SIZE - 1)) != 0)
    {
        return 0;
    }

    if (virtual_address < HEAP_PAGED_BASE ||
        virtual_address >= HEAP_PAGED_LIMIT)
    {
        return 0;
    }

    if (directory_index >= PAGE_TABLE_COUNT)
    {
        return 0;
    }

    unsigned int entry =
        kernel_address_space.page_tables[directory_index][table_index];

    if ((entry & PAGE_PRESENT) == 0)
    {
        return 0;
    }

    unsigned int frame =
        entry & 0xFFFFF000;

    if (!page_unmap(virtual_address))
    {
        return 0;
    }

    return frame_free(frame);
}

static unsigned int page_get_physical_in_address_space(
    process_address_space_t *address_space,
    unsigned int virtual_address
)
{
    unsigned int directory_index =
        (virtual_address >> 22) & 0x3FF;

    unsigned int table_index =
        (virtual_address >> 12) & 0x3FF;

    if (address_space == 0 ||
        address_space->page_tables == 0)
    {
        return 0;
    }

    if ((virtual_address & (FRAME_SIZE - 1)) != 0)
    {
        return 0;
    }

    if (directory_index >= address_space->page_table_count)
    {
        return 0;
    }

    unsigned int entry =
        address_space->page_tables[directory_index][table_index];

    if ((entry & PAGE_PRESENT) == 0)
    {
        return 0;
    }

    return entry & 0xFFFFF000;
}

static unsigned int page_get_physical(
    unsigned int virtual_address
)
{
    return page_get_physical_in_address_space(
        &kernel_address_space,
        virtual_address
    );
}

static int user_program_load(
    const unsigned char *image,
    unsigned int image_size,
    unsigned int *entry_point
)
{
    const unsigned int header_size =
        sizeof(initraos_program_header_t);

    if (image == 0 ||
        entry_point == 0 ||
        image_size < header_size)
    {
        return 0;
    }

    const initraos_program_header_t *header =
        (const initraos_program_header_t *)image;

    if (header->magic != INITRAOS_PROGRAM_MAGIC)
    {
        return 0;
    }

    if (header->version != INITRAOS_PROGRAM_VERSION)
    {
        return 0;
    }

    unsigned int payload_size =
        header->code_size +
        header->data_size;

    if (header->code_size == 0)
    {
        return 0;
    }

    if (payload_size < header->code_size)
    {
        return 0;
    }

    if (header->bss_size >
        user_code_region.size - payload_size)
    {
        return 0;
    }

    if (header_size > image_size ||
        payload_size > image_size - header_size)
    {
        return 0;
    }

    unsigned int image_memory_size =
        payload_size +
        header->bss_size;

    if (image_memory_size < payload_size ||
        image_memory_size > user_code_region.size)
    {
        return 0;
    }

    if (header->entry >= header->code_size)
    {
        return 0;
    }

    unsigned char *destination =
        (unsigned char *)user_code_region.base;

    const unsigned char *payload =
        image + header_size;

    for (unsigned int i = 0;
         i < payload_size;
         i++)
    {
        destination[i] =
            payload[i];
    }

    for (unsigned int i = payload_size;
         i < image_memory_size;
         i++)
    {
        destination[i] = 0;
    }

    *entry_point =
        user_code_region.base +
        header->entry;

    return 1;
}

static int user_space_prepare(
    unsigned int *entry_point
)
{
    unsigned int image_start =
        (unsigned int)user_mode_code_start;

    unsigned int image_end =
        (unsigned int)user_mode_code_end;

    unsigned int image_size =
        image_end - image_start;

    if (entry_point == 0 ||
        image_size == 0)
    {
        return 0;
    }

    if (!user_program_load(
            (const unsigned char *)image_start,
            image_size,
            entry_point))
    {
        return 0;
    }

    /*
     * Clear the complete user stack region.
     */
    volatile unsigned char *user_stack =
        (volatile unsigned char *)
        user_stack_region.base;

    for (unsigned int i = 0;
         i < user_stack_region.size;
         i++)
    {
        user_stack[i] = 0;
    }

    return 1;
}

static int user_secaudit_prepare(
    unsigned int *entry_point
)
{
    unsigned int image_start =
        (unsigned int)user_secaudit_code_start;

    unsigned int image_end =
        (unsigned int)user_secaudit_code_end;

    unsigned int image_size =
        image_end - image_start;

    if (entry_point == 0 ||
        image_size == 0)
    {
        return 0;
    }

    if (!user_program_load(
            (const unsigned char *)image_start,
            image_size,
            entry_point))
    {
        return 0;
    }

    /*
     * Clear the complete user stack before launching
     * the security utility.
     */
    volatile unsigned char *user_stack =
        (volatile unsigned char *)
        user_stack_region.base;

    for (unsigned int i = 0;
         i < user_stack_region.size;
         i++)
    {
        user_stack[i] = 0;
    }

    return 1;
}

/* ---------- Kernel Task Creation ---------- */

static task_t *task_create(void)
{
    task_t *task =
        (task_t *)heap_alloc(
            sizeof(task_t)
        );

    if (task == 0)
    {
        return 0;
    }

    task->id =
        next_task_id++;

    task->state =
        TASK_READY;

    task->privilege =
        TASK_KERNEL;

    task->process = 0;

    task->esp = 0;
    task->ebp = 0;

    task->stack_base = 0;
    task->context = 0;

    task->next = 0;

    /*
     * Allocate a dedicated 4096-byte kernel stack.
     */
    task->stack_base =
        (unsigned int)heap_alloc(4096);

    if (task->stack_base == 0)
    {
        heap_free(task);
        return 0;
    }

    /*
     * Add task to scheduler list.
     */
    if (task_list == 0)
    {
        task_list = task;
    }
    else
    {
        task_t *current =
            task_list;

        while (current->next != 0)
        {
            current =
                current->next;
        }

        current->next =
            task;
    }

    return task;
}

/* ---------- User Task Creation ---------- */

static task_t *task_create_user(
    process_t *process,
    unsigned int entry_point
)
{
    task_t *task =
        task_create();

    if (task == 0)
    {
        return 0;
    }

    /*
     * If this is a process-owned task, establish the
     * process/task relationship immediately.
     *
     * Independent user tasks remain process-less.
     */
    task->process =
        process;

    task->privilege =
        TASK_USER;

    /*
     * The user task does not use the kernel heap stack.
     * Release the temporary kernel stack.
     */
    heap_free(
        (void *)task->stack_base
    );

    task->stack_base =
        USER_STACK_BASE;

    task->esp =
        USER_STACK_TOP;

    task->esp &=
        ~0x0F;

    task->ebp =
        task->esp;

    task->context =
        (task_context_t *)
        heap_alloc(
            sizeof(task_context_t)
        );

    if (task->context == 0)
    {
        task_set_state(
            task,
            TASK_FINISHED
        );

        return task;
    }

    task->context->eax = 0;
    task->context->ebx = task->id;
    task->context->ecx = 0;
    task->context->edx = 0;
    task->context->esi = 0;
    task->context->edi = 0;

    task->context->ebp =
        task->ebp;

    task->context->esp =
        task->esp;

    task->context->eip =
    entry_point;

    task->context->eflags =
        0x202;

    task->context->privilege =
        TASK_USER;

    return task;
}

static void task_destroy(task_t *task)
{
    if (task == 0)
    {
        return;
    }

    /*
     * Do not destroy the currently running task.
     */
    if (task == current_task)
    {
        return;
    }

    /*
     * Remove the task from the global scheduler list.
     */
    if (task_list == task)
    {
        task_list = task->next;
    }
    else
    {
        task_t *previous =
            task_list;

        while (previous != 0 &&
               previous->next != task)
        {
            previous =
                previous->next;
        }

        if (previous == 0)
        {
            return;
        }

        previous->next =
            task->next;
    }

    /*
     * Release the task context.
     */
    if (task->context != 0)
    {
        heap_free(
            task->context
        );

        task->context = 0;
    }

    /*
     * Release the temporary kernel stack.
     *
     * USER_STACK_BASE is not heap allocated here,
     * so it must not be passed to heap_free().
     */
    if (task->stack_base != 0 &&
        task->stack_base != USER_STACK_BASE)
    {
        heap_free(
            (void *)task->stack_base
        );
    }

    task->stack_base = 0;
    task->process = 0;
    task->next = 0;

    heap_free(task);
}


static void process_destroy(process_t *process)
{
    if (process == 0)
    {
        return;
    }

    /*
     * The process owns its task.
     */
    if (process->task != 0)
    {
        task_destroy(
            process->task
        );

        process->task = 0;
    }

    /*
     * Release the process address space.
     */
    if (process->address_space != 0)
    {
        if (process->address_space->page_tables != 0)
        {
            heap_free(
                process->address_space->page_tables
            );
        }

        if (process->address_space->page_directory != 0)
        {
            heap_free(
                process->address_space->page_directory
            );
        }

        heap_free(
            process->address_space
        );

        process->address_space = 0;
    }

    heap_free(process);
}

/* ---------- Task State ---------- */

static void task_set_state(
    task_t *task,
    unsigned int state
)
{
    if (task == 0)
    {
        return;
    }

    task->state =
        state;
}


/* ---------- Scheduler ---------- */

/*
 * Select the next READY task using
 * round-robin order.
 */
static task_t *task_schedule_next(void)
{
    if (task_list == 0)
    {
        return 0;
    }

    task_t *start =
        task_list;

    if (current_task != 0 &&
        current_task->next != 0)
    {
        start =
            current_task->next;
    }

    task_t *task =
        start;

    do
    {
        if (task->state ==
            TASK_READY)
        {
            return task;
        }

        task =
            task->next;

        if (task == 0)
        {
            task =
                task_list;
        }

    } while (task != start);

    return 0;
}

void scheduler_tick(void)
{
    task_t *next_task;

    if (current_task == 0)
    {
        return;
    }

    next_task =
        task_schedule_next();

    if (next_task == 0 ||
        next_task == current_task)
    {
        return;
    }

    /*
     * A scheduling decision alone must not change
     * current_task. The CPU is still executing the
     * current task until task_switch() performs the
     * actual context switch.
     *
     * Preemptive task switching will be added separately.
     */
}


/* ---------- Task Test ---------- */

static unsigned char task_test_stack[4096];


static void task_exit(void)
{
    if (current_task != 0)
    {
        task_set_state(
            current_task,
            TASK_FINISHED
        );
    }

    task_switch(
        &test_context,
        &kernel_context
    );

    while (1)
    {
        __asm__ volatile ("hlt");
    }
}

void syscall_exit(void)
{
    if (current_task != 0)
    {
        task_set_state(
            current_task,
            TASK_FINISHED
        );

        task_switch(
            current_task->context,
            &kernel_context
        );
    }

    while (1)
    {
        __asm__ volatile ("hlt");
    }
}

static void user_stack_test(void)
{
    c_serial_print(
        "[InitraOS] USER_STACK_START\n");

    task_t *task =
        task_create_user(
            0,
            USER_CODE_BASE
        );

    if (task == 0)
    {
        c_serial_print(
            "[InitraOS] USER_STACK_FAIL_CREATE\n");
        return;
    }

    if (task->stack_base != USER_STACK_BASE)
    {
        c_serial_print(
            "[InitraOS] USER_STACK_FAIL_BASE\n");
        return;
    }

    if (task->esp != USER_STACK_TOP ||
        task->ebp != USER_STACK_TOP)
    {
        c_serial_print(
            "[InitraOS] USER_STACK_FAIL_TOP\n");
        return;
    }

    if ((task->esp & 0x0F) != 0)
    {
        c_serial_print(
            "[InitraOS] USER_STACK_FAIL_ALIGN\n");
        return;
    }

    if (task->context == 0 ||
        task->context->esp != task->esp ||
        task->context->ebp != task->ebp)
    {
        c_serial_print(
            "[InitraOS] USER_STACK_FAIL_CONTEXT\n");
        return;
    }

    unsigned int stack_page =
        task->stack_base &
        ~(HEAP_PAGE_SIZE - 1);

    unsigned int frame =
        page_get_physical(stack_page);

    if (frame == 0)
    {
        c_serial_print(
            "[InitraOS] USER_STACK_FAIL_MAPPING\n");
        return;
    }

    if ((kernel_address_space.page_tables[1]
            [(USER_STACK_BASE >> 12) & 0x3FF]
         & PAGE_USER) == 0)
    {
        c_serial_print(
            "[InitraOS] USER_STACK_FAIL_USER\n");
        return;
    }

    task_set_state(
        task,
        TASK_FINISHED
    );

    c_serial_print(
        "[InitraOS] USER_STACK_OK\n");
}

static void user_region_test(void)
{
    c_serial_print(
        "[InitraOS] USER_REGION_START\n");

    if (user_code_region.base != USER_CODE_BASE ||
        user_code_region.size != USER_CODE_SIZE)
    {
        c_serial_print(
            "[InitraOS] USER_REGION_FAIL_CODE\n");
        return;
    }

    if ((user_code_region.flags &
         (MEMORY_REGION_USER |
          MEMORY_REGION_EXECUTABLE)) !=
        (MEMORY_REGION_USER |
         MEMORY_REGION_EXECUTABLE))
    {
        c_serial_print(
            "[InitraOS] USER_REGION_FAIL_CODE_FLAGS\n");
        return;
    }

    if (user_stack_region.base != USER_STACK_BASE ||
        user_stack_region.size != USER_STACK_SIZE)
    {
        c_serial_print(
            "[InitraOS] USER_REGION_FAIL_STACK\n");
        return;
    }

    if ((user_stack_region.flags &
         (MEMORY_REGION_USER |
          MEMORY_REGION_WRITABLE)) !=
        (MEMORY_REGION_USER |
         MEMORY_REGION_WRITABLE))
    {
        c_serial_print(
            "[InitraOS] USER_REGION_FAIL_STACK_FLAGS\n");
        return;
    }

    if ((user_code_region.base & (FRAME_SIZE - 1)) != 0 ||
        (user_stack_region.base & (FRAME_SIZE - 1)) != 0)
    {
        c_serial_print(
            "[InitraOS] USER_REGION_FAIL_ALIGN\n");
        return;
    }

    if (user_code_region.base +
            user_code_region.size >
        user_stack_region.base)
    {
        c_serial_print(
            "[InitraOS] USER_REGION_FAIL_OVERLAP\n");
        return;
    }

    c_serial_print(
        "[InitraOS] USER_REGION_OK\n");
}

static void task_test_function(void)
{
    volatile unsigned short *vga =
        (volatile unsigned short *)
        0xB8700;

    const char *message =
        "TASK SWITCH WORKED!";

    for (unsigned int i = 0;
         message[i] != 0;
         i++)
    {
        vga[i] =
            0x0700 |
            message[i];
    }

    task_exit();
}

/* ---------- Keyboard ---------- */

static char keyboard_buffer[
    KEYBOARD_BUFFER_SIZE
];

static int keyboard_index = 0;

static int keyboard_column = 0;
static int keyboard_row = 13;

static int shift_pressed = 0;

/*
 * Set by the keyboard-driven shell when a user-space
 * security utility needs to be launched.
 *
 * The actual task switch happens from the kernel main loop,
 * not directly inside the keyboard interrupt handler.
 */
static volatile int shell_secaudit_requested = 0;


/* ---------- VGA Output ---------- */

static void print_at(
    int row,
    int column,
    const char *text
)
{
    volatile unsigned short *vga =
        (unsigned short *)0xB8000;

    while (*text != 0 &&
           column < 80)
    {
        int pos =
            row * 80 + column;

        vga[pos] =
            0x0700 |
            *text;

        column++;
        text++;
    }
}


/* ---------- Clear Screen ---------- */

static void clear_screen(void)
{
    volatile unsigned short *vga =
        (unsigned short *)0xB8000;

    for (int i = 0;
         i < 80 * 25;
         i++)
    {
        vga[i] =
            0x0720;
    }
}


/* ---------- Command Comparison ---------- */

static int command_equals(
    const char *command
)
{
    int i = 0;

    while (command[i] != 0)
    {
        if (keyboard_buffer[i] !=
            command[i])
        {
            return 0;
        }

        i++;
    }

    return keyboard_index == i;
}


/* ---------- Shell Prompt ---------- */

static void shell_prompt(void)
{
    print_at(
        keyboard_row,
        0,
        "InitraOS> "
    );

    keyboard_column =
        10;
}


/* ---------- Shell ---------- */

static void shell_run_secaudit(void)
{
    unsigned int user_entry_point = 0;
    task_t *user_task = 0;

    /*
     * Reload the current native user image.
     */
    if (!user_secaudit_prepare(
        &user_entry_point
    ))
    {
        keyboard_row++;

        print_at(
            keyboard_row,
            0,
            "secaudit: user image prepare failed"
        );

        goto shell_secaudit_done;
    }

    /*
     * Create a fresh Ring 3 task for the command.
     */
    user_task =
        task_create_user(
            0,
            user_entry_point
        );

    if (user_task == 0 ||
        user_task->context == 0)
    {
        keyboard_row++;

        print_at(
            keyboard_row,
            0,
            "secaudit: user task create failed"
        );

        goto shell_secaudit_done;
    }

    task_set_state(
        user_task,
        TASK_READY
    );

    current_task =
        user_task;

    task_set_state(
        user_task,
        TASK_RUNNING
    );

    /*
     * Save the current kernel-shell execution context
     * and enter the Ring 3 user program.
     *
     * SYSCALL_EXIT returns through kernel_context.
     */
    task_switch(
        &kernel_context,
        user_task->context
    );

    /*
     * Returning here means the user program exited.
     */
    if (user_task->state ==
            TASK_FINISHED &&
        current_task ==
            user_task)
    {
        if (*(volatile unsigned int *)
                (USER_STACK_BASE + 0x180U) ==
            0x53454341U)
        {
            keyboard_row++;

            print_at(
                keyboard_row,
                0,
                "secaudit: audit read completed"
            );
        }
        else
        {
            keyboard_row++;

            print_at(
                keyboard_row,
                0,
                "secaudit: audit read failed"
            );
        }
    }
    else
    {
        keyboard_row++;

        print_at(
            keyboard_row,
            0,
            "secaudit: user task exit failed"
        );
    }

    /*
     * We are no longer executing inside the user task.
     * Clear current_task before destroying it because
     * task_destroy() refuses to destroy the active task.
     */
    current_task = 0;

    task_destroy(
        user_task
    );

shell_secaudit_done:

    keyboard_index = 0;

    if (keyboard_row >= 25)
    {
        keyboard_row = 13;
    }

    shell_prompt();
}

static void shell_execute(void)
{
    if (command_equals("clear"))
    {
        clear_screen();

        keyboard_index = 0;
        keyboard_row = 13;

        shell_prompt();

        return;
    }

    if (command_equals("cpu"))
    {
        keyboard_row++;

        print_at(
            keyboard_row,
            0,
            "CPU: "
        );

        print_at(
            keyboard_row,
            5,
            cpu_vendor
        );

        keyboard_row++;

        keyboard_index = 0;

        if (keyboard_row >= 25)
        {
            keyboard_row = 13;
        }

        shell_prompt();

        return;
    }

    if (command_equals("help"))
    {
        keyboard_row++;

        print_at(
            keyboard_row,
            0,
            "Commands:"
        );

        keyboard_row++;

        print_at(
            keyboard_row,
            0,
            "help"
        );

        keyboard_row++;

        print_at(
            keyboard_row,
            0,
            "clear"
        );

        keyboard_row++;

        print_at(
            keyboard_row,
            0,
            "cpu"
        );

        keyboard_row++;

        print_at(
            keyboard_row,
            0,
            "about"
        );

        keyboard_row++;

        print_at(
            keyboard_row,
            0,
            "secaudit"
        );

        keyboard_row++;

    }
    else if (command_equals("about"))
    {
        keyboard_row++;

        print_at(
            keyboard_row,
            0,
            "InitraOS - custom 32-bit operating system"
        );

        keyboard_row++;
    }

    else if (command_equals("secaudit"))
{
    /*
     * Do not launch the user task from the keyboard IRQ.
     * Request it and let the kernel shell loop perform
     * the context switch safely.
     */
    shell_secaudit_requested = 1;

    keyboard_index = 0;

    return;
}

    else if (keyboard_index > 0)
    {
        keyboard_row++;

        print_at(
            keyboard_row,
            0,
            "Unknown command"
        );

        keyboard_row++;
    }

    keyboard_index = 0;

    if (keyboard_row >= 25)
    {
        keyboard_row = 13;
    }

    shell_prompt();
}

static void initrafs_superblock_test(void)
{
    initrafs_superblock_t superblock;

    if (!initrafs_superblock_init(
            &superblock,
            4096U))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_SUPERBLOCK_FAIL\n"
        );
        return;
    }

    if (!initrafs_superblock_validate(
            &superblock))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_SUPERBLOCK_FAIL\n"
        );
        return;
    }

    /*
     * Verify that validation rejects
     * an invalid filesystem signature.
     */
    unsigned int original_magic =
        superblock.magic;

    superblock.magic = 0;

    if (initrafs_superblock_validate(
            &superblock))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_SUPERBLOCK_FAIL\n"
        );
        return;
    }

    superblock.magic =
        original_magic;

    if (!initrafs_superblock_validate(
            &superblock))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_SUPERBLOCK_FAIL\n"
        );
        return;
    }

    c_serial_print(
        "[InitraOS] INITRAFS_SUPERBLOCK_OK\n"
    );
}

static void initrafs_root_test(void)
{
    initrafs_superblock_t superblock;
    initrafs_disk_inode_t root_inode;
    initrafs_disk_dirent_t root_entries[
        INITRAFS_ROOT_DIR_ENTRIES
    ];

    if (!initrafs_superblock_init(
            &superblock,
            4096U))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_ROOT_FAIL\n"
        );
        return;
    }

    if (!initrafs_root_inode_init(
            &root_inode,
            &superblock))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_ROOT_FAIL\n"
        );
        return;
    }

    if (!initrafs_root_directory_init(
            root_entries,
            INITRAFS_ROOT_DIR_ENTRIES))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_ROOT_FAIL\n"
        );
        return;
    }

    if (root_inode.inode_number !=
            INITRAFS_ROOT_INODE ||
        root_inode.type !=
            INITRAFS_TYPE_DIRECTORY ||
        root_inode.mode !=
            INITRAFS_ROOT_MODE ||
        root_inode.link_count !=
            INITRAFS_ROOT_LINK_COUNT ||
        root_inode.direct_blocks[0] !=
            superblock.data_start)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_ROOT_FAIL\n"
        );
        return;
    }

    if (root_entries[0].inode_number !=
            INITRAFS_ROOT_INODE ||
        root_entries[0].type !=
            INITRAFS_TYPE_DIRECTORY ||
        root_entries[0].name_length != 1U ||
        root_entries[0].name[0] != '.')
    {
        c_serial_print(
            "[InitraOS] INITRAFS_ROOT_FAIL\n"
        );
        return;
    }

    if (root_entries[1].inode_number !=
            INITRAFS_ROOT_INODE ||
        root_entries[1].type !=
            INITRAFS_TYPE_DIRECTORY ||
        root_entries[1].name_length != 2U ||
        root_entries[1].name[0] != '.' ||
        root_entries[1].name[1] != '.')
    {
        c_serial_print(
            "[InitraOS] INITRAFS_ROOT_FAIL\n"
        );
        return;
    }

    if (root_inode.size !=
        INITRAFS_ROOT_DIR_ENTRIES *
        sizeof(initrafs_disk_dirent_t))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_ROOT_FAIL\n"
        );
        return;
    }

    c_serial_print(
        "[InitraOS] INITRAFS_ROOT_OK\n"
    );
}

static void initrafs_block_allocator_test(void)
{
    #define INITRAFS_TEST_TOTAL_BLOCKS 4096U
    #define INITRAFS_TEST_BITMAP_BYTES         ((INITRAFS_TEST_TOTAL_BLOCKS + 7U) / 8U)

    initrafs_superblock_t superblock;

    initrafs_block_allocator_t allocator;

    unsigned char bitmap[
        INITRAFS_TEST_BITMAP_BYTES
    ];

    if (!initrafs_superblock_init(
            &superblock,
            INITRAFS_TEST_TOTAL_BLOCKS))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_FAIL\n"
        );
        return;
    }

    if (!initrafs_block_allocator_init(
            &allocator,
            &superblock,
            bitmap,
            sizeof(bitmap)))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_FAIL\n"
        );
        return;
    }

    /*
     * data_start is reserved for the root
     * directory, so it must not be allocated.
     */
    if (initrafs_block_alloc(
            &allocator) !=
        superblock.data_start + 1U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_FAIL\n"
        );
        return;
    }

    unsigned int first_block =
        superblock.data_start + 1U;

    unsigned int second_block =
        initrafs_block_alloc(
            &allocator
        );

    if (second_block !=
        first_block + 1U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_FAIL\n"
        );
        return;
    }

    if (!initrafs_block_free(
            &allocator,
            first_block))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_FAIL\n"
        );
        return;
    }

    if (initrafs_block_free(
            &allocator,
            first_block))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_FAIL\n"
        );
        return;
    }

    unsigned int reused_block =
        initrafs_block_alloc(
            &allocator
        );

    if (reused_block !=
        first_block)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_FAIL\n"
        );
        return;
    }

    if (initrafs_block_free(
            &allocator,
            INITRAFS_SUPERBLOCK_BLOCK) ||
        initrafs_block_free(
            &allocator,
            superblock.data_start))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_FAIL\n"
        );
        return;
    }

    if (!initrafs_block_free(
            &allocator,
            reused_block) ||
        !initrafs_block_free(
            &allocator,
            second_block))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_FAIL\n"
        );
        return;
    }

    if (superblock.free_block_count !=
        INITRAFS_TEST_TOTAL_BLOCKS -
        superblock.data_start -
        1U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_FAIL\n"
        );
        return;
    }

    c_serial_print(
        "[InitraOS] INITRAFS_BLOCK_OK\n"
    );
}

static void initrafs_inode_allocator_test(void)
{
    #define INITRAFS_TEST_INODE_BITMAP_BYTES 16U

    initrafs_superblock_t superblock;

    initrafs_inode_allocator_t allocator;

    unsigned char bitmap[
        INITRAFS_TEST_INODE_BITMAP_BYTES
    ];

    if (!initrafs_superblock_init(
            &superblock,
            4096U))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_FAIL\n"
        );
        return;
    }

    if (!initrafs_inode_allocator_init(
            &allocator,
            &superblock,
            bitmap,
            sizeof(bitmap)))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_FAIL\n"
        );
        return;
    }

    /*
     * Inode 0 is unused and inode 1 is the root.
     * The first allocatable inode is therefore 2.
     */
    unsigned int first_inode =
        initrafs_inode_alloc(
            &allocator
        );

    if (first_inode != 2U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_FAIL\n"
        );
        return;
    }

    unsigned int second_inode =
        initrafs_inode_alloc(
            &allocator
        );

    if (second_inode != 3U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_FAIL\n"
        );
        return;
    }

    /*
     * Free inode 2 and verify it can be reused.
     */
    if (!initrafs_inode_free(
            &allocator,
            first_inode))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_FAIL\n"
        );
        return;
    }

    /*
     * Double-free must fail.
     */
    if (initrafs_inode_free(
            &allocator,
            first_inode))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_FAIL\n"
        );
        return;
    }

    unsigned int reused_inode =
        initrafs_inode_alloc(
            &allocator
        );

    if (reused_inode !=
        first_inode)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_FAIL\n"
        );
        return;
    }

    /*
     * Inode 0, root inode 1, and inode_count
     * itself are invalid for the allocator.
     */
    if (initrafs_inode_free(
            &allocator,
            INITRAFS_UNUSED_INODE) ||
        initrafs_inode_free(
            &allocator,
            INITRAFS_ROOT_INODE) ||
        initrafs_inode_free(
            &allocator,
            superblock.inode_count))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_FAIL\n"
        );
        return;
    }

    if (!initrafs_inode_free(
            &allocator,
            reused_inode) ||
        !initrafs_inode_free(
            &allocator,
            second_inode))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_FAIL\n"
        );
        return;
    }

    /*
     * 128 total inodes:
     * inode 0 unused + inode 1 root,
     * leaving 126 allocatable/free inodes.
     */
    if (allocator.free_inodes !=
        superblock.inode_count - 2U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_FAIL\n"
        );
        return;
    }

    c_serial_print(
        "[InitraOS] INITRAFS_INODE_OK\n"
    );
}

static void initrafs_inode_create_test(void)
{
    #define INITRAFS_TEST_INODE_BITMAP_BYTES 16U

    initrafs_superblock_t superblock;

    initrafs_inode_allocator_t allocator;

    unsigned char bitmap[
        INITRAFS_TEST_INODE_BITMAP_BYTES
    ];

    struct fs_inode file_inode;
    struct fs_inode directory_inode;

    if (!initrafs_superblock_init(
            &superblock,
            4096U))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_CREATE_FAIL\n"
        );
        return;
    }

    if (!initrafs_inode_allocator_init(
            &allocator,
            &superblock,
            bitmap,
            sizeof(bitmap)))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_CREATE_FAIL\n"
        );
        return;
    }

    if (!initrafs_inode_create(
            &allocator,
            &file_inode,
            INODE_TYPE_FILE,
            0644U))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_CREATE_FAIL\n"
        );
        return;
    }

    if (file_inode.inode_number != 2U ||
        file_inode.type != INODE_TYPE_FILE ||
        file_inode.mode != 0644U ||
        file_inode.size != 0U ||
        file_inode.link_count != 1U ||
        file_inode.owner != 0U ||
        file_inode.group != 0U ||
        file_inode.filesystem_private != 0)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_CREATE_FAIL\n"
        );
        return;
    }

    if (!initrafs_inode_create(
            &allocator,
            &directory_inode,
            INODE_TYPE_DIRECTORY,
            0755U))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_CREATE_FAIL\n"
        );
        return;
    }

    /*
     * Verify basic owner/group/other permission
     * evaluation.
     */
    file_inode.owner = 100U;
    file_inode.group = 200U;
    file_inode.mode = 0640U;

    if (!initrafs_inode_check_permission(
            &file_inode,
            100U,
            999U,
            INITRAFS_PERMISSION_READ |
            INITRAFS_PERMISSION_WRITE
        ))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_CREATE_FAIL\n"
        );
        return;
    }

    if (!initrafs_inode_check_permission(
            &file_inode,
            101U,
            200U,
            INITRAFS_PERMISSION_READ
        ))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_CREATE_FAIL\n"
        );
        return;
    }

    if (initrafs_inode_check_permission(
            &file_inode,
            101U,
            200U,
            INITRAFS_PERMISSION_WRITE
        ))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_CREATE_FAIL\n"
        );
        return;
    }

    if (initrafs_inode_check_permission(
            &file_inode,
            101U,
            201U,
            INITRAFS_PERMISSION_READ
        ))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_CREATE_FAIL\n"
        );
        return;
    }

    if (directory_inode.inode_number != 3U ||
        directory_inode.type != INODE_TYPE_DIRECTORY ||
        directory_inode.mode != 0755U ||
        directory_inode.size != 0U ||
        directory_inode.link_count != 2U ||
        directory_inode.owner != 0U ||
        directory_inode.group != 0U ||
        directory_inode.filesystem_private != 0)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_CREATE_FAIL\n"
        );
        return;
    }

    /*
     * The allocator owns the inode number.
     * Freeing both objects must return them to
     * the allocator for later reuse.
     */
    if (!initrafs_inode_free(
            &allocator,
            file_inode.inode_number) ||
        !initrafs_inode_free(
            &allocator,
            directory_inode.inode_number))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_CREATE_FAIL\n"
        );
        return;
    }

    if (allocator.free_inodes !=
        superblock.inode_count - 2U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INODE_CREATE_FAIL\n"
        );
        return;
    }

    c_serial_print(
        "[InitraOS] INITRAFS_INODE_CREATE_OK\n"
    );
}

static void initrafs_directory_test(void)
{
    #define INITRAFS_TEST_DIRECTORY_ENTRIES 8U

    initrafs_superblock_t superblock;

    initrafs_disk_dirent_t entries[
        INITRAFS_TEST_DIRECTORY_ENTRIES
    ];

    inode_number_t inode_number;

    if (!initrafs_superblock_init(
            &superblock,
            4096U))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    if (!initrafs_root_directory_init(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    /*
     * Existing root entries must be searchable.
     */
    if (!initrafs_directory_lookup(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            ".",
            &inode_number) ||
        inode_number != INITRAFS_ROOT_INODE)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    if (!initrafs_directory_lookup(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            "..",
            &inode_number) ||
        inode_number != INITRAFS_ROOT_INODE)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    /*
     * Add a file and a directory.
     */
    if (!initrafs_directory_add(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            2U,
            INITRAFS_TYPE_FILE,
            "readme"))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    if (!initrafs_directory_add(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            3U,
            INITRAFS_TYPE_DIRECTORY,
            "bin"))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    if (!initrafs_directory_lookup(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            "readme",
            &inode_number) ||
        inode_number != 2U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    if (!initrafs_directory_lookup(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            "bin",
            &inode_number) ||
        inode_number != 3U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    /*
     * Duplicate names must be rejected.
     */
    if (initrafs_directory_add(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            4U,
            INITRAFS_TYPE_FILE,
            "readme"))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    /*
     * Empty names and overly long names are invalid.
     */
    if (initrafs_directory_add(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            4U,
            INITRAFS_TYPE_FILE,
            ""))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    /*
     * The "." and ".." entries are reserved.
     */
    if (initrafs_directory_add(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            4U,
            INITRAFS_TYPE_FILE,
            "."))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    if (initrafs_directory_remove(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            ".") ||
        initrafs_directory_remove(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            ".."))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    /*
     * Remove a normal entry and verify lookup fails.
     */
    if (!initrafs_directory_remove(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            "readme"))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    if (initrafs_directory_lookup(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            "readme",
            &inode_number))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    /*
     * Removing the same entry again must fail.
     */
    if (initrafs_directory_remove(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            "readme"))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    /*
     * A freed slot must be reusable.
     */
    if (!initrafs_directory_add(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            4U,
            INITRAFS_TYPE_FILE,
            "readme2"))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    if (!initrafs_directory_lookup(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            "readme2",
            &inode_number) ||
        inode_number != 4U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    /*
     * Fill all remaining free slots.
     */
    if (!initrafs_directory_add(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            5U,
            INITRAFS_TYPE_FILE,
            "one") ||
        !initrafs_directory_add(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            6U,
            INITRAFS_TYPE_FILE,
            "two") ||
        !initrafs_directory_add(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            7U,
            INITRAFS_TYPE_FILE,
            "three") ||
        !initrafs_directory_add(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            8U,
            INITRAFS_TYPE_FILE,
            "four"))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    /*
     * No free directory entry remains.
     */
    if (initrafs_directory_add(
            entries,
            INITRAFS_TEST_DIRECTORY_ENTRIES,
            9U,
            INITRAFS_TYPE_FILE,
            "full"))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    c_serial_print(
        "[InitraOS] INITRAFS_DIRECTORY_OK\n"
    );
}

static void initrafs_inode_block_mapping_test(void)
{
    initrafs_superblock_t superblock;
    initrafs_disk_inode_t inode;

    unsigned int physical_block;

    /*
     * A fresh on-disk inode must start with
     * all direct mappings clear.
     */
    for (unsigned int index = 0;
         index < 8U;
         index++)
    {
        inode.direct_blocks[index] = 0U;
    }

    if (!initrafs_superblock_init(
            &superblock,
            4096U))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_MAP_FAIL\n"
        );
        return;
    }

    /*
     * These are valid data blocks for this test.
     */
    unsigned int block0 =
        superblock.data_start + 1U;

    unsigned int block1 =
        superblock.data_start + 2U;

    unsigned int block7 =
        superblock.data_start + 8U;

    /*
     * Map logical blocks 0, 1 and 7.
     */
    if (!initrafs_inode_map_block(
            &inode,
            &superblock,
            0U,
            block0) ||
        !initrafs_inode_map_block(
            &inode,
            &superblock,
            1U,
            block1) ||
        !initrafs_inode_map_block(
            &inode,
            &superblock,
            7U,
            block7))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_MAP_FAIL\n"
        );
        return;
    }

    /*
     * Verify the three mappings.
     */
    if (!initrafs_inode_get_block(
            &inode,
            0U,
            &physical_block) ||
        physical_block != block0)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_MAP_FAIL\n"
        );
        return;
    }

    if (!initrafs_inode_get_block(
            &inode,
            1U,
            &physical_block) ||
        physical_block != block1)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_MAP_FAIL\n"
        );
        return;
    }

    if (!initrafs_inode_get_block(
            &inode,
            7U,
            &physical_block) ||
        physical_block != block7)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_MAP_FAIL\n"
        );
        return;
    }

    /*
     * Logical block 8 is outside the direct
     * block range and must be rejected.
     */
    if (initrafs_inode_map_block(
            &inode,
            &superblock,
            8U,
            block0) ||
        initrafs_inode_get_block(
            &inode,
            8U,
            &physical_block) ||
        initrafs_inode_unmap_block(
            &inode,
            8U))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_MAP_FAIL\n"
        );
        return;
    }

    /*
     * Metadata/root blocks cannot be mapped.
     */
    if (initrafs_inode_map_block(
            &inode,
            &superblock,
            2U,
            INITRAFS_SUPERBLOCK_BLOCK) ||
        initrafs_inode_map_block(
            &inode,
            &superblock,
            3U,
            superblock.data_start))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_MAP_FAIL\n"
        );
        return;
    }

    /*
     * Physical block zero is invalid.
     */
    if (initrafs_inode_map_block(
            &inode,
            &superblock,
            2U,
            0U))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_MAP_FAIL\n"
        );
        return;
    }

    /*
     * Existing mappings cannot be overwritten.
     */
    if (initrafs_inode_map_block(
            &inode,
            &superblock,
            0U,
            block1))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_MAP_FAIL\n"
        );
        return;
    }

    /*
     * Unmap logical block 1 and verify that
     * the mapping disappears.
     */
    if (!initrafs_inode_unmap_block(
            &inode,
            1U))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_MAP_FAIL\n"
        );
        return;
    }

    if (initrafs_inode_get_block(
            &inode,
            1U,
            &physical_block))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_MAP_FAIL\n"
        );
        return;
    }

    /*
     * The freed logical slot can be mapped again.
     */
    if (!initrafs_inode_map_block(
            &inode,
            &superblock,
            1U,
            block1))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_MAP_FAIL\n"
        );
        return;
    }

    if (!initrafs_inode_get_block(
            &inode,
            1U,
            &physical_block) ||
        physical_block != block1)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_MAP_FAIL\n"
        );
        return;
    }

    /*
     * Double-unmap must fail.
     */
    if (!initrafs_inode_unmap_block(
            &inode,
            0U) ||
        !initrafs_inode_unmap_block(
            &inode,
            1U) ||
        !initrafs_inode_unmap_block(
            &inode,
            7U) ||
        initrafs_inode_unmap_block(
            &inode,
            1U))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_BLOCK_MAP_FAIL\n"
        );
        return;
    }

    c_serial_print(
        "[InitraOS] INITRAFS_BLOCK_MAP_OK\n"
    );
}



#define INITRAFS_TEST_FILE_DISK_BLOCKS 64U

static unsigned char initrafs_test_file_disk[
    INITRAFS_TEST_FILE_DISK_BLOCKS *
    INITRAFS_BLOCK_SIZE
];

static int initrafs_test_disk_read(
    block_device_t *device,
    unsigned int block,
    void *buffer
)
{
    if (device == 0 ||
        buffer == 0 ||
        device->private_data == 0 ||
        block >= device->block_count)
    {
        return 0;
    }

    unsigned char *storage =
        (unsigned char *)device->private_data;

    for (unsigned int index = 0;
         index < INITRAFS_BLOCK_SIZE;
         index++)
    {
        ((unsigned char *)buffer)[index] =
            storage[
                block * INITRAFS_BLOCK_SIZE +
                index
            ];
    }

    return 1;
}

static int initrafs_test_disk_write(
    block_device_t *device,
    unsigned int block,
    const void *buffer
)
{
    if (device == 0 ||
        buffer == 0 ||
        device->private_data == 0 ||
        block >= device->block_count)
    {
        return 0;
    }

    unsigned char *storage =
        (unsigned char *)device->private_data;

    for (unsigned int index = 0;
         index < INITRAFS_BLOCK_SIZE;
         index++)
    {
        storage[
            block * INITRAFS_BLOCK_SIZE +
            index
        ] =
            ((const unsigned char *)buffer)[index];
    }

    return 1;
}

static void initrafs_file_io_test(void)
{
    initrafs_superblock_t superblock;
    initrafs_block_allocator_t block_allocator;
    initrafs_inode_allocator_t inode_allocator;

    unsigned char block_bitmap[
        (INITRAFS_TEST_FILE_DISK_BLOCKS + 7U) / 8U
    ];

    unsigned char inode_bitmap[16U];

    struct fs_inode file_inode;
    initrafs_disk_inode_t disk_inode;

    block_device_t device;

    unsigned char write_buffer[700U];
    unsigned char read_buffer[704U];

    unsigned char patch[5U] =
    {
        0xAA,
        0xBB,
        0xCC,
        0xDD,
        0xEE
    };

    unsigned char append_data[3U] =
    {
        0x11,
        0x22,
        0x33
    };

    unsigned int initial_free_blocks;
    unsigned int initial_free_inodes;
    unsigned int physical_block;

    /*
     * Initialize deterministic test storage.
     */
    for (unsigned int index = 0;
         index <
         sizeof(initrafs_test_file_disk);
         index++)
    {
        initrafs_test_file_disk[index] = 0;
    }

    if (!initrafs_superblock_init(
            &superblock,
            INITRAFS_TEST_FILE_DISK_BLOCKS))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    if (!initrafs_block_allocator_init(
            &block_allocator,
            &superblock,
            block_bitmap,
            sizeof(block_bitmap)))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    if (!initrafs_inode_allocator_init(
            &inode_allocator,
            &superblock,
            inode_bitmap,
            sizeof(inode_bitmap)))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    device.block_size =
        INITRAFS_BLOCK_SIZE;

    device.block_count =
        INITRAFS_TEST_FILE_DISK_BLOCKS;

    device.read =
        initrafs_test_disk_read;

    device.write =
        initrafs_test_disk_write;

    device.private_data =
        initrafs_test_file_disk;

    initial_free_blocks =
        block_allocator.free_blocks;

    initial_free_inodes =
        inode_allocator.free_inodes;

    /*
     * Create a new file.
     */
    if (!initrafs_file_create(
            &inode_allocator,
            &file_inode,
            &disk_inode,
            0644U))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    if (file_inode.inode_number != 2U ||
        disk_inode.inode_number != 2U ||
        file_inode.type != INODE_TYPE_FILE ||
        disk_inode.type != INITRAFS_TYPE_FILE ||
        file_inode.mode != 0644U ||
        disk_inode.mode != 0644U ||
        file_inode.size != 0U ||
        disk_inode.size != 0U ||
        file_inode.link_count != 1U ||
        disk_inode.link_count != 1U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    /*
     * Prepare 700 bytes of deterministic file data.
     */
    for (unsigned int index = 0;
         index < sizeof(write_buffer);
         index++)
    {
        write_buffer[index] =
            (unsigned char)(
                (index * 7U + 3U) &
                0xFFU
            );
    }

    /*
     * 700 bytes must require two data blocks.
     */
    if (initrafs_file_write(
            &file_inode,
            &disk_inode,
            &block_allocator,
            &device,
            0U,
            write_buffer,
            sizeof(write_buffer)) !=
        (int)sizeof(write_buffer))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    if (file_inode.size != 700U ||
        disk_inode.size != 700U ||
        block_allocator.free_blocks !=
            initial_free_blocks - 2U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    if (!initrafs_inode_get_block(
            &disk_inode,
            0U,
            &physical_block) ||
        physical_block !=
            superblock.data_start + 1U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    if (!initrafs_inode_get_block(
            &disk_inode,
            1U,
            &physical_block) ||
        physical_block !=
            superblock.data_start + 2U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    /*
     * Read back the entire file.
     */
    if (initrafs_file_read(
            &file_inode,
            &disk_inode,
            &device,
            0U,
            read_buffer,
            700U) != 700)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    for (unsigned int index = 0;
         index < 700U;
         index++)
    {
        if (read_buffer[index] !=
            write_buffer[index])
        {
            c_serial_print(
                "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
            );
            return;
        }
    }

    /*
     * Partial overwrite inside the first block.
     */
    if (initrafs_file_write(
            &file_inode,
            &disk_inode,
            &block_allocator,
            &device,
            100U,
            patch,
            sizeof(patch)) !=
        (int)sizeof(patch))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    for (unsigned int index = 0;
         index < sizeof(patch);
         index++)
    {
        write_buffer[
            100U + index
        ] = patch[index];
    }

    if (file_inode.size != 700U ||
        disk_inode.size != 700U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    if (initrafs_file_read(
            &file_inode,
            &disk_inode,
            &device,
            0U,
            read_buffer,
            700U) != 700)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    for (unsigned int index = 0;
         index < 700U;
         index++)
    {
        if (read_buffer[index] !=
            write_buffer[index])
        {
            c_serial_print(
                "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
            );
            return;
        }
    }

    /*
     * Append three bytes to the file.
     */
    if (initrafs_file_write(
            &file_inode,
            &disk_inode,
            &block_allocator,
            &device,
            700U,
            append_data,
            sizeof(append_data)) !=
        (int)sizeof(append_data))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    if (file_inode.size != 703U ||
        disk_inode.size != 703U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    if (initrafs_file_read(
            &file_inode,
            &disk_inode,
            &device,
            700U,
            read_buffer,
            sizeof(append_data)) !=
        (int)sizeof(append_data))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    for (unsigned int index = 0;
         index < sizeof(append_data);
         index++)
    {
        if (read_buffer[index] !=
            append_data[index])
        {
            c_serial_print(
                "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
            );
            return;
        }
    }

    /*
     * Reading at EOF returns zero.
     */
    if (initrafs_file_read(
            &file_inode,
            &disk_inode,
            &device,
            703U,
            read_buffer,
            1U) != 0)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    /*
     * Writes beyond the current EOF are not yet
     * treated as sparse writes.
     */
    if (initrafs_file_write(
            &file_inode,
            &disk_inode,
            &block_allocator,
            &device,
            704U,
            append_data,
            1U) != -1)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    /*
     * The eight-direct-block file-size limit must
     * reject an operation that exceeds 4096 bytes.
     */
    if (initrafs_file_write(
            &file_inode,
            &disk_inode,
            &block_allocator,
            &device,
            0U,
            append_data,
            (8U * INITRAFS_BLOCK_SIZE) + 1U) != -1)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    /*
     * Release the allocated file blocks.
     */
    for (unsigned int logical = 0;
         logical < 8U;
         logical++)
    {
        if (initrafs_inode_get_block(
                &disk_inode,
                logical,
                &physical_block))
        {
            if (!initrafs_inode_unmap_block(
                    &disk_inode,
                    logical) ||
                !initrafs_block_free(
                    &block_allocator,
                    physical_block))
            {
                c_serial_print(
                    "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
                );
                return;
            }
        }
    }

    if (!initrafs_inode_free(
            &inode_allocator,
            file_inode.inode_number))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    if (block_allocator.free_blocks !=
            initial_free_blocks ||
        inode_allocator.free_inodes !=
            initial_free_inodes ||
        disk_inode.direct_blocks[0] != 0U ||
        disk_inode.direct_blocks[1] != 0U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_FILE_IO_FAIL\n"
        );
        return;
    }

    c_serial_print(
        "[InitraOS] INITRAFS_FILE_IO_OK\n"
    );
}

static void initrafs_directory_path_test(void)
{
    #define INITRAFS_TEST_NAMESPACE_NODES 4U
    #define INITRAFS_TEST_DIRECTORY_CAPACITY 8U

    initrafs_superblock_t superblock;
    initrafs_inode_allocator_t inode_allocator;

    unsigned char inode_bitmap[16U];

    struct fs_inode root_inode;
    struct fs_inode docs_inode;
    struct fs_inode file_inode;

    initrafs_disk_inode_t root_disk_inode;
    initrafs_disk_inode_t docs_disk_inode;
    initrafs_disk_inode_t file_disk_inode;

    initrafs_disk_dirent_t root_entries[
        INITRAFS_TEST_DIRECTORY_CAPACITY
    ];

    initrafs_disk_dirent_t docs_entries[
        INITRAFS_TEST_DIRECTORY_CAPACITY
    ];

    initrafs_namespace_node_t nodes[
        INITRAFS_TEST_NAMESPACE_NODES
    ];

    initrafs_namespace_t namespace;

    initrafs_disk_dirent_t listed[
        INITRAFS_TEST_DIRECTORY_CAPACITY
    ];

    unsigned int listed_count;
    inode_number_t inode_number;

    if (!initrafs_superblock_init(
            &superblock,
            4096U))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_CREATE_FAIL\n"
        );
        return;
    }

    if (!initrafs_inode_allocator_init(
            &inode_allocator,
            &superblock,
            inode_bitmap,
            sizeof(inode_bitmap)))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_CREATE_FAIL\n"
        );
        return;
    }

    if (!initrafs_root_inode_init(
            &root_disk_inode,
            &superblock) ||
        !initrafs_root_directory_init(
            root_entries,
            INITRAFS_TEST_DIRECTORY_CAPACITY) ||
        !initrafs_inode_init(
            &root_inode,
            INITRAFS_ROOT_INODE,
            INODE_TYPE_DIRECTORY,
            INITRAFS_ROOT_MODE))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_CREATE_FAIL\n"
        );
        return;
    }

    root_inode.size =
        root_disk_inode.size;

    root_inode.link_count =
        root_disk_inode.link_count;

    if (!initrafs_directory_create(
            &inode_allocator,
            &docs_inode,
            &docs_disk_inode,
            docs_entries,
            INITRAFS_TEST_DIRECTORY_CAPACITY,
            0755U,
            INITRAFS_ROOT_INODE))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_CREATE_FAIL\n"
        );
        return;
    }

    if (docs_inode.type !=
            INODE_TYPE_DIRECTORY ||
        docs_disk_inode.type !=
            INITRAFS_TYPE_DIRECTORY ||
        docs_inode.inode_number !=
            docs_disk_inode.inode_number ||
        docs_entries[0].inode_number !=
            docs_inode.inode_number ||
        docs_entries[1].inode_number !=
            INITRAFS_ROOT_INODE ||
        docs_entries[0].name_length != 1U ||
        docs_entries[1].name_length != 2U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_CREATE_FAIL\n"
        );
        return;
    }

    if (!initrafs_directory_add(
            root_entries,
            INITRAFS_TEST_DIRECTORY_CAPACITY,
            docs_inode.inode_number,
            INITRAFS_TYPE_DIRECTORY,
            "docs"))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_CREATE_FAIL\n"
        );
        return;
    }

    root_inode.link_count++;
    root_disk_inode.link_count++;

    c_serial_print(
        "[InitraOS] INITRAFS_DIRECTORY_CREATE_OK\n"
    );

    if (!initrafs_file_create(
            &inode_allocator,
            &file_inode,
            &file_disk_inode,
            0644U))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_LIST_FAIL\n"
        );
        return;
    }

    if (!initrafs_directory_add(
            docs_entries,
            INITRAFS_TEST_DIRECTORY_CAPACITY,
            file_inode.inode_number,
            INITRAFS_TYPE_FILE,
            "readme"))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_LIST_FAIL\n"
        );
        return;
    }

    if (!initrafs_directory_list(
            root_entries,
            INITRAFS_TEST_DIRECTORY_CAPACITY,
            listed,
            INITRAFS_TEST_DIRECTORY_CAPACITY,
            &listed_count) ||
        listed_count != 3U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_LIST_FAIL\n"
        );
        return;
    }

    if (!initrafs_directory_list(
            docs_entries,
            INITRAFS_TEST_DIRECTORY_CAPACITY,
            listed,
            INITRAFS_TEST_DIRECTORY_CAPACITY,
            &listed_count) ||
        listed_count != 3U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_LIST_FAIL\n"
        );
        return;
    }

    c_serial_print(
        "[InitraOS] INITRAFS_DIRECTORY_LIST_OK\n"
    );

    if (!initrafs_namespace_init(
            &namespace,
            nodes,
            INITRAFS_TEST_NAMESPACE_NODES,
            INITRAFS_ROOT_INODE) ||
        !initrafs_namespace_register(
            &namespace,
            &root_inode,
            &root_disk_inode,
            root_entries,
            INITRAFS_TEST_DIRECTORY_CAPACITY) ||
        !initrafs_namespace_register(
            &namespace,
            &docs_inode,
            &docs_disk_inode,
            docs_entries,
            INITRAFS_TEST_DIRECTORY_CAPACITY) ||
        !initrafs_namespace_register(
            &namespace,
            &file_inode,
            &file_disk_inode,
            0,
            0))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_PATH_FAIL\n"
        );
        return;
    }

    if (!initrafs_path_lookup(
            &namespace,
            "/",
            &inode_number) ||
        inode_number != INITRAFS_ROOT_INODE)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_PATH_FAIL\n"
        );
        return;
    }

    if (!initrafs_path_lookup(
            &namespace,
            "/docs",
            &inode_number) ||
        inode_number != docs_inode.inode_number)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_PATH_FAIL\n"
        );
        return;
    }

    if (!initrafs_path_lookup(
            &namespace,
            "//docs//readme",
            &inode_number) ||
        inode_number != file_inode.inode_number)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_PATH_FAIL\n"
        );
        return;
    }

    if (!initrafs_path_lookup(
            &namespace,
            "/docs/../docs/readme",
            &inode_number) ||
        inode_number != file_inode.inode_number)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_PATH_FAIL\n"
        );
        return;
    }

    if (initrafs_path_lookup(
            &namespace,
            "/docs/readme/more",
            &inode_number))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_PATH_FAIL\n"
        );
        return;
    }

    c_serial_print(
        "[InitraOS] INITRAFS_PATH_OK\n"
    );

    if (initrafs_path_remove_directory(
            &namespace,
            &inode_allocator,
            "/docs"))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_REMOVE_FAIL\n"
        );
        return;
    }

    if (!initrafs_directory_remove(
            docs_entries,
            INITRAFS_TEST_DIRECTORY_CAPACITY,
            "readme") ||
        !initrafs_inode_free(
            &inode_allocator,
            file_inode.inode_number) ||
        !initrafs_namespace_unregister(
            &namespace,
            file_inode.inode_number))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_REMOVE_FAIL\n"
        );
        return;
    }

    if (!initrafs_directory_is_empty(
            docs_entries,
            INITRAFS_TEST_DIRECTORY_CAPACITY))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_REMOVE_FAIL\n"
        );
        return;
    }

    if (!initrafs_path_remove_directory(
            &namespace,
            &inode_allocator,
            "/docs"))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_REMOVE_FAIL\n"
        );
        return;
    }

    if (initrafs_path_lookup(
            &namespace,
            "/docs",
            &inode_number))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_REMOVE_FAIL\n"
        );
        return;
    }

    if (root_inode.link_count !=
            INITRAFS_ROOT_LINK_COUNT ||
        root_disk_inode.link_count !=
            INITRAFS_ROOT_LINK_COUNT ||
        inode_allocator.free_inodes !=
            superblock.inode_count - 2U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_DIRECTORY_REMOVE_FAIL\n"
        );
        return;
    }

    c_serial_print(
        "[InitraOS] INITRAFS_DIRECTORY_REMOVE_OK\n"
    );
}

static void vfs_test(void)
{
    if (!vfs_self_test())
    {
        c_serial_print(
            "[InitraOS] VFS_FAIL\n"
        );
        return;
    }

    c_serial_print(
        "[InitraOS] VFS_OK\n"
    );
}

static void initrafs_instance_test(void)
{
    initrafs_instance_t instance;
    block_device_t device;

    device.block_size =
        INITRAFS_BLOCK_SIZE;

    device.block_count =
        INITRAFS_TEST_FILE_DISK_BLOCKS;

    device.read =
        initrafs_test_disk_read;

    device.write =
        initrafs_test_disk_write;

    device.private_data =
        initrafs_test_file_disk;

    if (!initrafs_instance_init(
            &instance,
            &device))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INSTANCE_FAIL\n"
        );
        return;
    }

    if (instance.mounted != 1U ||
        instance.device != &device ||
        instance.superblock.total_blocks !=
            device.block_count ||
        instance.superblock.root_inode !=
            INITRAFS_ROOT_INODE ||
        instance.root_inode.inode_number !=
            INITRAFS_ROOT_INODE ||
        instance.root_inode.type !=
            INODE_TYPE_DIRECTORY ||
        instance.root_disk_inode.type !=
            INITRAFS_TYPE_DIRECTORY ||
        instance.namespace.node_count != 1U ||
        instance.namespace.root_inode !=
            INITRAFS_ROOT_INODE ||
        instance.namespace.nodes[0].inode !=
            &instance.root_inode ||
        instance.namespace.nodes[0].disk_inode !=
            &instance.root_disk_inode)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INSTANCE_FAIL\n"
        );
        return;
    }

    if (!initrafs_instance_unmount(
            &instance))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INSTANCE_FAIL\n"
        );
        return;
    }

    if (instance.mounted != 0U ||
        instance.device != 0 ||
        instance.namespace.node_count != 0U)
    {
        c_serial_print(
            "[InitraOS] INITRAFS_INSTANCE_FAIL\n"
        );
        return;
    }

    c_serial_print(
        "[InitraOS] INITRAFS_INSTANCE_OK\n"
    );
}

static void initrafs_vfs_test(void)
{
    initrafs_instance_t instance;
    filesystem_t filesystem;
    block_device_t device;

    struct fs_inode file_inode;
    initrafs_disk_inode_t disk_inode;

    vfs_file_t *file = 0;

    const unsigned char write_data[5] =
    {
        'h',
        'e',
        'l',
        'l',
        'o'
    };

    unsigned char read_data[5];

    unsigned int registered = 0;
    unsigned int mounted = 0;

    device.block_size =
        INITRAFS_BLOCK_SIZE;

    device.block_count =
        INITRAFS_TEST_FILE_DISK_BLOCKS;

    device.read =
        initrafs_test_disk_read;

    device.write =
        initrafs_test_disk_write;

    device.private_data =
        initrafs_test_file_disk;

    if (!initrafs_vfs_init(
            &filesystem,
            &instance))
    {
        goto fail;
    }

    if (!filesystem_register(
            &filesystem))
    {
        goto fail;
    }

    registered = 1;

    if (!vfs_mount(
            &filesystem,
            &device))
    {
        goto fail;
    }

    mounted = 1;

    /*
     * Create one real InitraFS file inside
     * the mounted namespace.
     */
    if (!initrafs_file_create(
            &instance.inode_allocator,
            &file_inode,
            &disk_inode,
            0644U))
    {
        goto fail;
    }

    if (!initrafs_directory_add(
            instance.root_entries,
            INITRAFS_INSTANCE_DIRECTORY_ENTRIES,
            file_inode.inode_number,
            INITRAFS_TYPE_FILE,
            "hello"))
    {
        goto fail;
    }

    if (!initrafs_namespace_register(
            &instance.namespace,
            &file_inode,
            &disk_inode,
            0,
            0))
    {
        goto fail;
    }

    /*
     * Open the real InitraFS file through the
     * generic VFS layer.
     */
    if (!vfs_open(
            "/hello",
            0,
            &file) ||
        file == 0)
    {
        goto fail;
    }

    if (vfs_write(
            file,
            write_data,
            sizeof(write_data)) !=
        (int)sizeof(write_data))
    {
        goto fail;
    }

    if (file->position !=
        sizeof(write_data) ||
        file->inode->size !=
        sizeof(write_data))
    {
        goto fail;
    }

    if (!vfs_seek(
            file,
            0U))
    {
        goto fail;
    }

    if (vfs_read(
            file,
            read_data,
            sizeof(read_data)) !=
        (int)sizeof(read_data))
    {
        goto fail;
    }

    for (unsigned int index = 0;
         index < sizeof(read_data);
         index++)
    {
        if (read_data[index] !=
            write_data[index])
        {
            goto fail;
        }
    }

    if (!vfs_close(file))
    {
        file = 0;
        goto fail;
    }

    file = 0;

    if (!vfs_unmount(
            &filesystem))
    {
        goto fail;
    }

    mounted = 0;

    if (!filesystem_unregister(
            &filesystem))
    {
        registered = 0;
        goto fail;
    }

    registered = 0;

    c_serial_print(
        "[InitraOS] INITRAFS_VFS_OK\n"
    );

    return;

fail:

    if (file != 0)
    {
        vfs_close(file);
    }

    if (mounted)
    {
        vfs_unmount(&filesystem);
    }

    if (registered)
    {
        filesystem_unregister(&filesystem);
    }

    c_serial_print(
        "[InitraOS] INITRAFS_VFS_FAIL\n"
    );
}

static void initrafs_vfs_directory_test(void)
{
    initrafs_instance_t instance;
    filesystem_t filesystem;
    block_device_t device;

    initrafs_disk_dirent_t entries[
        INITRAFS_INSTANCE_DIRECTORY_ENTRIES
    ];

    unsigned int listed;

    device.block_size =
        INITRAFS_BLOCK_SIZE;

    device.block_count =
        INITRAFS_TEST_FILE_DISK_BLOCKS;

    device.read =
        initrafs_test_disk_read;

    device.write =
        initrafs_test_disk_write;

    device.private_data =
        initrafs_test_file_disk;

    if (!initrafs_vfs_init(
            &filesystem,
            &instance) ||
        !filesystem_register(
            &filesystem) ||
        !vfs_mount(
            &filesystem,
            &device))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_VFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    listed =
        (unsigned int)vfs_readdir(
            "/",
            entries,
            sizeof(entries)
        );

    if (listed != 2U ||
        entries[0].inode_number !=
            INITRAFS_ROOT_INODE ||
        entries[0].name_length != 1U ||
        entries[0].name[0] != '.' ||
        entries[1].inode_number !=
            INITRAFS_ROOT_INODE ||
        entries[1].name_length != 2U ||
        entries[1].name[0] != '.' ||
        entries[1].name[1] != '.')
    {
        vfs_unmount(&filesystem);
        filesystem_unregister(&filesystem);

        c_serial_print(
            "[InitraOS] INITRAFS_VFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    if (!vfs_unmount(
            &filesystem) ||
        !filesystem_unregister(
            &filesystem))
    {
        c_serial_print(
            "[InitraOS] INITRAFS_VFS_DIRECTORY_FAIL\n"
        );
        return;
    }

    c_serial_print(
        "[InitraOS] INITRAFS_VFS_DIRECTORY_OK\n"
    );
}
static void initrafs_vfs_rmdir_test(void)
{
    initrafs_instance_t instance;
    filesystem_t filesystem;
    block_device_t device;

    struct fs_inode docs_inode;
    initrafs_disk_inode_t docs_disk_inode;

    initrafs_disk_dirent_t docs_entries[
        INITRAFS_INSTANCE_DIRECTORY_ENTRIES
    ];

    unsigned int inode_number;
    unsigned int registered = 0;
    unsigned int mounted = 0;

    device.block_size =
        INITRAFS_BLOCK_SIZE;

    device.block_count =
        INITRAFS_TEST_FILE_DISK_BLOCKS;

    device.read =
        initrafs_test_disk_read;

    device.write =
        initrafs_test_disk_write;

    device.private_data =
        initrafs_test_file_disk;

    if (!initrafs_vfs_init(
            &filesystem,
            &instance) ||
        !filesystem_register(
            &filesystem))
    {
        goto fail;
    }

    registered = 1;

    if (!vfs_mount(
            &filesystem,
            &device))
    {
        goto fail;
    }

    mounted = 1;

    /*
     * The root directory itself cannot be removed.
     */
    if (vfs_rmdir("/") != 0)
    {
        goto fail;
    }

    /*
     * Create one empty child directory through
     * the existing InitraFS primitives.
     */
    if (!initrafs_directory_create(
            &instance.inode_allocator,
            &docs_inode,
            &docs_disk_inode,
            docs_entries,
            INITRAFS_INSTANCE_DIRECTORY_ENTRIES,
            0755U,
            INITRAFS_ROOT_INODE))
    {
        goto fail;
    }

    if (!initrafs_directory_add(
            instance.root_entries,
            INITRAFS_INSTANCE_DIRECTORY_ENTRIES,
            docs_inode.inode_number,
            INITRAFS_TYPE_DIRECTORY,
            "docs"))
    {
        goto fail;
    }

    instance.root_inode.link_count++;
    instance.root_disk_inode.link_count++;

    if (!initrafs_namespace_register(
            &instance.namespace,
            &docs_inode,
            &docs_disk_inode,
            docs_entries,
            INITRAFS_INSTANCE_DIRECTORY_ENTRIES))
    {
        goto fail;
    }

    /*
     * Remove the empty directory through VFS.
     */
    if (!vfs_rmdir("/docs"))
    {
        goto fail;
    }

    if (initrafs_path_lookup(
            &instance.namespace,
            "/docs",
            &inode_number))
    {
        goto fail;
    }

    if (instance.root_inode.link_count !=
            INITRAFS_ROOT_LINK_COUNT ||
        instance.root_disk_inode.link_count !=
            INITRAFS_ROOT_LINK_COUNT ||
        instance.inode_allocator.free_inodes !=
            instance.superblock.inode_count - 2U)
    {
        goto fail;
    }

    if (!vfs_unmount(
            &filesystem))
    {
        goto fail;
    }

    mounted = 0;

    if (!filesystem_unregister(
            &filesystem))
    {
        goto fail;
    }

    registered = 0;

    c_serial_print(
        "[InitraOS] INITRAFS_VFS_RMDIR_OK\n"
    );

    return;

fail:

    if (mounted)
    {
        vfs_unmount(
            &filesystem
        );
    }

    if (registered)
    {
        filesystem_unregister(
            &filesystem
        );
    }

    c_serial_print(
        "[InitraOS] INITRAFS_VFS_RMDIR_FAIL\n"
    );
}

static void security_core_test(void)
{
    security_audit_event_t denied_event;
    security_audit_event_t allowed_event;

    security_init();

    /*
     * A Ring 3 user must not be allowed to perform
     * a kernel-only protected operation.
     */
    if (security_authorize(
            100U,
            SECURITY_PRIVILEGE_USER,
            SECURITY_OPERATION_PROTECTED_TEST,
            SECURITY_PRIVILEGE_KERNEL
        ) != SECURITY_DENIED)
    {
        c_serial_print(
            "[InitraOS] SECURITY_CORE_FAIL_USER\n"
        );

        return;
    }

    /*
     * A Ring 0 kernel caller is allowed.
     */
    if (security_authorize(
            101U,
            SECURITY_PRIVILEGE_KERNEL,
            SECURITY_OPERATION_PROTECTED_TEST,
            SECURITY_PRIVILEGE_KERNEL
        ) != SECURITY_ALLOWED)
    {
        c_serial_print(
            "[InitraOS] SECURITY_CORE_FAIL_KERNEL\n"
        );

        return;
    }

    /*
     * Both authorization decisions must have generated
     * audit events.
     */
    if (security_audit_count() != 2U)
    {
        c_serial_print(
            "[InitraOS] SECURITY_CORE_FAIL_AUDIT_COUNT\n"
        );

        return;
    }

    if (!security_audit_get(
            0U,
            &denied_event
        ))
    {
        c_serial_print(
            "[InitraOS] SECURITY_CORE_FAIL_AUDIT_READ\n"
        );

        return;
    }

    if (!security_audit_get(
            1U,
            &allowed_event
        ))
    {
        c_serial_print(
            "[InitraOS] SECURITY_CORE_FAIL_AUDIT_READ\n"
        );

        return;
    }

    if (denied_event.pid != 100U ||
        denied_event.privilege !=
            SECURITY_PRIVILEGE_USER ||
        denied_event.operation !=
            SECURITY_OPERATION_PROTECTED_TEST ||
        denied_event.result !=
            SECURITY_DENIED)
    {
        c_serial_print(
            "[InitraOS] SECURITY_CORE_FAIL_DENIED\n"
        );

        return;
    }

    if (allowed_event.pid != 101U ||
        allowed_event.privilege !=
            SECURITY_PRIVILEGE_KERNEL ||
        allowed_event.operation !=
            SECURITY_OPERATION_PROTECTED_TEST ||
        allowed_event.result !=
            SECURITY_ALLOWED)
    {
        c_serial_print(
            "[InitraOS] SECURITY_CORE_FAIL_ALLOWED\n"
        );

        return;
    }

    c_serial_print(
        "[InitraOS] SECURITY_CORE_OK\n"
    );
}

static void security_resource_access_test(void)
{
    /*
     * A user process may access its own resource.
     */
    if (security_resource_authorize(
            100U,
            SECURITY_PRIVILEGE_USER,
            100U
        ) != SECURITY_ALLOWED)
    {
        c_serial_print(
            "[InitraOS] SECURITY_RESOURCE_FAIL_OWNER\n"
        );

        return;
    }

    /*
     * A different user process must not access
     * another process's resource.
     */
    if (security_resource_authorize(
            101U,
            SECURITY_PRIVILEGE_USER,
            100U
        ) != SECURITY_DENIED)
    {
        c_serial_print(
            "[InitraOS] SECURITY_RESOURCE_FAIL_ISOLATION\n"
        );

        return;
    }

    /*
     * Kernel privilege may access the resource.
     */
    if (security_resource_authorize(
            102U,
            SECURITY_PRIVILEGE_KERNEL,
            100U
        ) != SECURITY_ALLOWED)
    {
        c_serial_print(
            "[InitraOS] SECURITY_RESOURCE_FAIL_KERNEL\n"
        );

        return;
    }

    c_serial_print(
        "[InitraOS] SECURITY_RESOURCE_ACCESS_OK\n"
    );
}

static void security_audit_syscall_test(void)
{
    security_audit_event_t *event =
        (security_audit_event_t *)USER_STACK_BASE;

    unsigned int count;

    /*
     * security_core_test() creates exactly two events:
     *
     *   event 0 = user denied
     *   event 1 = kernel allowed
     */
    count =
        syscall_dispatcher(
            SYSCALL_SECURITY_AUDIT_COUNT,
            0, 0, 0, 0, 0
        );

    if (count != 2U)
    {
        c_serial_print(
            "[InitraOS] SECURITY_AUDIT_API_FAIL_COUNT\n"
        );

        return;
    }

    /*
     * Read the first real audit event into a validated
     * user-writable buffer.
     */
    if (syscall_dispatcher(
            SYSCALL_SECURITY_AUDIT_READ,
            0U,
            USER_STACK_BASE,
            0, 0, 0
        ) != SECURITY_ALLOWED)
    {
        c_serial_print(
            "[InitraOS] SECURITY_AUDIT_API_FAIL_READ\n"
        );

        return;
    }

    if (event->pid != 100U ||
        event->privilege !=
            SECURITY_PRIVILEGE_USER ||
        event->operation !=
            SECURITY_OPERATION_PROTECTED_TEST ||
        event->result !=
            SECURITY_DENIED)
    {
        c_serial_print(
            "[InitraOS] SECURITY_AUDIT_API_FAIL_EVENT\n"
        );

        return;
    }

    /*
     * A kernel address must never be accepted as a
     * user-space destination.
     */
    if (syscall_dispatcher(
            SYSCALL_SECURITY_AUDIT_READ,
            0U,
            KERNEL_TEST_ADDRESS,
            0, 0, 0
        ) != SECURITY_DENIED)
    {
        c_serial_print(
            "[InitraOS] SECURITY_AUDIT_API_FAIL_POINTER\n"
        );

        return;
    }

    c_serial_print(
        "[InitraOS] SECURITY_AUDIT_API_OK\n"
    );
}

static void security_syscall_test(
    task_t *user_task
)
{
    security_audit_event_t event;

    if (user_task == 0)
    {
        c_serial_print(
            "[InitraOS] SECURITY_SYSCALL_FAIL_TASK\n"
        );

        return;
    }

    if (security_audit_count() != 3U)
    {
        c_serial_print(
            "[InitraOS] SECURITY_SYSCALL_FAIL_COUNT\n"
        );

        return;
    }

    if (!security_audit_get(
            2U,
            &event
        ))
    {
        c_serial_print(
            "[InitraOS] SECURITY_SYSCALL_FAIL_READ\n"
        );

        return;
    }

    if (event.pid != user_task->id ||
        event.privilege !=
            SECURITY_PRIVILEGE_USER ||
        event.operation !=
            SECURITY_OPERATION_PROTECTED_TEST ||
        event.result !=
            SECURITY_DENIED)
    {
        c_serial_print(
            "[InitraOS] SECURITY_SYSCALL_FAIL_EVENT\n"
        );

        return;
    }

    c_serial_print(
        "[InitraOS] SECURITY_SYSCALL_DENIED_OK\n"
    );
}

/* ---------- Kernel Main ---------- */

void kernel_main(void)
{
    __asm__ volatile ("cli");

    heap_pointer =
        align_up_4k(
            (unsigned int)
            &__kernel_end
        );

    /*
     * Physical frame allocator validation.
     *
     * This runs before task creation and before paging,
     * using the E820 map prepared by Stage 2.
     */
    frame_allocator_test();
    frame_protection_test();
    frame_validation_test();

    /*
     * The kernel context is a Ring 0 context.
     */
    kernel_context.privilege =
        TASK_KERNEL;

    /*
     * Create the first kernel task.
     */

current_task =
    task_create();

    if (current_task == 0)
    {
        shell_prompt();
        return;
    }

task_t *next_task =
    task_schedule_next();

    if (next_task != 0)
    {
        current_task =
            next_task;

        current_task->state =
            TASK_RUNNING;
    }

    /*
     * Keep the existing kernel task-switch test.
     */
    unsigned int stack_top =
        (unsigned int)(
            task_test_stack +
            sizeof(task_test_stack)
        );

    stack_top &=
        ~0x0F;

    stack_top -=
        sizeof(unsigned int);

    *(unsigned int *)stack_top =
        (unsigned int)task_exit;

    test_context.eax = 0;
    test_context.ebx = 0;
    test_context.ecx = 0;
    test_context.edx = 0;
    test_context.esi = 0;
    test_context.edi = 0;
    test_context.ebp = 0;

    test_context.esp =
        stack_top;

    test_context.eip =
        (unsigned int)
        task_test_function;

    test_context.eflags =
        0x202;

    test_context.privilege =
        TASK_KERNEL;

    task_switch(
        &kernel_context,
        &test_context
    );

    /*
     * Execution resumes here after task_exit() switches back.
     */
    current_task = 0;

    print_at(
        22,
        0,
        "Task finished. Kernel resumed."
    );

    /*
     * Prepare the user image before paging is enabled.
     *
     * The executable loader returns the virtual address of
     * the program's declared entry point.
     */
    unsigned int user_entry_point = 0;

    if (!user_space_prepare(
            &user_entry_point
        ))
    {
        print_at(
            12,
            0,
            "USER IMAGE PREPARE FAILED"
        );

        shell_prompt();

        while (1)
        {
            __asm__ volatile ("hlt");
        }
    }

    /*
     * Create the user task from the loaded executable.
     */
    task_t *user_test =
        task_create_user(
            0,
            user_entry_point
        );

    if (user_test == 0)
    {
        print_at(
            11,
            0,
            "USER TASK CREATE FAILED"
        );
    }
    else
    {
        print_at(
            11,
            0,
            "USER TASK: RING 3 + PROTECTED MEMORY"
        );

        task_set_state(
            user_test,
            TASK_READY
        );
    }

    print_at(
        12,
        0,
        "PAGING: KERNEL SUPERVISOR / USER PAGES READY"
    );

    paging_init();
    paging_enable();
    heap_paging_enabled = 1;
    address_space_create_test();
    process_isolation_test();
    process_permission_isolation_test();
    process_create_test();
    process_instance_isolation_test();
    process_task_link_test();
    syscall_dispatcher_test();
    syscall_memory_test();
    security_core_test();
    security_audit_syscall_test();
    user_region_test();
    user_stack_test();
    frame_paging_test();
    dynamic_page_test();
    page_protection_test();
    page_user_protection_test();
    heap_paging_test();
    heap_dynamic_test();
    initrafs_superblock_test();
    initrafs_root_test();
    initrafs_block_allocator_test();
    initrafs_inode_allocator_test();
    initrafs_inode_create_test();
    initrafs_directory_test();
    initrafs_inode_block_mapping_test();
    initrafs_file_io_test();
    initrafs_directory_path_test();
    initrafs_instance_test();
    initrafs_vfs_directory_test();
    initrafs_vfs_rmdir_test();
    initrafs_vfs_test();
    vfs_test();

    /*
     * Start the user task through the privilege-aware task switch.
     *
     * This must happen before enable_long_mode(), because
     * enable_long_mode() enters the 64-bit kernel and does
     * not return.
     */
    if (user_test != 0 &&
        user_test->context != 0)
    {
        current_task =
            user_test;

        current_task->state =
            TASK_RUNNING;

        print_at(
            14,
            0,
            "SWITCHING TO RING 3 USER TASK"
        );

        task_switch(
            &kernel_context,
            user_test->context
        );

        /*
         * Returning here means the user task called
         * SYSCALL_EXIT and switched back to the
         * saved kernel context.
         */
        if (user_test->state ==
                TASK_FINISHED &&
            current_task ==
                user_test)
        {
            c_serial_print(
                "[InitraOS] USER_PROGRAM_EXIT_OK\n"
            );

            /*
             * Validate that the actual Ring 3 user program
             * executed its instructions and successfully
             * wrote to the user-writable stack page.
             */
            if (*(volatile unsigned int *)USER_STACK_BASE ==
                0x45584543)
            {
                c_serial_print(
                    "[InitraOS] USER_PROGRAM_EXECUTION_OK\n"
                );

                security_syscall_test(
                    user_test
                );

                security_resource_access_test();
            }
            else
            {
                c_serial_print(
                    "[InitraOS] USER_PROGRAM_EXECUTION_FAIL\n"
                );
            }
        }
        else
        {
            c_serial_print(
                "[InitraOS] USER_PROGRAM_EXIT_FAIL\n"
            );
        }
    }

        /*
     * CI/autoboot keeps the existing 64-bit validation path.
     *
     * Interactive boots stop here and enter the native
     * 32-bit kernel shell so user-space utilities can be
     * launched from commands.
     */
    if (kernel_autoboot_mode != 0)
    {
        enable_long_mode();

        print_at(
            13,
            0,
            "PAGING ENABLED: PROCESS SPACE PROTECTED"
        );

        while (1)
        {
            __asm__ volatile ("hlt");
        }
    }

    /*
     * The initial Ring 3 validation task is finished.
     * The interactive shell itself is not a task.
     */
    current_task = 0;

    keyboard_index = 0;
    keyboard_row = 13;

    shell_prompt();

    /*
     * Enable keyboard interrupts for the interactive shell.
     */
    __asm__ volatile ("sti");

    while (1)
    {
        if (shell_secaudit_requested)
        {
            shell_secaudit_requested = 0;

            shell_run_secaudit();
        }

        __asm__ volatile ("hlt");
    }
}

/* ---------- Keyboard Handler ---------- */

void keyboard_handle(
    unsigned char scancode
)
{
    volatile unsigned short *vga =
        (unsigned short *)0xB8000;

    char c = 0;

    /* Left Shift / Right Shift press */
    if (scancode == 0x2A ||
        scancode == 0x36)
    {
        shift_pressed = 1;
        return;
    }

    /* Left Shift / Right Shift release */
    if (scancode == 0xAA ||
        scancode == 0xB6)
    {
        shift_pressed = 0;
        return;
    }

    /* Backspace */
    if (scancode == 0x0E)
    {
        if (keyboard_column > 10 &&
            keyboard_index > 0)
        {
            keyboard_column--;
            keyboard_index--;

            keyboard_buffer[
                keyboard_index
            ] = 0;

            int pos =
                keyboard_row * 80 +
                keyboard_column;

            vga[pos] =
                0x0720;
        }

        return;
    }

    /* Enter */
    if (scancode == 0x1C)
    {
        keyboard_buffer[
            keyboard_index
        ] = 0;

        shell_execute();

        return;
    }

    /* Keyboard scan codes */
    switch (scancode)
    {
        case 0x10: c = 'q'; break;
        case 0x11: c = 'w'; break;
        case 0x12: c = 'e'; break;
        case 0x13: c = 'r'; break;
        case 0x14: c = 't'; break;
        case 0x15: c = 'y'; break;
        case 0x16: c = 'u'; break;
        case 0x17: c = 'i'; break;
        case 0x18: c = 'o'; break;
        case 0x19: c = 'p'; break;

        case 0x1E: c = 'a'; break;
        case 0x1F: c = 's'; break;
        case 0x20: c = 'd'; break;
        case 0x21: c = 'f'; break;
        case 0x22: c = 'g'; break;
        case 0x23: c = 'h'; break;
        case 0x24: c = 'j'; break;
        case 0x25: c = 'k'; break;
        case 0x26: c = 'l'; break;

        case 0x2C: c = 'z'; break;
        case 0x2D: c = 'x'; break;
        case 0x2E: c = 'c'; break;
        case 0x2F: c = 'v'; break;
        case 0x30: c = 'b'; break;
        case 0x31: c = 'n'; break;
        case 0x32: c = 'm'; break;

        case 0x39: c = ' '; break;
    }

    /* Shift + lowercase letter */
    if (shift_pressed &&
        c >= 'a' &&
        c <= 'z')
    {
        c =
            c - 'a' + 'A';
    }

    /* Add character to keyboard buffer */
    if (c != 0 &&
        keyboard_index <
            KEYBOARD_BUFFER_SIZE - 1 &&
        keyboard_column < 80)
    {
        keyboard_buffer[
            keyboard_index
        ] = c;

        keyboard_index++;

        int pos =
            keyboard_row * 80 +
            keyboard_column;

        vga[pos] =
            0x0700 | c;

        keyboard_column++;
    }
}

static int security_user_buffer_valid(
    unsigned int address,
    unsigned int size
)
{
    unsigned int directory_index;
    unsigned int table_index;
    unsigned int entry;

    /*
     * The current user-space design provides one writable
     * user stack page. Keep the first audit API deliberately
     * limited to that validated user buffer.
     */
    if (size == 0 ||
        size > USER_STACK_SIZE)
    {
        return 0;
    }

    /*
     * Prevent address + size overflow and ensure the complete
     * buffer remains inside the user stack region.
     */
    if (address < USER_STACK_BASE ||
        address > USER_STACK_TOP - size)
    {
        return 0;
    }

    directory_index =
        (address >> 22) & 0x3FF;

    table_index =
        (address >> 12) & 0x3FF;

    if (directory_index >=
            kernel_address_space.page_table_count)
    {
        return 0;
    }

    entry =
        kernel_address_space.page_tables[
            directory_index
        ][
            table_index
        ];

    /*
     * The destination must be:
     *
     *   present
     *   writable
     *   user-accessible
     */
    if ((entry &
         (PAGE_PRESENT |
          PAGE_WRITABLE |
          PAGE_USER)) !=
        (PAGE_PRESENT |
         PAGE_WRITABLE |
         PAGE_USER))
    {
        return 0;
    }

    return 1;
}

unsigned int syscall_dispatcher(
    unsigned int syscall_number,
    unsigned int arg1,
    unsigned int arg2,
    unsigned int arg3,
    unsigned int arg4,
    unsigned int arg5
)
{
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;

        switch (syscall_number)
    {
        case SYSCALL_EXIT:
case SYSCALL_WRITE:
case SYSCALL_YIELD:
    /*
     * These system calls are recognized but their
     * operations will be implemented separately.
     */
    return 0;

case SYSCALL_GETPID:
    /*
     * Return the ID of the currently running task.
     */
    if (current_task == 0)
    {
        return (unsigned int)-1;
    }

    c_serial_print(
        "[InitraOS] SYSCALL_GETPID_OK\n"
    );

    return current_task->id;

        case SYSCALL_ALLOC:
            /*
             * EBX / arg1 = allocation size.
             *
             * Return the allocated virtual address in EAX.
             */
            return (unsigned int)
                heap_alloc(arg1);

        case SYSCALL_FREE:
            /*
             * EBX / arg1 = allocated address.
             */
            heap_free(
                (void *)arg1
            );

            return 0;

        case SYSCALL_SECURITY_CHECK:
            /*
             * Test a kernel-only protected operation through
             * the real user-to-kernel syscall path.
             */
            if (current_task == 0)
            {
                return (unsigned int)-1;
            }

            return security_authorize(
                current_task->id,
                current_task->privilege,
                SECURITY_OPERATION_PROTECTED_TEST,
                SECURITY_PRIVILEGE_KERNEL
            );

        case SYSCALL_SECURITY_AUDIT_COUNT:
            /*
             * Return the number of currently stored audit events.
             */
            return security_audit_count();

        case SYSCALL_SECURITY_AUDIT_READ:
        {
            security_audit_event_t *destination =
                (security_audit_event_t *)arg2;

            /*
             * EBX / arg1 = audit event index
             * ECX / arg2 = user-space destination
             */
            if (!security_user_buffer_valid(
                    arg2,
                    sizeof(security_audit_event_t)
                ))
            {
                return SECURITY_DENIED;
            }

            if (!security_audit_get(
                    arg1,
                    destination
                ))
            {
                return SECURITY_DENIED;
            }

            return SECURITY_ALLOWED;
        }

        default:
            /*
             * Unknown system call.
             */
            return (unsigned int)-1;
    }
}

static void syscall_dispatcher_test(void)
{
    unsigned int result;

    result = syscall_dispatcher(
        SYSCALL_EXIT,
        0, 0, 0, 0, 0);

    if (result != 0)
    {
        c_serial_print(
            "[InitraOS] SYSCALL_DISPATCH_FAIL\n");
        return;
    }

    result = syscall_dispatcher(
        SYSCALL_WRITE,
        0, 0, 0, 0, 0);

    if (result != 0)
    {
        c_serial_print(
            "[InitraOS] SYSCALL_DISPATCH_FAIL\n");
        return;
    }

    result = syscall_dispatcher(
        SYSCALL_GETPID,
        0, 0, 0, 0, 0);

    /*
     * No task is running during the direct dispatcher tests.
     * GETPID therefore reports an invalid result.
     */
    if (current_task != 0 ||
        result != (unsigned int)-1)
    {
        c_serial_print(
            "[InitraOS] SYSCALL_DISPATCH_FAIL\n");
        return;
    }

    result = syscall_dispatcher(
        SYSCALL_YIELD,
        0, 0, 0, 0, 0);

    if (result != 0)
    {
        c_serial_print(
            "[InitraOS] SYSCALL_DISPATCH_FAIL\n");
        return;
    }

    result = syscall_dispatcher(
        SYSCALL_ALLOC,
        0, 0, 0, 0, 0);

    if (result != 0)
    {
        c_serial_print(
            "[InitraOS] SYSCALL_DISPATCH_FAIL\n");
        return;
    }

    result = syscall_dispatcher(
        SYSCALL_FREE,
        0, 0, 0, 0, 0);

    if (result != 0)
    {
        c_serial_print(
            "[InitraOS] SYSCALL_DISPATCH_FAIL\n");
        return;
    }

    /*
     * Unknown syscall numbers must return an error.
     */
    result = syscall_dispatcher(
        0xFF,
        0, 0, 0, 0, 0);

    if (result != (unsigned int)-1)
    {
        c_serial_print(
            "[InitraOS] SYSCALL_DISPATCH_FAIL\n");
        return;
    }

    c_serial_print(
        "[InitraOS] SYSCALL_DISPATCH_OK\n");
}

static void syscall_memory_test(void)
{
    const unsigned int magic =
        0x5CA110C0;

    unsigned int address =
        syscall_dispatcher(
            SYSCALL_ALLOC,
            64,
            0, 0, 0, 0
        );

    if (address == 0)
    {
        c_serial_print(
            "[InitraOS] SYSCALL_MEMORY_FAIL_ALLOC\n");
        return;
    }

    volatile unsigned int *value =
        (volatile unsigned int *)address;

    *value = magic;

    if (*value != magic)
    {
        c_serial_print(
            "[InitraOS] SYSCALL_MEMORY_FAIL_WRITE\n");

        syscall_dispatcher(
            SYSCALL_FREE,
            address,
            0, 0, 0, 0
        );

        return;
    }

    syscall_dispatcher(
        SYSCALL_FREE,
        address,
        0, 0, 0, 0
    );

    c_serial_print(
        "[InitraOS] SYSCALL_MEMORY_OK\n");
}