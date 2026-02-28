#ifndef KLOG_H
#define KLOG_H

void klog_init(void);
void klog_write(const char* msg);
void klog_write_pair(const char* prefix, const char* value);
void kpanic(const char* msg);
void kpanic_exception(void);
int kpanic_is_active(void);

#endif
