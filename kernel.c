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

extern void enter_user_mode(
    unsigned int entry,
    unsigned int stack_top
);

extern void user_mode_entry(void);
extern unsigned char user_mode_code_start[];
extern unsigned char user_mode_code_end[];

static void paging_init(void);
static void paging_enable(void);
static void page_directory_init(void);
static void page_tables_init(void);

static int page_map(
    unsigned int virtual_address,
    unsigned int physical_address,
    unsigned int flags
);

static int page_unmap(
    unsigned int virtual_address
);

/* ---------- Heap ---------- */

typedef struct heap_block
{
    unsigned int size;
    unsigned int free;
    struct heap_block *next;
} heap_block_t;

static unsigned int heap_pointer = 0;
static unsigned int heap_limit = 0x80000;

static heap_block_t *heap_first_block = 0;

static unsigned int align_up_4k(unsigned int address)
{
    return (address + 0xFFF) & ~0xFFF;
}

static void *heap_alloc(unsigned int size)
{
    if (size == 0)
    {
        return 0;
    }

    /* Align allocation size to 4 bytes */
    size = (size + 3) & ~3;

    /* Look for a previously freed block */
    heap_block_t *current = heap_first_block;

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
        heap_limit - heap_pointer < sizeof(heap_block_t) ||
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

static void heap_free(void *address)
{
    if (address == 0)
    {
        return;
    }

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
                current->next->free == 1)
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
                previous->free == 1)
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

#define USER_CODE_BASE      0x00100000
#define USER_STACK_BASE     0x007FF000
#define USER_STACK_TOP      0x00800000
#define VGA_MEMORY_PAGE     0x000B8000
#define KERNEL_TEST_ADDRESS 0x00008800

#define PAGE_PRESENT         0x001
#define PAGE_WRITABLE        0x002
#define PAGE_USER            0x004

#define PAGE_TABLE_COUNT     4

static unsigned int page_directory[1024]
    __attribute__((aligned(4096)));

static unsigned int page_tables[PAGE_TABLE_COUNT][1024]
    __attribute__((aligned(4096)));

typedef struct task_context task_context_t;

typedef struct task
{
    unsigned int id;
    unsigned int state;
    unsigned int privilege;

    unsigned int esp;
    unsigned int ebp;

    unsigned int stack_base;

    task_context_t *context;

    struct task *next;
} task_t;

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

static void page_directory_init(void)
{
    for (unsigned int entry = 0; entry < 1024; entry++)
    {
        page_directory[entry] = 0;
    }
}

/*
 * Build the initial identity-mapped page tables.
 * Each virtual page maps to the physical page at the same address.
 */

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
                address |
                PAGE_PRESENT |
                PAGE_WRITABLE;
        }
    }
}

/*
 * Initialize the initial address space by connecting
 * the identity-mapped page tables to the page directory.
 */

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
            PAGE_PRESENT |
            PAGE_WRITABLE;
    }

    page_directory[0] |= PAGE_USER;
    page_directory[1] |= PAGE_USER;

    /* Only explicitly required user pages are user accessible. */
    page_tables[0][VGA_MEMORY_PAGE >> 12] |= PAGE_USER;
    page_tables[0][(USER_CODE_BASE >> 12) & 0x3FF] |= PAGE_USER;
    page_tables[1][(USER_STACK_BASE >> 12) & 0x3FF] |= PAGE_USER;

    /* Keep the kernel image supervisor-only. */
    page_tables[0][KERNEL_TEST_ADDRESS >> 12] =
        KERNEL_TEST_ADDRESS |
        PAGE_PRESENT |
        PAGE_WRITABLE;
}

/*
 * Load the page directory into CR3 and enable
 * paging through the CPU's CR0.PG control bit.
 */

static void paging_enable(void)
{
    unsigned int directory =
        (unsigned int)page_directory;

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

static int page_map(
    unsigned int virtual_address,
    unsigned int physical_address,
    unsigned int flags
)
{
    unsigned int directory_index =
        (virtual_address >> 22) & 0x3FF;

    unsigned int table_index =
        (virtual_address >> 12) & 0x3FF;

    if (directory_index >= PAGE_TABLE_COUNT)
    {
        return 0;
    }

    page_tables[directory_index][table_index] =
        (physical_address & 0xFFFFF000) |
        (flags & 0xFFF);

    return 1;
}

static int page_unmap(unsigned int virtual_address)
{
    unsigned int directory_index =
        (virtual_address >> 22) & 0x3FF;

    unsigned int table_index =
        (virtual_address >> 12) & 0x3FF;

    if (directory_index >= PAGE_TABLE_COUNT)
    {
        return 0;
    }

    page_tables[directory_index][table_index] = 0;

    return 1;
}

static int user_space_prepare(void)
{
    unsigned int source_start =
        (unsigned int)user_mode_code_start;

    unsigned int source_end =
        (unsigned int)user_mode_code_end;

    unsigned int source_size =
        source_end - source_start;

    if (source_size == 0 ||
        source_size > 0x1000)
    {
        return 0;
    }

    unsigned char *source =
        (unsigned char *)source_start;

    unsigned char *destination =
        (unsigned char *)USER_CODE_BASE;

    for (unsigned int i = 0;
         i < source_size;
         i++)
    {
        destination[i] = source[i];
    }

    /* Clear the entire user stack page before entering Ring 3. */
    volatile unsigned char *user_stack =
        (volatile unsigned char *)USER_STACK_BASE;

    for (unsigned int i = 0;
         i < 0x1000;
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

static task_t *task_create_user(void)
{
    task_t *task =
        task_create();

    if (task == 0)
    {
        return 0;
    }

    task->privilege =
        TASK_USER;

    /*
     * The user task does not use the kernel heap stack for Ring 3.
     * Release the temporary kernel stack and use the dedicated
     * 0x007FF000 user stack page instead.
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

    /*
     * Give the user task its own CPU context.
     */
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
    task->context->ebx = 0;
    task->context->ecx = 0;
    task->context->edx = 0;
    task->context->esi = 0;
    task->context->edi = 0;
    task->context->ebp =
        task->ebp;

    task->context->esp =
        task->esp;

    /* User code is copied into a user-accessible page. */
    task->context->eip =
        USER_CODE_BASE;

    task->context->eflags =
        0x202;

    task->context->privilege =
        TASK_USER;

    return task;
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

    current_task->state =
        TASK_READY;

    next_task->state =
        TASK_RUNNING;

    current_task =
        next_task;
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
     * Create a user task. Its code and stack live in user-accessible
     * pages, while the kernel image remains supervisor-only.
     */
    task_t *user_test =
        task_create_user();

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
     */
    if (!user_space_prepare())
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

    print_at(
        12,
        0,
        "PAGING: KERNEL SUPERVISOR / USER PAGES READY"
    );

    paging_init();
    paging_enable();

    print_at(
        13,
        0,
        "PAGING ENABLED: PROCESS SPACE PROTECTED"
    );

    /*
     * Start the user task through the privilege-aware task switch.
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
    }

    while (1)
    {
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