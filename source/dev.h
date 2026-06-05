#ifndef DEV_H
#define DEV_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include "env.h"
#include "bot.h"

int install();
int uninstall(uint64_t confirm);
int update(const char* new_exe_path, const char* msg_id);
void get_channel_id(char* oout, size_t osize);
int save_channel_id(const char* channel_id);
uint64_t get_installed_exe_msg_id(void);
void attach_console_if_present(void);
bool is_disabled(void);
void set_disabled(bool disabled);

#endif /* DEV_H */