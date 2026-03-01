#ifndef MP_H
#define MP_H

/*
 * 统一内存布局：
 * - 0x00010000 起为内核装载区
 * - 0x00180000 起为视频后缓冲
 * - 0x00280000 起为内核日志区
 * - 0x00300000 ~ 0x007FFFFF 为应用槽位区
 * - 0x00900000 起为窗口像素缓冲
 */

#define MP_KERNEL_CODE_BASE        0x00010000u
#define MP_KERNEL_RESERVED_BASE    MP_KERNEL_CODE_BASE
#define MP_VIDEO_BACK_BUFFER_BASE  0x00180000u
#define MP_KLOG_BASE               0x00280000u

#define MP_APP_SLOT_BASE           0x00300000u
#define MP_APP_SLOT_SIZE           0x00040000u
#define MP_APP_SLOT_LIMIT          0x00800000u
#define MP_APP_SLOT_ADDR(slot)     (MP_APP_SLOT_BASE + ((unsigned int)(slot) * MP_APP_SLOT_SIZE))

#define MP_APP_LOAD_APP            MP_APP_SLOT_ADDR(0)
#define MP_APP_LOAD_TERMINAL       MP_APP_SLOT_ADDR(1)
#define MP_APP_LOAD_WM             MP_APP_SLOT_ADDR(2)
#define MP_APP_LOAD_START          MP_APP_SLOT_ADDR(3)
#define MP_APP_LOAD_IMAGE          MP_APP_SLOT_ADDR(4)
#define MP_APP_LOAD_SETTINGS       MP_APP_SLOT_ADDR(5)

#define MP_KERNEL_RESERVED_LIMIT   MP_APP_SLOT_BASE

#define MP_WINDOW_BUFFER_BASE      0x00900000u
#define MP_WINDOW_BUFFER_SLOTS     8

#endif
