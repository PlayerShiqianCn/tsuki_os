#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    char magic[4];      // "TSK"
    unsigned int entry; // 程序入口地址
    unsigned int size;  // 程序大小
    unsigned int reserved[1];
} TskHeader;

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s input.elf output.tsk\n", argv[0]);
        return 1;
    }

    const char* input = argv[1];
    const char* output = argv[2];

    FILE* in = fopen(input, "rb");
    if (!in) {
        perror("fopen input");
        return 1;
    }

    fseek(in, 0, SEEK_END);
    size_t size = ftell(in);
    fseek(in, 0, SEEK_SET);

    // 读取ELF头
    Elf32_Ehdr ehdr;
    fread(&ehdr, sizeof(Elf32_Ehdr), 1, in);

    // ELF入口点 (相对于文件开始的偏移，但tsk_load会重定位)
    // tsk_load分配内存从program_data开始，entry是e_entry - 虚拟地址 + program_data
    // 但简化，假设代码从0开始，entry = e_entry - 0x8048000 (ld默认虚拟地址)
    unsigned int entry = ehdr.e_entry - 0x8048000;

    // 读取剩余数据
    unsigned char* data = malloc(size - sizeof(Elf32_Ehdr));
    fread(data, 1, size - sizeof(Elf32_Ehdr), in);
    fclose(in);

    size -= sizeof(Elf32_Ehdr); // 去除头

    FILE* out = fopen(output, "wb");
    if (!out) {
        perror("fopen output");
        free(data);
        return 1;
    }

    TskHeader hdr;
    strcpy(hdr.magic, "TSK");
    hdr.entry = entry;
    hdr.size = size;
    hdr.reserved[0] = 0;

    fwrite(&hdr, sizeof(TskHeader), 1, out);
    fwrite(data, 1, size, out);

    fclose(out);
    free(data);

    printf("Created %s from %s (size=%zu, entry=0x%x)\n", output, input, size, entry);

    return 0;
}