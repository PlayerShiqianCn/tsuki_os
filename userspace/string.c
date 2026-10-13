// string.c — 用户态字符串/内存原语实现
// Engine: Tsuki OS userspace | i386 | C (freestanding)
// Build: 由 Makefile 统一编译为 build/userspace/string.o，
//        静态链接进每个 .tsk 应用（见各 TSK_*_ELF 链接规则）。

#include "string.h"

void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;

    if (d == s || n == 0) return dst;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        while (n--) d[n] = s[n];
    }
    return dst;
}

void* memset(void* dst, int value, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    unsigned char v = (unsigned char)value;
    while (n--) *d++ = v;
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    while (n--) {
        if (*x != *y) return (int)*x - (int)*y;
        x++;
        y++;
    }
    return 0;
}

size_t strlen(const char* s) {
    unsigned int n = 0;
    while (s && s[n]) n++;
    return n;
}

int strcmp(const char* a, const char* b) {
    unsigned int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int)((unsigned char)a[i]) - (int)((unsigned char)b[i]);
        i++;
    }
    return (int)((unsigned char)a[i]) - (int)((unsigned char)b[i]);
}

int strncmp(const char* a, const char* b, size_t n) {
    unsigned int i;
    for (i = 0; i < n; i++) {
        unsigned char ac = (unsigned char)a[i];
        unsigned char bc = (unsigned char)b[i];
        if (ac != bc) return (int)ac - (int)bc;
        if (ac == 0) return 0;
    }
    return 0;
}

char* strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++) != '\0') {}
    return dst;
}

char* strncpy(char* dst, const char* src, size_t n) {
    unsigned int i = 0;
    while (i < n && src[i]) {
        dst[i] = src[i];
        i++;
    }
    while (i < n) dst[i++] = '\0';
    return dst;
}

char* strcat(char* dst, const char* src) {
    char* d = dst;
    while (*d) d++;
    while ((*d++ = *src++) != '\0') {}
    return dst;
}

char* strncat(char* dst, const char* src, size_t n) {
    char* d = dst;
    unsigned int i = 0;
    while (*d) d++;
    while (i < n && src[i]) {
        d[i] = src[i];
        i++;
    }
    d[i] = '\0';
    return dst;
}

char* strchr(const char* s, int c) {
    char target = (char)c;
    for (; *s; s++) {
        if (*s == target) return (char*)s;
    }
    return target == '\0' ? (char*)s : 0;
}

char* strrchr(const char* s, int c) {
    char target = (char)c;
    const char* last = 0;
    for (; *s; s++) {
        if (*s == target) last = s;
    }
    if (target == '\0') return (char*)s;
    return (char*)last;
}

size_t strlcpy(char* dst, const char* src, size_t max) {
    size_t slen = strlen(src);
    size_t i = 0;

    if (max == 0) return slen;
    while (i + 1 < max && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return slen;
}

size_t strlcat(char* dst, const char* src, size_t max) {
    size_t dlen = strlen(dst);
    size_t slen = strlen(src);
    size_t room;
    size_t i = 0;

    if (dlen >= max) return slen + max;
    room = max - dlen - 1;
    while (i < room && src[i]) {
        dst[dlen + i] = src[i];
        i++;
    }
    dst[dlen + i] = '\0';
    return dlen + slen;
}

char* strstr(const char* haystack, const char* needle) {
    unsigned int n = 0;

    if (!haystack || !needle) return (char*)haystack;
    if (needle[0] == '\0') return (char*)haystack;
    for (n = 0; haystack[n]; n++) {
        unsigned int j = 0;
        while (needle[j] && haystack[n + j] == needle[j]) j++;
        if (needle[j] == '\0') return (char*)(haystack + n);
    }
    return 0;
}

int str_ends_with(const char* s, const char* suffix) {
    unsigned int ls = strlen(s);
    unsigned int lf = strlen(suffix);
    if (ls < lf) return 0;
    return strcmp(s + (ls - lf), suffix) == 0;
}
