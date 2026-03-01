#include "klog.h"
#include "mp.h"
#include "video.h"
#include "utils.h"

#define KLOG_MAX_LINES 24
#define KLOG_LINE_LEN  40
#define KLOG_MAGIC     0x4B4C4F47u

typedef struct {
    unsigned int magic;
    int count;
    int panic_active;
    char lines[KLOG_MAX_LINES][KLOG_LINE_LEN + 1];
} KlogState;

static KlogState* klog_state(void) {
    return (KlogState*)MP_KLOG_BASE;
}

static KlogState* ensure_state(void) {
    KlogState* state = klog_state();
    if (state->magic != KLOG_MAGIC) {
        memset(state, 0, sizeof(KlogState));
        state->magic = KLOG_MAGIC;
    }
    return state;
}

static void copy_line(char* dst, const char* src) {
    int i = 0;
    if (!dst) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (i < KLOG_LINE_LEN && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void append_line(char* dst, const char* src) {
    int i = strlen(dst);
    int j = 0;
    if (!dst || !src) return;
    while (i < KLOG_LINE_LEN && src[j]) {
        dst[i++] = src[j++];
    }
    dst[i] = '\0';
}

static void push_line(const char* msg) {
    KlogState* state = ensure_state();
    if (!msg) msg = "(null)";

    if (state->count < KLOG_MAX_LINES) {
        copy_line(state->lines[state->count], msg);
        state->count++;
        return;
    }

    for (int i = 1; i < KLOG_MAX_LINES; i++) {
        copy_line(state->lines[i - 1], state->lines[i]);
    }
    copy_line(state->lines[KLOG_MAX_LINES - 1], msg);
}

static void render_panic_screen(const char* msg) {
    KlogState* state = ensure_state();
    int start = 0;
    int y = 56;

    draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, C_BLACK);
    draw_rect(0, 0, SCREEN_WIDTH, 22, C_RED);
    draw_string(8, 7, "KERNEL PANIC", C_WHITE);

    draw_string(8, 30, "Reason:", C_LIGHT_RED);
    draw_string(72, 30, (char*)(msg ? msg : "panic"), C_YELLOW);
    draw_string(8, 44, "Recent kernel logs:", C_LIGHT_GRAY);

    if (state->count > 16) start = state->count - 16;
    for (int i = start; i < state->count && y <= SCREEN_HEIGHT - 10; i++) {
        draw_string(8, y, state->lines[i], C_WHITE);
        y += 8;
    }

    if (state->count == 0) {
        draw_string(8, 56, "(no logs)", C_WHITE);
    }

    video_swap_buffer();
}

void klog_init(void) {
    KlogState* state = klog_state();
    memset(state, 0, sizeof(KlogState));
    state->magic = KLOG_MAGIC;
}

void klog_write(const char* msg) {
    push_line(msg);
}

void klog_write_pair(const char* prefix, const char* value) {
    char line[KLOG_LINE_LEN + 1];
    line[0] = '\0';
    if (prefix) append_line(line, prefix);
    if (value) append_line(line, value);
    push_line(line);
}

int kpanic_is_active(void) {
    return ensure_state()->panic_active;
}

void kpanic(const char* msg) {
    KlogState* state = ensure_state();
    __asm__ volatile("cli");

    if (!state->panic_active) {
        state->panic_active = 1;
        push_line("panic");
        if (msg) push_line(msg);
        render_panic_screen(msg);
    }

    while (1) {
        __asm__ volatile("hlt");
    }
}

void kpanic_exception(void) {
    kpanic("CPU exception");
}
