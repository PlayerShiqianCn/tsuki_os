#ifndef KERNEL_CONFIG_H
#define KERNEL_CONFIG_H

void kernel_set_wallpaper_style(int style);
int kernel_get_wallpaper_style(void);

void kernel_set_start_page_enabled(int enabled);
int kernel_is_start_page_enabled(void);

void kernel_reload_system_config(void);

#endif
