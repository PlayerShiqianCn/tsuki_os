/* link.ld */
ENTRY(_start)

SECTIONS
{
    /* 内核加载地址，必须与 boot.asm 中读取的目标地址一致 */
    /* boot.asm loads to segment 0x1000, offset 0 = 0x10000 */
    . = 0x10000;

    .text : ALIGN(4K)
    {
        /* 启动入口的先后顺序由链接命令里的对象顺序控制 */
        *(.text)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata)
    }

    .data : ALIGN(4K)
    {
        *(.data)
    }

    .bss : ALIGN(4K)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss)
        __bss_end = .;
    }
}

ASSERT(__bss_end < 0x9F000, "Kernel BSS overlaps boot stack")
