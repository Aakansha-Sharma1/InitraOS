#define KEYBOARD_BUFFER_SIZE 128

extern char cpu_vendor[13];
extern char __kernel_end;

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
            current->free = 1;
            return;
        }

        current = current->next;
    }
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
    else if (keyboard_index > 0)
    {
        keyboard_row++;

        print_at(keyboard_row, 0, "Unknown command");

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
    heap_pointer = align_up_4k((unsigned int)&__kernel_end);

    shell_prompt();
}


/* ---------- Keyboard Handler ---------- */

void keyboard_handle(unsigned char scancode)
{
    volatile unsigned short *vga =
        (unsigned short *)0xB8000;

    char c = 0;

    /* Left Shift / Right Shift press */
    if (scancode == 0x2A || scancode == 0x36)
    {
        shift_pressed = 1;
        return;
    }

    /* Left Shift / Right Shift release */
    if (scancode == 0xAA || scancode == 0xB6)
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
                keyboard_row * 80 + keyboard_column;

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
            keyboard_row * 80 + keyboard_column;

        vga[pos] = 0x0700 | c;

        keyboard_column++;
    }
}