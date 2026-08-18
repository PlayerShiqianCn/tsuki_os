// ps2.h
#ifndef PS2_H
#define PS2_H

void ps2_init(void);
void ps2_poll_inputs_once(void);
char ps2_getchar(void);
int ps2_has_key(void);

// 特殊按键码 (返回值 >= 0x80, 不与 ASCII 冲突)
// 通过 SYS_GET_KEY 透传给用户态, get_key() 返回 int 可完整保存 0-255
#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83

void ps2_mouse_init(void);

typedef struct {
	signed char dx;
	signed char dy;
	unsigned char buttons; 
} ps2_mouse_event_t;

int ps2_get_mouse_event(ps2_mouse_event_t *out);

// 键盘中断处理函数
void keyboard_handler_isr(void);

// 鼠标中断处理函数
void mouse_handler_isr(void);

#endif
