#include <stdio.h>
#include <string.h>

#include "kernel_core.h"

static int fail(const char* name) {
    fprintf(stderr, "FAIL %s\n", name);
    return 1;
}

static int test_damage(void) {
    DamageQueue q;
    DamageRect out[DAMAGE_MAX_RECTS];
    int count;

    damage_init(&q, 320, 200);
    damage_add(&q, -5, -4, 12, 10);
    damage_add(&q, 5, 4, 8, 8);
    count = damage_consume(&q, out, DAMAGE_MAX_RECTS);
    if (count != 1 || out[0].x != 0 || out[0].y != 0 ||
        out[0].w != 13 || out[0].h != 12) return fail("damage_merge_clip");
    if (damage_consume(&q, out, DAMAGE_MAX_RECTS) != 0) return fail("damage_consume_clears");

    damage_add_full(&q);
    count = damage_consume(&q, out, DAMAGE_MAX_RECTS);
    if (count != 1 || out[0].w != 320 || out[0].h != 200) return fail("damage_full");
    puts("PASS damage_queue");
    return 0;
}

static int test_slot_arena(void) {
    SlotArena a;
    int first;

    slot_arena_init(&a, 16);
    first = slot_arena_alloc(&a, 5);
    if (first != 0) return fail("slot_first");
    if (slot_arena_alloc(&a, 4) != 5) return fail("slot_second");
    slot_arena_free(&a, first, 5);
    if (slot_arena_alloc(&a, 3) != 0) return fail("slot_reuse");
    if (slot_arena_alloc(&a, 9) != -1) return fail("slot_capacity");
    puts("PASS slot_arena");
    return 0;
}

static int test_scheduler(void) {
    int runnable[4] = {1, 1, 1, 1};
    int priority[4] = {0, 0, 0, 0};
    int next = -1;

    for (int expected = 0; expected < 8; expected++) {
        next = sched_pick_next_index(runnable, priority, 4, next);
        if (next != expected % 4) return fail("scheduler_round_robin");
    }
    priority[2] = -2;
    next = sched_pick_next_index(runnable, priority, 4, 0);
    if (next != 2) return fail("scheduler_priority");
    runnable[2] = 0;
    next = sched_pick_next_index(runnable, priority, 4, 2);
    if (next != 3) return fail("scheduler_skip_blocked");
    puts("PASS scheduler_policy");
    return 0;
}

static int test_nearest_scale(void) {
    if (scale_nearest_index(0, 7, 3) != 0) return fail("scale_first");
    if (scale_nearest_index(3, 7, 3) != 1) return fail("scale_middle");
    if (scale_nearest_index(6, 7, 3) != 2) return fail("scale_last");
    if (scale_nearest_index(-1, 7, 3) != -1) return fail("scale_bounds");
    puts("PASS nearest_scale");
    return 0;
}

static int test_paging_math(void) {
    if (app_slot_index_from_address(0x00300000u) != 0) return fail("slot_base");
    if (app_slot_index_from_address(0x00340000u) != 1) return fail("slot_second");
    if (app_slot_index_from_address(0x007fffffu) != 19) return fail("slot_last");
    if (app_slot_index_from_address(0x00800000u) != -1) return fail("slot_limit");
    if (page_count_for_bytes(1) != 1 || page_count_for_bytes(4096) != 1 ||
        page_count_for_bytes(4097) != 2) return fail("page_rounding");
    puts("PASS paging_math");
    return 0;
}

static int test_path_identity(void) {
    char out[32];
    if (!path_canonical_leaf("/system/terminal.tsk._hid_", out, sizeof(out)))
        return fail("path_parse");
    if (strcmp(out, "terminal.tsk") != 0) return fail("path_hidden_alias");
    if (!path_canonical_leaf("terminal.tsk", out, sizeof(out)) ||
        strcmp(out, "terminal.tsk") != 0) return fail("path_root_alias");
    puts("PASS path_identity");
    return 0;
}

int main(void) {
    if (test_damage()) return 1;
    if (test_slot_arena()) return 1;
    if (test_scheduler()) return 1;
    if (test_nearest_scale()) return 1;
    if (test_paging_math()) return 1;
    if (test_path_identity()) return 1;
    return 0;
}
