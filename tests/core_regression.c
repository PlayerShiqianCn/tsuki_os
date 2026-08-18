#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "kernel_core.h"

static int fail(const char* name) {
    fprintf(stderr, "FAIL %s\n", name);
    return 1;
}

/* ========================================================================
 * Existing tests (preserved)
 * ======================================================================== */

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

/* ========================================================================
 * New tests: Scheduler priority configurations
 * ======================================================================== */

static int test_sched_round_robin_same_priority(void) {
    /* Verify pure round-robin within same priority level. */
    int runnable[6] = {1, 1, 1, 1, 1, 1};
    int priority[6] = {5, 5, 5, 5, 5, 5};
    int next = -1;
    int order[24];
    int i;

    for (i = 0; i < 24; i++) {
        next = sched_pick_next_index(runnable, priority, 6, next);
        if (next < 0 || next >= 6) return fail("rr_same_prio: index out of range");
        order[i] = next;
    }

    /* After the first pick (index 0), it should cycle 0,1,2,3,4,5,0,1,... */
    /* The first call with after_index=-1 starts at index 0. */
    if (order[0] != 0) return fail("rr_same_prio: first pick");
    for (i = 1; i < 24; i++) {
        if (order[i] != i % 6) return fail("rr_same_prio: cycle");
    }

    puts("PASS sched_round_robin_same_priority");
    return 0;
}

static int test_sched_higher_priority_preempts(void) {
    /* Higher priority (lower number) should always be selected. */
    int runnable[4] = {1, 1, 1, 1};
    int priority[4] = {10, 5, 10, 20};
    int next;
    int i;

    /* Index 1 has priority 5 (best), should always be picked. */
    for (i = 0; i < 10; i++) {
        next = sched_pick_next_index(runnable, priority, 4, i % 4);
        if (next != 1) return fail("sched_preempt: higher priority not selected");
    }

    /* Make index 1 non-runnable: now index 0 and 2 (priority 10) should alternate. */
    runnable[1] = 0;
    next = -1;
    for (i = 0; i < 6; i++) {
        next = sched_pick_next_index(runnable, priority, 4, next);
        /* Should pick among indices 0 and 2 (both priority 10) in round-robin. */
        if (next != 0 && next != 2) return fail("sched_preempt: wrong index after blocking 1");
    }

    /* Verify index 3 (priority 20) never picked when 0 or 2 are runnable. */
    runnable[1] = 0;
    next = -1;
    for (i = 0; i < 20; i++) {
        next = sched_pick_next_index(runnable, priority, 4, next);
        if (next == 3) return fail("sched_preempt: low priority should not be picked");
    }

    puts("PASS sched_higher_priority_preempts");
    return 0;
}

static int test_sched_all_blocked(void) {
    /* When no process is runnable, should return -1. */
    int runnable[4] = {0, 0, 0, 0};
    int priority[4] = {0, 0, 0, 0};

    if (sched_pick_next_index(runnable, priority, 4, -1) != -1)
        return fail("sched_all_blocked: should return -1");
    if (sched_pick_next_index(runnable, priority, 4, 2) != -1)
        return fail("sched_all_blocked: should return -1 with after_index");

    puts("PASS sched_all_blocked");
    return 0;
}

static int test_sched_single_runnable(void) {
    /* Only one runnable process: should always pick it. */
    int runnable[4] = {0, 1, 0, 0};
    int priority[4] = {0, -5, 0, 0};
    int i;

    for (i = 0; i < 5; i++) {
        if (sched_pick_next_index(runnable, priority, 4, i % 4) != 1)
            return fail("sched_single: should always pick index 1");
    }

    puts("PASS sched_single_runnable");
    return 0;
}

static int test_sched_priority_boundary(void) {
    /* Test with extreme priority values. */
    int runnable[3] = {1, 1, 1};
    int priority[3] = {-10, 0, 10};
    int next;

    /* -10 is the highest priority. */
    next = sched_pick_next_index(runnable, priority, 3, -1);
    if (next != 0) return fail("sched_prio_boundary: -10 not picked");

    /* Block index 0, now 0 is best. */
    runnable[0] = 0;
    next = sched_pick_next_index(runnable, priority, 3, -1);
    if (next != 1) return fail("sched_prio_boundary: 0 not picked after blocking -10");

    puts("PASS sched_priority_boundary");
    return 0;
}

/* ========================================================================
 * New tests: Slot arena edge cases
 * ======================================================================== */

static int test_slot_arena_full(void) {
    SlotArena a;
    int i;

    slot_arena_init(&a, 8);

    /* Allocate all slots. */
    for (i = 0; i < 8; i++) {
        if (slot_arena_alloc(&a, 1) != i) return fail("slot_fill");
    }

    /* Arena is full, next alloc should fail. */
    if (slot_arena_alloc(&a, 1) != -1) return fail("slot_full_reject");

    /* Free a slot and verify it can be reallocated. */
    slot_arena_free(&a, 3, 1);
    if (slot_arena_alloc(&a, 1) != 3) return fail("slot_reuse_freed");

    puts("PASS slot_arena_full");
    return 0;
}

static int test_slot_arena_multi_free(void) {
    SlotArena a;

    slot_arena_init(&a, 16);

    /* Allocate [0..4] and [5..9], leave [10..15] free. */
    if (slot_arena_alloc(&a, 5) != 0) return fail("slot_mf_first");
    if (slot_arena_alloc(&a, 5) != 5) return fail("slot_mf_second");

    /* Free [0..4] and allocate [0..3] — should reuse freed area. */
    slot_arena_free(&a, 0, 5);
    if (slot_arena_alloc(&a, 4) != 0) return fail("slot_mf_reuse");

    /* Only 1 slot left in the first region (index 4), so a 2-slot alloc
     * should go to the free area at [10..15]. */
    if (slot_arena_alloc(&a, 2) != 10) return fail("slot_mf_next_free");

    puts("PASS slot_arena_multi_free");
    return 0;
}

/* ========================================================================
 * New tests: Damage queue edge cases
 * ======================================================================== */

static int test_damage_no_overlap(void) {
    DamageQueue q;
    DamageRect out[DAMAGE_MAX_RECTS];
    int count;

    damage_init(&q, 100, 100);
    damage_add(&q, 0, 0, 10, 10);
    damage_add(&q, 50, 50, 10, 10);
    count = damage_consume(&q, out, DAMAGE_MAX_RECTS);
    if (count != 2) return fail("damage_no_overlap: count");
    if (out[0].x != 0 || out[0].y != 0 || out[0].w != 10 || out[0].h != 10)
        return fail("damage_no_overlap: first rect");
    if (out[1].x != 50 || out[1].y != 50 || out[1].w != 10 || out[1].h != 10)
        return fail("damage_no_overlap: second rect");

    puts("PASS damage_no_overlap");
    return 0;
}

static int test_damage_clipping(void) {
    DamageQueue q;
    DamageRect out[DAMAGE_MAX_RECTS];
    int count;

    damage_init(&q, 100, 100);
    /* Add a rect that extends beyond the screen bounds. */
    damage_add(&q, 90, 90, 30, 30);
    count = damage_consume(&q, out, DAMAGE_MAX_RECTS);
    if (count != 1) return fail("damage_clip: count");
    if (out[0].x != 90 || out[0].y != 90) return fail("damage_clip: origin");
    /* Should be clipped to (100-90)=10 in each dimension. */
    if (out[0].w != 10 || out[0].h != 10) return fail("damage_clip: clipped size");

    puts("PASS damage_clipping");
    return 0;
}

/* ========================================================================
 * Test runner
 * ======================================================================== */

typedef struct {
    const char* name;
    int (*func)(void);
} CoreTest;

static const CoreTest core_tests[] = {
    {"damage_queue",              test_damage},
    {"slot_arena",                test_slot_arena},
    {"scheduler_policy",          test_scheduler},
    {"nearest_scale",             test_nearest_scale},
    {"paging_math",               test_paging_math},
    {"path_identity",             test_path_identity},
    {"sched_round_robin",         test_sched_round_robin_same_priority},
    {"sched_preempt",             test_sched_higher_priority_preempts},
    {"sched_all_blocked",         test_sched_all_blocked},
    {"sched_single_runnable",     test_sched_single_runnable},
    {"sched_priority_boundary",   test_sched_priority_boundary},
    {"slot_arena_full",           test_slot_arena_full},
    {"slot_arena_multi_free",     test_slot_arena_multi_free},
    {"damage_no_overlap",         test_damage_no_overlap},
    {"damage_clipping",           test_damage_clipping},
    {NULL, NULL}
};

int main(int argc, char** argv) {
    int i;
    int passed = 0;
    int failed = 0;

    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        for (i = 0; core_tests[i].name; i++) {
            printf("%s\n", core_tests[i].name);
        }
        return 0;
    }

    if (argc == 2) {
        /* Run a single test by name. */
        for (i = 0; core_tests[i].name; i++) {
            if (strcmp(argv[1], core_tests[i].name) == 0) {
                return core_tests[i].func();
            }
        }
        fprintf(stderr, "unknown test: %s\n", argv[1]);
        return 2;
    }

    /* Run all tests. */
    for (i = 0; core_tests[i].name; i++) {
        if (core_tests[i].func() == 0) {
            passed++;
        } else {
            failed++;
        }
    }

    printf("\n%d passed, %d failed, %d total\n", passed, failed, passed + failed);
    return failed > 0 ? 1 : 0;
}
