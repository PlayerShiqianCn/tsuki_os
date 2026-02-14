// ps2.c
#include "ps2.h"

#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64

static volatile char kb_buffer[32];
static volatile int kb_head = 0;
static volatile int kb_tail = 0;

static volatile ps2_mouse_event_t mouse_buffer[16];
static volatile int mouse_head = 0;
static volatile int mouse_tail = 0;

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ __volatile__ ("inb %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ __volatile__ ("outb %0, %1" : : "a" (val), "Nd" (port));
}

static void wait_data(void) {
    while ((inb(PS2_STATUS_PORT) & 1) == 0);
}

static const char scancode_ascii[128] = {
    [0x01] = 27, [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']', [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';',
    [0x28] = '\'', [0x29] = '`', [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' ',
};

static const char scancode_ascii_shift[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+', [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E',
    [0x13] = 'R', [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}', [0x1E] = 'A',
    [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G', [0x23] = 'H',
    [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = ':', [0x28] = '"',
    [0x29] = '~', [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V',
    [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<', [0x34] = '>',
    [0x35] = '?', [0x39] = ' ',
};

void ps2_init(void) {
    while (inb(PS2_STATUS_PORT) & 1) inb(PS2_DATA_PORT);
    kb_head = kb_tail = 0;
    mouse_head = mouse_tail = 0;

    // 【关键】开启 IRQ1 (键盘)
    unsigned char mask = inb(0x21); // 主片 mask 寄存器
    mask &= ~(1 << 1); // Clear bit 1 (IRQ1)
    outb(0x21, mask);
}

static int shift_pressed = 0;
static int caps_lock = 0;
static unsigned char mouse_cycle = 0;
static unsigned char mouse_packet[3];

static void push_kb_char(char c) {
    if (!c) return;
    int next = (kb_head + 1) % 32;
    if (next != kb_tail) {
        kb_buffer[kb_head] = c;
        kb_head = next;
    }
}

static void push_mouse_event(ps2_mouse_event_t ev) {
    int next = (mouse_head + 1) % 16;
    if (next != mouse_tail) {
        mouse_buffer[mouse_head] = ev;
        mouse_head = next;
    }
}

void ps2_poll_inputs_once(void) {
    unsigned char status = inb(PS2_STATUS_PORT);
    if ((status & 1) == 0) return;

    unsigned char data = inb(PS2_DATA_PORT);
    if (status & 0x20) {
        switch (mouse_cycle) {
            case 0:
                if (!(data & 0x08)) { mouse_cycle = 0; break; }
                mouse_packet[0] = data; mouse_cycle = 1; break;
            case 1:
                mouse_packet[1] = data; mouse_cycle = 2; break;
            case 2: {
                mouse_packet[2] = data; mouse_cycle = 0;
                ps2_mouse_event_t ev;
                ev.buttons = mouse_packet[0] & 0x07;
                ev.dx = (signed char)mouse_packet[1];
                ev.dy = -(signed char)mouse_packet[2]; // Y axis correction
                push_mouse_event(ev);
                break;
            }
        }
    } else {
        unsigned char sc = data;
        if (sc == 0xE0 || sc == 0xE1) return;
        int released = (sc & 0x80) != 0;
        unsigned char sc_no_rel = sc & 0x7F;

        if (!released) {
            if (sc_no_rel == 0x2A || sc_no_rel == 0x36) { shift_pressed = 1; return; }
            if (sc_no_rel == 0x3A) { caps_lock = !caps_lock; return; }
            const char *tbl = shift_pressed ? scancode_ascii_shift : scancode_ascii;
            char c = tbl[sc_no_rel];
            if (!shift_pressed && caps_lock && c >= 'a' && c <= 'z') c -= 32;
            else if (shift_pressed && caps_lock && c >= 'A' && c <= 'Z') c += 32;
            push_kb_char(c);
        } else {
            if (sc_no_rel == 0x2A || sc_no_rel == 0x36) shift_pressed = 0;
        }
    }
}

char ps2_getchar(void) {
    ps2_poll_inputs_once();
    if (kb_tail == kb_head) return 0;
    char c = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % 32;
    return c;
}

static void wait_input_empty(void) {
    while (inb(PS2_STATUS_PORT) & 2);
}

void ps2_mouse_init(void) {
    wait_input_empty(); outb(PS2_STATUS_PORT, 0xA8);
    wait_input_empty(); outb(PS2_STATUS_PORT, 0xD4);
    wait_input_empty(); outb(PS2_DATA_PORT, 0xF4);

    // 【关键】开启 IRQ12 (鼠标，从片 bit 4)
    unsigned char mask = inb(0xA1); // 从片 mask 寄存器
    mask &= ~(1 << 4); // Clear bit 4 (IRQ12)
    outb(0xA1, mask);
}

int ps2_get_mouse_event(ps2_mouse_event_t *out) {
    if (mouse_tail == mouse_head) return 0;
    *out = mouse_buffer[mouse_tail];
    mouse_tail = (mouse_tail + 1) % 16;
    return 1;
}

// 键盘中断处理函数
// 由汇编跳板调用，在中断上下文中执行
void keyboard_handler_isr(void) {
    // 读取数据以清除中断，但不处理（由轮询处理）
    inb(PS2_DATA_PORT);

    // 发送EOI给主PIC
    outb(0x20, 0x20);
}

// 鼠标中断处理函数
// IRQ12，由汇编跳板调用
void mouse_handler_isr(void) {
    // 读取数据以清除中断，但不处理（由轮询处理）
    inb(PS2_DATA_PORT);

    // 发送EOI给从PIC和主PIC
    outb(0xA0, 0x20);
    outb(0x20, 0x20);
}