#include "files.h"

#include <AsyncInfo.h>
#include <windows.h>
#include <shellapi.h>

#include <string>

namespace ninja {
    std::wstring appdata_folder() {
        wchar_t buffer[MAX_PATH] = {};
        DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
        // 0 = unset, >= MAX_PATH = buffer too small. But path shouldn't exceeding MAX_PATH
        if (length == 0 || length >= MAX_PATH) {
            return {};
        }

        std::wstring folder = std::wstring(buffer, length) + L"\\taskbar-ninja";
        if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
            return {}; // when directory creation failed
        }

        return folder;
    }

    bool open_path(const std::wstring& path, INT_PTR* result_code) {
        // shell add-ins can need COM STA, see the ShellExecuteW remarks
        HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        HINSTANCE result = ShellExecuteW(
                nullptr,       // no parent window
                L"open",       // file: default app, folder: Explorer
                path.c_str(),  // file or folder
                nullptr,       // no parameters
                nullptr,       // current working directory
                SW_SHOWNORMAL  // show window normally
            );

        // S_OK and S_FALSE both need a matching CoUninitialize
        if (SUCCEEDED(com_result)) {
            CoUninitialize();
        }

        const auto status = reinterpret_cast<INT_PTR>(result);
        if (result_code != nullptr) {
            *result_code = status;
        }

        return status > 32;
    }
} // namespace ninja