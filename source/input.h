#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>
#include <windows.h>

#define INPUT_IDLE_TIMEOUT_SECONDS  5
#define INPUT_MAX_TEXT              4096
#define INPUT_MAX_SCANCODES         4096
#define ALLOW_DEBUG_BREAK           0

typedef struct {
    uint64_t  timestamp_start;
    uint64_t  timestamp_end;
    char*     text;
    uint8_t*  scancodes;
    char      layout[16];
    char*     focused_app;
} InputPacket;

typedef void (*InputPacketCallback)(InputPacket* packet, void* userdata);
int  input_init(InputPacketCallback callback, void* userdata, int idle_secs);
int  input_poll(void);
void input_shutdown(void);
void input_free_packet(InputPacket* p);

#endif /* INPUT_H */