#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define PT_LOAD 1
#define PF_X 0x1
#define PF_W 0x2
#define TSK_VERSION 1u

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
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

typedef struct {
    char magic[4];             // "TSK2"
    uint32_t version;          // 格式版本
    uint32_t load_addr;        // 建议加载地址
    uint32_t entry_offset;     // 入口相对偏移
    uint32_t image_size;       // 完整内存镜像大小（包含 .bss / 对齐空洞）
    uint32_t image_checksum;   // FNV-1a 校验
    uint32_t flags;
    uint32_t reserved;
} TskHeader;

static uint32_t tsk_checksum32(const unsigned char* data, uint32_t size) {
    uint32_t hash = 2166136261u;

    if (!data && size != 0) return 0;

    for (uint32_t i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }

    return hash;
}

static int should_pack_segment(const Elf32_Phdr* ph, uint32_t entry) {
    uint32_t seg_end;

    if (!ph || ph->p_type != PT_LOAD || ph->p_memsz == 0) return 0;

    seg_end = ph->p_vaddr + ph->p_memsz;
    if ((ph->p_flags & (PF_X | PF_W)) != 0) return 1;
    return entry >= ph->p_vaddr && entry < seg_end;
}

static int read_program_headers(FILE* in, const Elf32_Ehdr* ehdr, Elf32_Phdr** out_phdrs) {
    size_t total_size;
    Elf32_Phdr* phdrs;

    if (!in || !ehdr || !out_phdrs) return 0;
    if (ehdr->e_phnum == 0 || ehdr->e_phentsize != sizeof(Elf32_Phdr)) return 0;

    total_size = (size_t)ehdr->e_phnum * sizeof(Elf32_Phdr);
    phdrs = (Elf32_Phdr*)malloc(total_size);
    if (!phdrs) return 0;

    if (fseek(in, (long)ehdr->e_phoff, SEEK_SET) != 0) {
        free(phdrs);
        return 0;
    }
    if (fread(phdrs, sizeof(Elf32_Phdr), ehdr->e_phnum, in) != ehdr->e_phnum) {
        free(phdrs);
        return 0;
    }

    *out_phdrs = phdrs;
    return 1;
}

static int build_image(FILE* in, long input_size, const Elf32_Ehdr* ehdr, const Elf32_Phdr* phdrs,
                       unsigned char** out_image, uint32_t* out_base, uint32_t* out_size,
                       uint32_t* out_entry_offset) {
    uint32_t min_vaddr = 0xFFFFFFFFu;
    uint32_t max_vaddr = 0;
    unsigned char* image;
    int found = 0;

    if (!in || !ehdr || !phdrs || !out_image || !out_base || !out_size || !out_entry_offset) return 0;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf32_Phdr* ph = &phdrs[i];
        uint32_t seg_end;

        if (!should_pack_segment(ph, ehdr->e_entry)) continue;
        if (ph->p_filesz > ph->p_memsz) return 0;
        if ((uint64_t)ph->p_offset + ph->p_filesz > (uint64_t)input_size) return 0;

        seg_end = ph->p_vaddr + ph->p_memsz;
        if (!found || ph->p_vaddr < min_vaddr) min_vaddr = ph->p_vaddr;
        if (!found || seg_end > max_vaddr) max_vaddr = seg_end;
        found = 1;
    }

    if (!found || max_vaddr <= min_vaddr) return 0;
    if (ehdr->e_entry < min_vaddr || ehdr->e_entry >= max_vaddr) return 0;

    image = (unsigned char*)malloc((size_t)(max_vaddr - min_vaddr));
    if (!image) return 0;
    memset(image, 0, (size_t)(max_vaddr - min_vaddr));

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf32_Phdr* ph = &phdrs[i];
        uint32_t offset_in_image;

        if (!should_pack_segment(ph, ehdr->e_entry) || ph->p_filesz == 0) continue;

        offset_in_image = ph->p_vaddr - min_vaddr;
        if (fseek(in, (long)ph->p_offset, SEEK_SET) != 0) {
            free(image);
            return 0;
        }
        if (fread(image + offset_in_image, 1, ph->p_filesz, in) != ph->p_filesz) {
            free(image);
            return 0;
        }
    }

    *out_image = image;
    *out_base = min_vaddr;
    *out_size = max_vaddr - min_vaddr;
    *out_entry_offset = ehdr->e_entry - min_vaddr;
    return 1;
}

int main(int argc, char* argv[]) {
    FILE* in;
    FILE* out;
    long input_size;
    Elf32_Ehdr ehdr;
    Elf32_Phdr* phdrs = 0;
    unsigned char* image = 0;
    uint32_t load_addr = 0;
    uint32_t image_size = 0;
    uint32_t entry_offset = 0;
    TskHeader hdr;

    if (argc != 3) {
        printf("Usage: %s input.elf output.tsk\n", argv[0]);
        return 1;
    }

    in = fopen(argv[1], "rb");
    if (!in) {
        perror("fopen input");
        return 1;
    }

    fseek(in, 0, SEEK_END);
    input_size = ftell(in);
    fseek(in, 0, SEEK_SET);
    if (input_size < (long)sizeof(Elf32_Ehdr)) {
        printf("Input too small: %s\n", argv[1]);
        fclose(in);
        return 1;
    }

    if (fread(&ehdr, sizeof(Elf32_Ehdr), 1, in) != 1) {
        printf("Failed to read ELF header: %s\n", argv[1]);
        fclose(in);
        return 1;
    }
    if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' ||
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F' ||
        ehdr.e_ident[4] != 1 || ehdr.e_ident[5] != 1) {
        printf("Unsupported ELF file: %s\n", argv[1]);
        fclose(in);
        return 1;
    }

    if (!read_program_headers(in, &ehdr, &phdrs)) {
        printf("Failed to read program headers: %s\n", argv[1]);
        fclose(in);
        return 1;
    }
    if (!build_image(in, input_size, &ehdr, phdrs, &image, &load_addr, &image_size, &entry_offset)) {
        printf("Failed to pack loadable image: %s\n", argv[1]);
        free(phdrs);
        fclose(in);
        return 1;
    }
    fclose(in);

    out = fopen(argv[2], "wb");
    if (!out) {
        perror("fopen output");
        free(image);
        free(phdrs);
        return 1;
    }

    memcpy(hdr.magic, "TSK2", 4);
    hdr.version = TSK_VERSION;
    hdr.load_addr = load_addr;
    hdr.entry_offset = entry_offset;
    hdr.image_size = image_size;
    hdr.image_checksum = tsk_checksum32(image, image_size);
    hdr.flags = 0;
    hdr.reserved = 0;

    fwrite(&hdr, sizeof(TskHeader), 1, out);
    fwrite(image, 1, image_size, out);

    fclose(out);
    free(image);
    free(phdrs);

    printf("Created %s from %s (load=0x%x, size=%u, entry=+0x%x)\n",
           argv[2], argv[1], load_addr, image_size, entry_offset);

    return 0;
}
