#define KEYBOARD_BUFFER_SIZE 128

extern char cpu_vendor[13];
extern char __kernel_end;
extern void c_print_string(const char *message);

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
        if (current->free == 1 && current->size >= size)
        {
            if (current->size >= size + sizeof(heap_block_t) + 1)
            {
                heap_block_t *new_block =
                    (heap_block_t *)((unsigned int)(current + 1) + size);

                new_block->size =
                    current->size - size - sizeof(heap_block_t);

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
        size > (heap_limit - heap_pointer) - sizeof(heap_block_t))
    {
        return 0;
    }

    heap_block_t *block = (heap_block_t *)heap_pointer;

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

    unsigned int address = heap_pointer;

    heap_pointer += size;

    return (void *)address;
}

static void heap_free(void *address)
{
    if (address == 0)
    {
        return;
    }

    heap_block_t *current = heap_first_block;

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
            if (current->next != 0 && current->next->free == 1)
            {
                current->size +=
                    sizeof(heap_block_t) + current->next->size;

                current->next = current->next->next;
            }

            /* Find the previous block */
            heap_block_t *previous = heap_first_block;

            while (previous != 0 && previous->next != current)
            {
                previous = previous->next;
            }

            /* Merge with the previous block if it is also free */
            if (previous != 0 && previous->free == 1)
            {
                previous->size +=
                    sizeof(heap_block_t) + current->size;

                previous->next = current->next;
            }

            return;
        }

        current = current->next;
    }
}

static void *heap_realloc(void *address, unsigned int size)
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

    heap_block_t *current = heap_first_block;

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
            void *new_address = heap_alloc(size);

            if (new_address == 0)
            {
                return 0;
            }

            /* Copy the old contents */
            unsigned char *source =
                (unsigned char *)address;

            unsigned char *destination =
                (unsigned char *)new_address;

            for (unsigned int i = 0; i < current->size; i++)
            {
                destination[i] = source[i];
            }

            heap_free(address);

            return new_address;
        }

        current = current->next;
    }

    return 0;
}

/* ---------- Tasks ---------- */

/* ---------- Tasks ---------- */

#define TASK_READY    0
#define TASK_RUNNING  1
#define TASK_BLOCKED  2
#define TASK_FINISHED 3

typedef struct task
{
    unsigned int id;
    unsigned int state;
    unsigned int esp;
    unsigned int ebp;
    struct task *next;
} task_t;

typedef struct task_context
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
} task_context_t;

extern void task_switch(task_context_t *old_context,
                        task_context_t *new_context);

static unsigned int next_task_id = 1;

static task_t *current_task = 0;
static task_t *task_list = 0;

static task_context_t kernel_context;
static task_context_t test_context;

static task_t *task_create(void)
{
    task_t *task =
        (task_t *)heap_alloc(sizeof(task_t));

    if (task == 0)
    {
        return 0;
    }

    task->id = next_task_id++;
    task->state = TASK_READY;
    task->esp = 0;
    task->ebp = 0;
    task->next = 0;

    /*
     * Add the new task to the scheduler list.
     */
    if (task_list == 0)
    {
        task_list = task;
    }
    else
    {
        task_t *current = task_list;

        while (current->next != 0)
        {
            current = current->next;
        }

        current->next = task;
    }

    return task;
}

static void task_set_state(task_t *task, unsigned int state)
{
    if (task == 0)
    {
        return;
    }

    task->state = state;
}

/*
 * Select the next READY task using round-robin order.
 */
static task_t *task_schedule_next(void)
{
    if (task_list == 0)
    {
        return 0;
    }

    task_t *start = task_list;

    if (current_task != 0 &&
        current_task->next != 0)
    {
        start = current_task->next;
    }

    task_t *task = start;

    do
    {
        if (task->state == TASK_READY)
        {
            return task;
        }

        task = task->next;

        if (task == 0)
        {
            task = task_list;
        }

    } while (task != start);

    return 0;
}

/* ---------- Task Test ---------- */

static unsigned char task_test_stack[4096];

static void task_exit(void)
{
    if (current_task != 0)
    {
        task_set_state(current_task, TASK_FINISHED);
    }

    task_switch(&test_context, &kernel_context);

    while (1)
    {
        __asm__ volatile ("hlt");
    }
}

static void task_test_function(void)
{
    volatile unsigned short *vga =
        (volatile unsigned short *)0xB8700;

    const char *message = "TASK SWITCH WORKED!";

    for (unsigned int i = 0; message[i] != 0; i++)
    {
        vga[i] = 0x0700 | message[i];
    }

    task_exit();
}

/* ---------- Keyboard ---------- */

static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static int keyboard_index = 0;

static int keyboard_column = 0;
static int keyboard_row = 13;

static int shift_pressed = 0;

/* ---------- VGA Output ---------- */

static void print_at(int row, int column, const char *text)
{
    volatile unsigned short *vga =
        (unsigned short *)0xB8000;

    while (*text != 0 && column < 80)
    {
        int pos = row * 80 + column;

        vga[pos] = 0x0700 | *text;

        column++;
        text++;
    }
}

/* ---------- Clear Screen ---------- */

static void clear_screen(void)
{
    volatile unsigned short *vga =
        (unsigned short *)0xB8000;

    for (int i = 0; i < 80 * 25; i++)
    {
        vga[i] = 0x0720;
    }
}

/* ---------- Command Comparison ---------- */

static int command_equals(const char *command)
{
    int i = 0;

    while (command[i] != 0)
    {
        if (keyboard_buffer[i] != command[i])
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
    print_at(keyboard_row, 0, "InitraOS> ");

    keyboard_column = 10;
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

        print_at(keyboard_row, 0, "CPU: ");
        print_at(keyboard_row, 5, cpu_vendor);

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

        print_at(keyboard_row, 0, "Commands:");

        keyboard_row++;
        print_at(keyboard_row, 0, "help");

        keyboard_row++;
        print_at(keyboard_row, 0, "clear");

        keyboard_row++;
        print_at(keyboard_row, 0, "cpu");

        keyboard_row++;
        print_at(keyboard_row, 0, "about");

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
        __asm__ volatile ("sti");

    heap_pointer =
        align_up_4k((unsigned int)&__kernel_end);

    /*
     * Create the first task.
     */
   current_task = task_create();

if (current_task == 0)
{
    shell_prompt();
    return;
}

task_t *next_task = task_schedule_next();

if (next_task != 0)
{
    current_task = next_task;
    current_task->state = TASK_RUNNING;
}

    /*
     * Build the initial task context.
     *
     * The stack grows downward. The top contains task_exit()
     * as the return address for task_test_function().
     */
    unsigned int stack_top =
        (unsigned int)(
            task_test_stack +
            sizeof(task_test_stack)
        );

    stack_top &= ~0x0F;

    stack_top -= sizeof(unsigned int);

    *(unsigned int *)stack_top =
        (unsigned int)task_exit;

    test_context.eax = 0;
    test_context.ebx = 0;
    test_context.ecx = 0;
    test_context.edx = 0;
    test_context.esi = 0;
    test_context.edi = 0;
    test_context.ebp = 0;

    test_context.esp = stack_top;
    test_context.eip = (unsigned int)task_test_function;

    /*
     * Interrupts enabled for the task.
     */
    test_context.eflags = 0x202;

    /*
     * Switch into the first task.
     */
    task_switch(
        &kernel_context,
        &test_context
    );

    /*
     * Execution resumes here after task_exit()
     * switches back to kernel_context.
     */
    current_task = 0;

    print_at(
        22,
        0,
        "Task finished. Kernel resumed."
    );

    keyboard_row = 13;
    keyboard_index = 0;

    shell_prompt();

    /*
     * Keep the kernel alive while interrupts continue.
     */
    while (1)
    {
        __asm__ volatile ("hlt");
    }
}

/* ---------- Keyboard Handler ---------- */

void keyboard_handle(unsigned char scancode)
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

            keyboard_buffer[keyboard_index] = 0;

            int pos =
                keyboard_row * 80 +
                keyboard_column;

            vga[pos] = 0x0720;
        }

        return;
    }

    /* Enter */
    if (scancode == 0x1C)
    {
        keyboard_buffer[keyboard_index] = 0;

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
        c = c - 'a' + 'A';
    }

    /* Add character to keyboard buffer */
    if (c != 0 &&
        keyboard_index < KEYBOARD_BUFFER_SIZE - 1 &&
        keyboard_column < 80)
    {
        keyboard_buffer[keyboard_index] = c;

        keyboard_index++;

        int pos =
            keyboard_row * 80 +
            keyboard_column;

        vga[pos] = 0x0700 | c;

        keyboard_column++;
    }
}