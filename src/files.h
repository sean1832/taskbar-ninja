#pragma once

#include <windows.h>
#include <string>

namespace ninja {
    /**
     * Get Appdata folder for this program on this machine
     * @return Appdata folder
     * @remark C:\\Users\\<username>\\AppData\\Roaming\\taskbar-ninja
     */
    [[nodiscard]] std::wstring appdata_folder();

    /**
     * Opens a file in its default app, or a folder in Explorer.
     * @param path directory or file to open
     * @param result_code shell status code when not null, above 32 = succeeded
     * @return true when the shell opened the path
     * @remark See https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecutew
     */
    bool open_path(const std::wstring& path, INT_PTR* result_code);
}