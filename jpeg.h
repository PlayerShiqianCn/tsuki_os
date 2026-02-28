#ifndef JPEG_H
#define JPEG_H

typedef struct {
    int width;
    int height;
    int components;
    int progressive;
} JpegInfo;

int jpeg_probe(const unsigned char* data, int size, JpegInfo* out_info);
int jpeg_decode_rgb(const unsigned char* data, int size, unsigned char* out_rgb, int out_capacity, JpegInfo* out_info);

#endif
