#include "utils.h"

void* memcpy(void* dest, const void* src, int count) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    while (count--) *d++ = *s++;
    return dest;
}

void* memset(void* dest, int val, int count) {
    char* d = (char*)dest;
    while(count--) *d++ = (char)val;
    return dest;
}

int strlen(const char* str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

void strcpy(char* dest, const char* src) {
    while ((*dest++ = *src++));
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}
int strncmp(const char* s1, const char* s2, int n) {
    while (n-- && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    if (n < 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}