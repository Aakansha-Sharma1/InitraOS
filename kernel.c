#define KEYBOARD_BUFFER_SIZE 128

static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static int keyboard_index = 0;

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

    switch (scancode)
    {
        case 0x10: c = 'Q'; break;
        case 0x11: c = 'W'; break;
        case 0x12: c = 'E'; break;
        case 0x13: c = 'R'; break;
        case 0x14: c = 'T'; break;
        case 0x15: c = 'Y'; break;
        case 0x16: c = 'U'; break;
        case 0x17: c = 'I'; break;
        case 0x18: c = 'O'; break;
        case 0x19: c = 'P'; break;

        case 0x1E: c = 'A'; break;
        case 0x1F: c = 'S'; break;
        case 0x20: c = 'D'; break;
        case 0x21: c = 'F'; break;
        case 0x22: c = 'G'; break;
        case 0x23: c = 'H'; break;
        case 0x24: c = 'J'; break;
        case 0x25: c = 'K'; break;
        case 0x26: c = 'L'; break;

        case 0x2C: c = 'Z'; break;
        case 0x2D: c = 'X'; break;
        case 0x2E: c = 'C'; break;
        case 0x2F: c = 'V'; break;
        case 0x30: c = 'B'; break;
        case 0x31: c = 'N'; break;
        case 0x32: c = 'M'; break;

        case 0x39: c = ' '; break;
    }

    if (c != 0 && keyboard_index < KEYBOARD_BUFFER_SIZE - 1)
    {
        keyboard_buffer[keyboard_index] = c;
        keyboard_index++;

        int pos = (13 * 80) + keyboard_index - 1;

        vga[pos] = 0x0700 | c;
    }
}