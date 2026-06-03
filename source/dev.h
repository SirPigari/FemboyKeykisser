#ifndef DEV_H
#define DEV_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include "env.h"

int install();
int uninstall(uint64_t confirm);
void get_channel_id(char* oout, size_t osize);
int save_channel_id(const char* channel_id);
void attach_console_if_present(void);
bool is_disabled(void);
void set_disabled(bool disabled);

#endif /* DEV_H */