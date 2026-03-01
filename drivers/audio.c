#include "audio.h"
#include "pci.h"
#include "utils.h"
#include "disk.h"

static AudioDriverInfo g_audio_info;
static int g_audio_scanned = 0;

static void busy_delay(unsigned int loops) {
    for (volatile unsigned int i = 0; i < loops; i++) {
        __asm__ volatile("nop");
    }
}

void audio_init(void) {
    if (g_audio_scanned) return;
    g_audio_scanned = 1;

    PciDeviceInfo dev;
    memset(&g_audio_info, 0, sizeof(g_audio_info));

    if (pci_find_first_by_class(0x04, 0xFF, &dev)) {
        g_audio_info.present = 1;
        g_audio_info.vendor_id = dev.vendor_id;
        g_audio_info.device_id = dev.device_id;
        g_audio_info.bus = dev.bus;
        g_audio_info.slot = dev.slot;
        g_audio_info.func = dev.func;
        g_audio_info.irq_line = dev.irq_line;
    }

    // 先提供 PC Speaker 测试能力（独立于 PCI 声卡）
    g_audio_info.initialized = 1;
}

const AudioDriverInfo* audio_get_info(void) {
    return &g_audio_info;
}

void audio_beep(unsigned int hz, unsigned int ms) {
    if (hz < 37) hz = 37;
    if (hz > 20000) hz = 20000;
    if (ms == 0) ms = 80;
    if (ms > 2000) ms = 2000;

    unsigned int div = 1193180u / hz;
    if (div == 0) div = 1;

    outb(0x43, 0xB6);
    outb(0x42, (unsigned char)(div & 0xFF));
    outb(0x42, (unsigned char)((div >> 8) & 0xFF));

    unsigned char speaker = inb(0x61);
    outb(0x61, speaker | 0x03);

    busy_delay(ms * 2500);

    outb(0x61, speaker & 0xFC);
}
