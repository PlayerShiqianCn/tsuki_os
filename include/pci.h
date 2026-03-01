#ifndef PCI_H
#define PCI_H

typedef struct {
    unsigned char bus;
    unsigned char slot;
    unsigned char func;
    unsigned short vendor_id;
    unsigned short device_id;
    unsigned char class_code;
    unsigned char subclass;
    unsigned char prog_if;
    unsigned char revision;
    unsigned int bar[6];
    unsigned char irq_line;
} PciDeviceInfo;

unsigned int pci_config_read_dword(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset);
unsigned short pci_config_read_word(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset);
unsigned char pci_config_read_byte(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset);
void pci_config_write_dword(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset, unsigned int value);
void pci_config_write_word(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset, unsigned short value);
void pci_config_write_byte(unsigned char bus, unsigned char slot, unsigned char func, unsigned char offset, unsigned char value);

int pci_probe_device(unsigned char bus, unsigned char slot, unsigned char func, PciDeviceInfo* out);
int pci_find_first_by_class(unsigned char class_code, unsigned char subclass, PciDeviceInfo* out);

#endif
