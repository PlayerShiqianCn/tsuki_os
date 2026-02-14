#ifndef AUDIO_H
#define AUDIO_H

typedef struct {
    int present;
    int initialized;
    unsigned short vendor_id;
    unsigned short device_id;
    unsigned char bus;
    unsigned char slot;
    unsigned char func;
    unsigned char irq_line;
} AudioDriverInfo;

void audio_init(void);
const AudioDriverInfo* audio_get_info(void);
void audio_beep(unsigned int hz, unsigned int ms);

#endif
