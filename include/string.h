#ifndef STRING_H
#define STRING_H

/*
 * 用户态字符串/内存原语（freestanding，无宿主 libc）。
 *
 * 标准名与 ISO C 对齐：gcc 在 freestanding 下仍可能为结构体拷贝/清零
 * 隐式生成 memcpy/memset 调用，-nostdlib 链接时由本模块提供定义。
 *
 * strlcpy/strlcat 是 BSD 风格的有界接口，替代各应用手搓的
 * s_copy/s_append：保证 NUL 结尾、绝不越界，max 为目标缓冲区总大小。
 */

/* 内存 */
void* memcpy(void* dst, const void* src, unsigned int n);
void* memmove(void* dst, const void* src, unsigned int n);
void* memset(void* dst, int value, unsigned int n);
int   memcmp(const void* a, const void* b, unsigned int n);

/* 字符串 */
unsigned int strlen(const char* s);
int   strcmp(const char* a, const char* b);
int   strncmp(const char* a, const char* b, unsigned int n);
char* strcpy(char* dst, const char* src);
char* strncpy(char* dst, const char* src, unsigned int n);
char* strcat(char* dst, const char* src);
char* strncat(char* dst, const char* src, unsigned int n);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);

/* 有界接口（返回值仅作诊断，调用方可忽略） */
unsigned int strlcpy(char* dst, const char* src, unsigned int max);
unsigned int strlcat(char* dst, const char* src, unsigned int max);
int str_ends_with(const char* s, const char* suffix);

#endif
