#ifndef UTILS_H
#define UTILS_H

void* memcpy(void* dest, const void* src, int count);
void* memset(void* dest, int val, int count);
int strlen(const char* str);
void strcpy(char* dest, const char* src);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, int n);
#endif