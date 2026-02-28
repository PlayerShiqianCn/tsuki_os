/* link.ld */
ENTRY(_start)

SECTIONS
{
    /* 内核加载地址，必须与 boot.asm 中读取的目标地址一致 */
    /* boot.asm loads to segment 0x1000, offset 0 = 0x10000 */
    . = 0x10000;

    .text : ALIGN(4K)
    {
        /* 【关键】强制将 kernel_entry.o 的代码放在最前面 */
        kernel_entry.o(.text)
        
        /* 然后才是其他所有文件的代码 */
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
