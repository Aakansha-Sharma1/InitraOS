#define KEYBOARD_BUFFER_SIZE 128

static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static int keyboard_index = 0;

static int keyboard_column = 0;
static int keyboard_row = 13;

static int shift_pressed = 0;


void kernel_main(void)
{
    volatile unsigned short *vga = (unsigned short *)0xB8000;

    int pos = 11 * 80;

    vga[pos + 0]  = 0x0743;   // C
    vga[pos + 1]  = 0x0720;   // space
    vga[pos + 2]  = 0x074B;   // K
    vga[pos + 3]  = 0x0745;   // E
    vga[pos + 4]  = 0x0752;   // R
    vga[pos + 5]  = 0x074E;   // N
    vga[pos + 6]  = 0x0745;   // E
    vga[pos + 7]  = 0x074C;   // L
    vga[pos + 8]  = 0x0720;   // space
    vga[pos + 9]  = 0x0741;   // A
    vga[pos + 10] = 0x0743;   // C
    vga[pos + 11] = 0x0754;   // T
    vga[pos + 12] = 0x0749;   // I
    vga[pos + 13] = 0x0756;   // V
    vga[pos + 14] = 0x0745;   // E
}


void keyboard_handle(unsigned char scancode)
{
    volatile unsigned short *vga = (unsigned short *)0xB8000;

    char c = 0;


    /*
     * -----------------------------------------
     * Shift key
     * -----------------------------------------
     *
     * Left Shift  = 0x2A
     * Right Shift = 0x36
     *
     * Key release adds 0x80:
     *
     * Left Shift release  = 0xAA
     * Right Shift release = 0xB6
     */

    if (scancode == 0x2A || scancode == 0x36)
    {
        shift_pressed = 1;
        return;
    }

    if (scancode == 0xAA || scancode == 0xB6)
    {
        shift_pressed = 0;
        return;
    }


    /*
     * -----------------------------------------
     * Backspace
     * -----------------------------------------
     */

    if (scancode == 0x0E)
    {
        if (keyboard_column > 0 && keyboard_index > 0)
        {
            keyboard_column--;
            keyboard_index--;

            int pos = keyboard_row * 80 + keyboard_column;

            keyboard_buffer[keyboard_index] = 0;

            vga[pos] = 0x0720;
        }

        return;
    }


    /*
     * -----------------------------------------
     * Enter
     * -----------------------------------------
     */

    if (scancode == 0x1C)
    {
        keyboard_row++;
        keyboard_column = 0;

        /*
         * Input starts from row 13.
         * Keep it inside the 25-row VGA screen.
         */
        if (keyboard_row >= 25)
        {
            keyboard_row = 13;
        }

        return;
    }


    /*
     * -----------------------------------------
     * Convert keyboard scancode to lowercase
     * characters.
     * -----------------------------------------
     */

    switch (scancode)
    {
        /* QWERTY row */

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

        /* ASDF row */

        case 0x1E: c = 'a'; break;
        case 0x1F: c = 's'; break;
        case 0x20: c = 'd'; break;
        case 0x21: c = 'f'; break;
        case 0x22: c = 'g'; break;
        case 0x23: c = 'h'; break;
        case 0x24: c = 'j'; break;
        case 0x25: c = 'k'; break;
        case 0x26: c = 'l'; break;

        /* ZXCV row */

        case 0x2C: c = 'z'; break;
        case 0x2D: c = 'x'; break;
        case 0x2E: c = 'c'; break;
        case 0x2F: c = 'v'; break;
        case 0x30: c = 'b'; break;
        case 0x31: c = 'n'; break;
        case 0x32: c = 'm'; break;

        /* Space */

        case 0x39: c = ' '; break;
    }


    /*
     * -----------------------------------------
     * Apply Shift
     * -----------------------------------------
     */

    if (shift_pressed && c >= 'a' && c <= 'z')
    {
        c = c - 'a' + 'A';
    }


    /*
     * -----------------------------------------
     * Store character and display it
     * -----------------------------------------
     */

    if (c != 0 &&
        keyboard_index < KEYBOARD_BUFFER_SIZE - 1 &&
        keyboard_column < 80)
    {
        keyboard_buffer[keyboard_index] = c;
        keyboard_index++;

        int pos = keyboard_row * 80 + keyboard_column;

        vga[pos] = 0x0700 | c;

        keyboard_column++;
    }
}