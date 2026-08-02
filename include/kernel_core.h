#ifndef KERNEL_CORE_H
#define KERNEL_CORE_H

#define DAMAGE_MAX_RECTS 16
#define SLOT_ARENA_MAX_SLOTS 1024

typedef struct { int x, y, w, h; } DamageRect;

typedef struct {
    DamageRect rects[DAMAGE_MAX_RECTS];
    int count;
    int full;
    int width;
    int height;
} DamageQueue;

typedef struct {
    unsigned int used[(SLOT_ARENA_MAX_SLOTS + 31) / 32];
    int slot_count;
} SlotArena;

void damage_init(DamageQueue* queue, int width, int height);
void damage_add(DamageQueue* queue, int x, int y, int w, int h);
void damage_add_full(DamageQueue* queue);
int damage_consume(DamageQueue* queue, DamageRect* out, int max_count);
void slot_arena_init(SlotArena* arena, int slot_count);
int slot_arena_alloc(SlotArena* arena, int count);
void slot_arena_free(SlotArena* arena, int first, int count);
int slot_arena_is_used(const SlotArena* arena, int slot);
int sched_pick_next_index(const int* runnable, const int* priorities, int count, int after_index);
int scale_nearest_index(int destination_index, int destination_size, int source_size);
int app_slot_index_from_address(unsigned int address);
unsigned int page_count_for_bytes(unsigned int bytes);
int path_canonical_leaf(const char* path, char* out, int out_size);

#endif
