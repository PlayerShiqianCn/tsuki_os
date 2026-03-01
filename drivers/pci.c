#include "pci.h"
#include "utils.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static inline void outl(unsigned short port, unsigned int value) {
    __asm__ __volatile__("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned int inl(unsigned short port) {
    unsigned int ret;
    __asm__ __volatile__("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

unsigned int pci_config_read_dword(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset) {
    unsigned int address = 0x80000000u
        | ((unsigned int)bus << 16)
        | ((unsigned int)slot << 11)
        | ((unsigned int)func << 8)
        | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

unsigned short pci_config_read_word(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset) {
    unsigned int data = pci_config_read_dword(bus, slot, func, offset);
    unsigned int shift = (offset & 2) * 8;
    return (unsigned short)((data >> shift) & 0xFFFF);
}

unsigned char pci_config_read_byte(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset) {
    unsigned int data = pci_config_read_dword(bus, slot, func, offset);
    unsigned int shift = (offset & 3) * 8;
    return (unsigned char)((data >> shift) & 0xFF);
}

void pci_config_write_dword(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset, unsigned int value) {
    unsigned int address = 0x80000000u
        | ((unsigned int)bus << 16)
        | ((unsigned int)slot << 11)
        | ((unsigned int)func << 8)
        | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, address);
    outl(PCI_CONFIG_DATA, value);
}

void pci_config_write_word(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset, unsigned short value) {
    unsigned int aligned = offset & 0xFC;
    unsigned int data = pci_config_read_dword(bus, slot, func, (unsigned char)aligned);
    unsigned int shift = (offset & 2) * 8;
    data &= ~(0xFFFFu << shift);
    data |= ((unsigned int)value << shift);
    pci_config_write_dword(bus, slot, func, (unsigned char)aligned, data);
}

void pci_config_write_byte(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset, unsigned char value) {
    unsigned int aligned = offset & 0xFC;
    unsigned int data = pci_config_read_dword(bus, slot, func, (unsigned char)aligned);
    unsigned int shift = (offset & 3) * 8;
    data &= ~(0xFFu << shift);
    data |= ((unsigned int)value << shift);
    pci_config_write_dword(bus, slot, func, (unsigned char)aligned, data);
}

int pci_probe_device(unsigned char bus, unsigned char slot, unsigned char func, PciDeviceInfo* out) {
    unsigned short vendor = pci_config_read_word(bus, slot, func, 0x00);
    if (vendor == 0xFFFF) return 0;

    if (!out) return 1;

    unsigned short device = pci_config_read_word(bus, slot, func, 0x02);
    unsigned int class_reg = pci_config_read_dword(bus, slot, func, 0x08);

    memset(out, 0, sizeof(PciDeviceInfo));
    out->bus = bus;
    out->slot = slot;
    out->func = func;
    out->vendor_id = vendor;
    out->device_id = device;
    out->revision = (unsigned char)(class_reg & 0xFF);
    out->prog_if = (unsigned char)((class_reg >> 8) & 0xFF);
    out->subclass = (unsigned char)((class_reg >> 16) & 0xFF);
    out->class_code = (unsigned char)((class_reg >> 24) & 0xFF);

    for (int i = 0; i < 6; i++) {
        out->bar[i] = pci_config_read_dword(bus, slot, func, (unsigned char)(0x10 + i * 4));
    }
    out->irq_line = pci_config_read_byte(bus, slot, func, 0x3C);
    return 1;
}

int pci_find_first_by_class(unsigned char class_code, unsigned char subclass, PciDeviceInfo* out) {
    PciDeviceInfo info;
    // 当前平台以 QEMU/单总线为主，先扫 bus 0，避免启动阶段过慢
    for (unsigned int bus = 0; bus < 1; bus++) {
        for (unsigned int slot = 0; slot < 32; slot++) {
            for (unsigned int func = 0; func < 8; func++) {
                if (!pci_probe_device((unsigned char)bus, (unsigned char)slot, (unsigned char)func, &info)) {
                    continue;
                }
                if (info.class_code != class_code) continue;
                if (subclass != 0xFF && info.subclass != subclass) continue;
                if (out) *out = info;
                return 1;
            }
        }
    }
    return 0;
}
