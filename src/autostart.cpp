#include "autostart.h"

#include <windows.h>

#include <string>

namespace ninja {
    namespace {
        // apps Windows starts at sign-in, for current user only
        constexpr wchar_t run_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
        constexpr wchar_t run_value_name[] = L"taskbar-ninja";

        // quoted exe path, empty when path does not fit MAX_PATH (shouldn't happen)
        std::wstring make_run_command() {
            wchar_t exe_path[MAX_PATH];
            DWORD length = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
            if (length == 0 || length >= MAX_PATH) {
                return {};
            }
            // quoted path
            return std::wstring(L"\"") + exe_path + L"\"";
        }
    } // namespace

    bool is_autostart_enabled() {
        const std::wstring command = make_run_command();
        if (command.empty()) {
            return false;
        }

        // room for a quoted MAX_PATH path
        wchar_t stored_command[MAX_PATH + 3] = {};
        DWORD stored_bytes = sizeof(stored_command);
        // RRF_RT_REG_SZ: text value only, always null-terminated
        LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, run_key, run_value_name, RRF_RT_REG_SZ, nullptr,
            stored_command, &stored_bytes);
        if (status != ERROR_SUCCESS) {
            return false; // missing value, or longer than our path can be
        }

        // windows path ignore case
        return lstrcmpiW(stored_command, command.c_str()) == 0;
    }

    LSTATUS set_autostart(bool enabled) {
        if (!enabled) {
            LSTATUS status = RegDeleteKeyValueW(HKEY_CURRENT_USER, run_key, run_value_name);
            if (status == ERROR_FILE_NOT_FOUND) {
                return ERROR_SUCCESS; // already gone
            }
            return status;
        }

        const std::wstring command = make_run_command();
        if (command.empty()) {
            return ERROR_FILENAME_EXCED_RANGE;
        }

        // size in bytes, with the end null
        const auto command_bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
        return RegSetKeyValueW(HKEY_CURRENT_USER, run_key, run_value_name, REG_SZ, command.c_str(), command_bytes);
    }
} // namespace ninja