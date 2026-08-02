#include "kernel_core.h"
#include "mp.h"

static int rects_touch(const DamageRect* a, const DamageRect* b) {
    return a->x <= b->x + b->w && b->x <= a->x + a->w &&
           a->y <= b->y + b->h && b->y <= a->y + a->h;
}

static DamageRect rect_union(const DamageRect* a, const DamageRect* b) {
    DamageRect out;
    int right_a = a->x + a->w;
    int right_b = b->x + b->w;
    int bottom_a = a->y + a->h;
    int bottom_b = b->y + b->h;
    out.x = a->x < b->x ? a->x : b->x;
    out.y = a->y < b->y ? a->y : b->y;
    out.w = (right_a > right_b ? right_a : right_b) - out.x;
    out.h = (bottom_a > bottom_b ? bottom_a : bottom_b) - out.y;
    return out;
}

void damage_init(DamageQueue* q, int width, int height) {
    if (!q) return;
    q->count = 0;
    q->full = 0;
    q->width = width > 0 ? width : 0;
    q->height = height > 0 ? height : 0;
}

void damage_add_full(DamageQueue* q) {
    if (!q) return;
    q->count = 0;
    q->full = 1;
}

void damage_add(DamageQueue* q, int x, int y, int w, int h) {
    DamageRect rect;
    int x1;
    int y1;
    int merged;

    if (!q || q->full || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= q->width || y >= q->height || w <= 0 || h <= 0) return;
    x1 = x + w;
    y1 = y + h;
    if (x1 < x || x1 > q->width) x1 = q->width;
    if (y1 < y || y1 > q->height) y1 = q->height;
    if (x1 <= x || y1 <= y) return;
    rect.x = x;
    rect.y = y;
    rect.w = x1 - x;
    rect.h = y1 - y;

    do {
        merged = 0;
        for (int i = 0; i < q->count; i++) {
            if (!rects_touch(&rect, &q->rects[i])) continue;
            rect = rect_union(&rect, &q->rects[i]);
            for (int j = i; j < q->count - 1; j++) q->rects[j] = q->rects[j + 1];
            q->count--;
            merged = 1;
            break;
        }
    } while (merged);

    if (q->count >= DAMAGE_MAX_RECTS) {
        damage_add_full(q);
        return;
    }
    q->rects[q->count++] = rect;
}

int damage_consume(DamageQueue* q, DamageRect* out, int max_count) {
    int count;
    if (!q || !out || max_count <= 0) return 0;
    if (q->full) {
        out[0].x = 0;
        out[0].y = 0;
        out[0].w = q->width;
        out[0].h = q->height;
        q->full = 0;
        q->count = 0;
        return 1;
    }
    count = q->count < max_count ? q->count : max_count;
    for (int i = 0; i < count; i++) out[i] = q->rects[i];
    if (count == q->count) {
        q->count = 0;
    } else {
        for (int i = count; i < q->count; i++) q->rects[i - count] = q->rects[i];
        q->count -= count;
    }
    return count;
}

void slot_arena_init(SlotArena* arena, int slot_count) {
    if (!arena) return;
    if (slot_count < 0) slot_count = 0;
    if (slot_count > SLOT_ARENA_MAX_SLOTS) slot_count = SLOT_ARENA_MAX_SLOTS;
    arena->slot_count = slot_count;
    for (int i = 0; i < (SLOT_ARENA_MAX_SLOTS + 31) / 32; i++) arena->used[i] = 0;
}

int slot_arena_is_used(const SlotArena* arena, int slot) {
    if (!arena || slot < 0 || slot >= arena->slot_count) return 0;
    return (arena->used[slot / 32] >> (slot % 32)) & 1u;
}

static void slot_set(SlotArena* arena, int slot, int used) {
    unsigned int mask = 1u << (slot % 32);
    if (used) arena->used[slot / 32] |= mask;
    else arena->used[slot / 32] &= ~mask;
}

int slot_arena_alloc(SlotArena* arena, int count) {
    if (!arena || count <= 0 || count > arena->slot_count) return -1;
    for (int first = 0; first <= arena->slot_count - count; first++) {
        int free_run = 1;
        for (int i = 0; i < count; i++) {
            if (slot_arena_is_used(arena, first + i)) {
                free_run = 0;
                first += i;
                break;
            }
        }
        if (!free_run) continue;
        for (int i = 0; i < count; i++) slot_set(arena, first + i, 1);
        return first;
    }
    return -1;
}

void slot_arena_free(SlotArena* arena, int first, int count) {
    if (!arena || first < 0 || count <= 0 || first + count > arena->slot_count) return;
    for (int i = 0; i < count; i++) slot_set(arena, first + i, 0);
}

int sched_pick_next_index(const int* runnable, const int* priorities,
                          int count, int after_index) {
    int best_priority = 0x7fffffff;
    int start;
    if (!runnable || !priorities || count <= 0) return -1;
    for (int i = 0; i < count; i++) {
        if (runnable[i] && priorities[i] < best_priority) best_priority = priorities[i];
    }
    if (best_priority == 0x7fffffff) return -1;
    start = after_index >= 0 && after_index < count ? (after_index + 1) % count : 0;
    for (int offset = 0; offset < count; offset++) {
        int i = (start + offset) % count;
        if (runnable[i] && priorities[i] == best_priority) return i;
    }
    return -1;
}

int path_canonical_leaf(const char* path, char* out, int out_size) {
    static const char suffix[] = "._hid_";
    const char* leaf;
    int length = 0;
    int suffix_len = (int)sizeof(suffix) - 1;
    if (!path || !out || out_size <= 0) return 0;
    while (*path == '/') path++;
    if (!*path) { out[0] = 0; return 0; }
    leaf = path;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/' || path[i] == '\\') leaf = path + i + 1;
    }
    while (leaf[length]) length++;
    if (length >= suffix_len) {
        int matches = 1;
        for (int i = 0; i < suffix_len; i++) {
            if (leaf[length - suffix_len + i] != suffix[i]) { matches = 0; break; }
        }
        if (matches) length -= suffix_len;
    }
    if (length <= 0 || length >= out_size) { out[0] = 0; return 0; }
    for (int i = 0; i < length; i++) out[i] = leaf[i];
    out[length] = 0;
    return 1;
}

int scale_nearest_index(int destination_index, int destination_size, int source_size) {
    if (destination_index < 0 || destination_size <= 0 || source_size <= 0 ||
        destination_index >= destination_size) return -1;
    return (destination_index * source_size) / destination_size;
}

int app_slot_index_from_address(unsigned int address) {
    if (address < MP_APP_SLOT_BASE || address >= MP_APP_SLOT_LIMIT) return -1;
    return (int)((address - MP_APP_SLOT_BASE) / MP_APP_SLOT_SIZE);
}

unsigned int page_count_for_bytes(unsigned int bytes) {
    if (bytes == 0) return 0;
    return (bytes - 1u) / 4096u + 1u;
}
