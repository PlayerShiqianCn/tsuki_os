#ifndef IRQ_H
#define IRQ_H

static inline unsigned int irq_save_disable(void) {
    unsigned int flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void irq_restore(unsigned int flags) {
    __asm__ volatile ("push %0; popf" :: "r"(flags) : "memory", "cc");
}

static inline int irq_were_enabled(unsigned int flags) {
    return (flags & (1u << 9)) != 0;
}

#endif
