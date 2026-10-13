#ifndef STRING_H
#define STRING_H

/*
 * 用户态字符串/内存原语（freestanding，无宿主 libc）。
 *
 * 签名使用 size_t（gcc 自带 <stddef.h>，freestanding 下同样可用）：
 * i386 上 size_t 即 unsigned int，与 OS 构建 ABI 一致；托管回归测试
 * （-Iinclude，64 位主机）下与编译器内建签名对齐，不产生冲突。
 *
 * strlcpy/strlcat 是 BSD 风格的有界接口，替代各应用手搓的
 * s_copy/s_append：保证 NUL 结尾、绝不越界，max 为目标缓冲区总大小。
 */

#include <stddef.h>

/* 内存 */
void* memcpy(void* dst, const void* src, size_t n);
void* memmove(void* dst, const void* src, size_t n);
void* memset(void* dst, int value, size_t n);
int   memcmp(const void* a, const void* b, size_t n);

/* 字符串 */
size_t strlen(const char* s);
int   strcmp(const char* a, const char* b);
int   strncmp(const char* a, const char* b, size_t n);
char* strcpy(char* dst, const char* src);
char* strncpy(char* dst, const char* src, size_t n);
char* strcat(char* dst, const char* src);
char* strncat(char* dst, const char* src, size_t n);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
char* strstr(const char* haystack, const char* needle);

/* 有界接口（返回值仅作诊断，调用方可忽略） */
size_t strlcpy(char* dst, const char* src, size_t max);
size_t strlcat(char* dst, const char* src, size_t max);
int str_ends_with(const char* s, const char* suffix);

#endif
