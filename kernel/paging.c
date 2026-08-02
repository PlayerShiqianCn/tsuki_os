#include "paging.h"
#include "mp.h"
#include "utils.h"
#include "irq.h"
#include "kernel_core.h"
#include "process.h"
#include "klog.h"

#define PAGE_SIZE 4096u
#define PAGE_PRESENT 0x001u
#define PAGE_WRITE 0x002u
#define PAGE_FLAGS (PAGE_PRESENT | PAGE_WRITE)
#define PAGE_MASK 0xFFFFF000u
#define PAGING_STRUCT_PAGES ((MP_PAGING_STRUCT_LIMIT - MP_PAGING_STRUCT_BASE) / PAGE_SIZE)
#define IDENTITY_INITIAL_LIMIT 0x02000000u
#define APP_SLOT_COUNT ((int)MP_APP_SLOT_COUNT)

static SlotArena structure_pages;
static SlotArena app_physical_slots;
static unsigned int* kernel_directory;
static unsigned int kernel_cr3_value;
static unsigned int current_cr3_value;
static int enabled;
static unsigned int active_directories[MP_APP_SLOT_COUNT];

static unsigned int* physical_page_ptr(unsigned int address) {
    return (unsigned int*)(address & PAGE_MASK);
}

static int structure_slot_from_address(unsigned int address) {
    if (address < MP_PAGING_STRUCT_BASE || address >= MP_PAGING_STRUCT_LIMIT) return -1;
    return (int)((address - MP_PAGING_STRUCT_BASE) / PAGE_SIZE);
}

static unsigned int allocate_structure_pages(int count) {
    int slot = slot_arena_alloc(&structure_pages, count);
    unsigned int address;
    if (slot < 0) return 0;
    address = MP_PAGING_STRUCT_BASE + (unsigned int)slot * PAGE_SIZE;
    memset((void*)address, 0, count * PAGE_SIZE);
    return address;
}

static void free_structure_pages(unsigned int address, int count) {
    int slot = structure_slot_from_address(address);
    if (slot >= 0) slot_arena_free(&structure_pages, slot, count);
}

static void register_directory(unsigned int cr3) {
    for (int i = 0; i < APP_SLOT_COUNT; i++) {
        if (!active_directories[i]) { active_directories[i] = cr3; return; }
    }
}

static void unregister_directory(unsigned int cr3) {
    for (int i = 0; i < APP_SLOT_COUNT; i++) {
        if (active_directories[i] == cr3) { active_directories[i] = 0; return; }
    }
}

static unsigned int* ensure_kernel_table(unsigned int directory_index) {
    unsigned int entry;
    unsigned int table_address;
    if (directory_index >= 1024) return 0;
    entry = kernel_directory[directory_index];
    if (entry & PAGE_PRESENT) return physical_page_ptr(entry);
    table_address = allocate_structure_pages(1);
    if (!table_address) return 0;
    kernel_directory[directory_index] = table_address | PAGE_FLAGS;
    if (directory_index >= 2) {
        for (int i = 0; i < APP_SLOT_COUNT; i++) {
            if (active_directories[i]) {
                unsigned int* directory = physical_page_ptr(active_directories[i]);
                directory[directory_index] = table_address | PAGE_FLAGS;
            }
        }
    }
    return physical_page_ptr(table_address);
}

static int map_kernel_range(unsigned int virtual_base, unsigned int physical_base,
                            unsigned int size) {
    unsigned int pages = page_count_for_bytes(size);
    unsigned int virtual_page = virtual_base & PAGE_MASK;
    unsigned int physical_page = physical_base & PAGE_MASK;
    for (unsigned int i = 0; i < pages; i++) {
        unsigned int address = virtual_page + i * PAGE_SIZE;
        unsigned int directory_index = address >> 22;
        unsigned int table_index = (address >> 12) & 0x3FFu;
        unsigned int* table = ensure_kernel_table(directory_index);
        if (!table) return 0;
        table[table_index] = (physical_page + i * PAGE_SIZE) | PAGE_FLAGS;
    }
    return 1;
}

void paging_switch(unsigned int cr3) {
    if (!cr3) return;
    current_cr3_value = cr3 & PAGE_MASK;
    __asm__ volatile("mov %0, %%cr3" :: "r"(current_cr3_value) : "memory");
}

void paging_init(void) {
    unsigned int cr0;
    unsigned int flags = irq_save_disable();

    slot_arena_init(&structure_pages, PAGING_STRUCT_PAGES);
    slot_arena_init(&app_physical_slots, MP_APP_SLOT_COUNT);
    for (int i = 0; i < APP_SLOT_COUNT; i++) active_directories[i] = 0;

    kernel_cr3_value = allocate_structure_pages(1);
    if (!kernel_cr3_value) kpanic("paging directory oom");
    kernel_directory = physical_page_ptr(kernel_cr3_value);

    if (!map_kernel_range(0, 0, IDENTITY_INITIAL_LIMIT))
        kpanic("paging identity oom");
    if (!map_kernel_range(MP_APP_PHYS_ALIAS_BASE, MP_APP_SLOT_BASE,
                          MP_APP_SLOT_LIMIT - MP_APP_SLOT_BASE))
        kpanic("paging alias oom");

    current_cr3_value = kernel_cr3_value;
    __asm__ volatile("mov %0, %%cr3" :: "r"(kernel_cr3_value) : "memory");
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
    enabled = 1;
    irq_restore(flags);
}

int paging_is_enabled(void) { return enabled; }
unsigned int paging_kernel_cr3(void) { return kernel_cr3_value; }
unsigned int paging_current_cr3(void) { return current_cr3_value; }

int paging_map_identity_range(unsigned int base, unsigned int size) {
    unsigned int flags;
    int ok;
    if (!enabled || size == 0) return 0;
    flags = irq_save_disable();
    ok = map_kernel_range(base, base, size + (base & (PAGE_SIZE - 1u)));
    if (ok) paging_switch(current_cr3_value);
    irq_restore(flags);
    return ok;
}

int paging_create_process_space(unsigned int virtual_base, unsigned int image_size,
                                PagingSpace* out) {
    unsigned int block;
    unsigned int* directory;
    unsigned int* table0;
    unsigned int* table1;
    int physical_slot;
    int virtual_slot;
    unsigned int flags;

    if (!out || image_size == 0 || image_size > MP_APP_SLOT_SIZE) return 0;
    virtual_slot = app_slot_index_from_address(virtual_base);
    if (virtual_slot < 0 || virtual_base != MP_APP_SLOT_ADDR(virtual_slot)) return 0;

    flags = irq_save_disable();
    physical_slot = slot_arena_alloc(&app_physical_slots, 1);
    if (physical_slot < 0) { irq_restore(flags); return 0; }
    block = allocate_structure_pages(3);
    if (!block) {
        slot_arena_free(&app_physical_slots, physical_slot, 1);
        irq_restore(flags);
        return 0;
    }

    directory = physical_page_ptr(block);
    table0 = physical_page_ptr(block + PAGE_SIZE);
    table1 = physical_page_ptr(block + PAGE_SIZE * 2);
    memcpy(directory, kernel_directory, PAGE_SIZE);
    memcpy(table0, physical_page_ptr(kernel_directory[0]), PAGE_SIZE);
    memcpy(table1, physical_page_ptr(kernel_directory[1]), PAGE_SIZE);

    for (unsigned int address = MP_APP_SLOT_BASE; address < MP_APP_SLOT_LIMIT; address += PAGE_SIZE) {
        unsigned int table_index = (address >> 12) & 0x3FFu;
        if ((address >> 22) == 0) table0[table_index] = 0;
        else table1[table_index] = 0;
    }
    for (unsigned int page = 0; page < page_count_for_bytes(image_size); page++) {
        unsigned int address = virtual_base + page * PAGE_SIZE;
        unsigned int physical = MP_APP_SLOT_ADDR(physical_slot) + page * PAGE_SIZE;
        unsigned int table_index = (address >> 12) & 0x3FFu;
        if ((address >> 22) == 0) table0[table_index] = physical | PAGE_FLAGS;
        else table1[table_index] = physical | PAGE_FLAGS;
    }
    directory[0] = (block + PAGE_SIZE) | PAGE_FLAGS;
    directory[1] = (block + PAGE_SIZE * 2) | PAGE_FLAGS;
    register_directory(block);
    out->cr3 = block;
    out->app_physical_slot = physical_slot;
    irq_restore(flags);
    return 1;
}

void paging_destroy_process_space(unsigned int cr3, int app_physical_slot) {
    unsigned int flags;
    if (!cr3 || cr3 == kernel_cr3_value) return;
    flags = irq_save_disable();
    unregister_directory(cr3);
    if (app_physical_slot >= 0 && app_physical_slot < APP_SLOT_COUNT)
        slot_arena_free(&app_physical_slots, app_physical_slot, 1);
    free_structure_pages(cr3, 3);
    irq_restore(flags);
}

void* paging_app_alias(int app_physical_slot) {
    if (app_physical_slot < 0 || app_physical_slot >= APP_SLOT_COUNT) return 0;
    return (void*)(MP_APP_PHYS_ALIAS_BASE + (unsigned int)app_physical_slot * MP_APP_SLOT_SIZE);
}

static void hex32(unsigned int value, char out[11]) {
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 8; i++) out[2 + i] = digits[(value >> (28 - i * 4)) & 0xF];
    out[10] = 0;
}

void paging_handle_fault(unsigned int fault_address, unsigned int error_code,
                         unsigned int instruction_pointer) {
    char text[11];
    (void)error_code;
    hex32(fault_address, text); klog_write_pair("page fault addr ", text);
    hex32(instruction_pointer, text); klog_write_pair("page fault eip ", text);
    if (current_process && current_process->pid != 0) {
        klog_write_pair("page fault task ", current_process->name);
        process_exit();
    }
    kpanic("kernel page fault");
}
