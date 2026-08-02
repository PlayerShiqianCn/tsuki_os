#ifndef PAGING_H
#define PAGING_H

typedef struct {
    unsigned int cr3;
    int app_physical_slot;
} PagingSpace;

void paging_init(void);
int paging_is_enabled(void);
unsigned int paging_kernel_cr3(void);
unsigned int paging_current_cr3(void);
void paging_switch(unsigned int cr3);
int paging_map_identity_range(unsigned int base, unsigned int size);
int paging_create_process_space(unsigned int virtual_base, unsigned int image_size,
                                PagingSpace* out);
void paging_destroy_process_space(unsigned int cr3, int app_physical_slot);
void* paging_app_alias(int app_physical_slot);
void paging_handle_fault(unsigned int fault_address, unsigned int error_code,
                         unsigned int instruction_pointer);

#endif
