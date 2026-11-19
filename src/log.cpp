#include "log.h"

#include <windows.h>

#include <cwchar>
#include <string>
#include <string_view>

namespace ninja {
    namespace {
        constexpr LONGLONG max_log_bytes = 1024 * 1024; // 1 MiB

        // "main" until a mode is chosen
        std::wstring log_role = L"main";

        std::wstring log_path() {
            std::wstring folder = app_data_folder();
            if (folder.empty()) {
                return {};
            }
            return folder + L"\\log.txt";
        }

        std::string to_utf8(std::wstring_view text) {
            int text_length = static_cast<int>(text.size());
            // first call asks for the size, second call converts
            int utf8_length = WideCharToMultiByte(CP_UTF8, 0, text.data(), text_length, nullptr, 0, nullptr, nullptr);
            std::string utf8(static_cast<size_t>(utf8_length), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), text_length, utf8.data(), utf8_length, nullptr, nullptr);
            return utf8;
        }

        std::wstring_view level_text(LogLevel level) {
            switch (level) {
            case LogLevel::info:
                return L"INF";
            case LogLevel::warning:
                return L"WRN";
            case LogLevel::error:
                return L"ERR";
            case LogLevel::critical:
                return L"CRT";
            }
            return L"???"; // MSVC: not all paths return
        }

        bool should_quote(std::wstring_view value) {
            if (value.empty()) {
                return true;
            }
            // space, quote, equals, line break
            return value.find_first_of(L" \"=\r\n\t") != std::wstring_view::npos;
        }

        // logfmt value: bare, or in quotes with \ " and line breaks escaped
        void append_value(std::wstring& line, std::wstring_view value) {
            if (!should_quote(value)) {
                line += value;
                return;
            }

            // Windows error text from FormatMessageW ends with \r\n
            line += L'"';
            for (wchar_t character : value) {
                switch (character) {
                case L'\\':
                    line += L"\\\\";
                    break;
                case L'"':
                    line += L"\\\"";
                    break;
                case L'\r':
                    line += L"\\r";
                    break;
                case L'\n':
                    line += L"\\n";
                    break;
                case L'\t':
                    line += L"\\t";
                    break;
                default:
                    line += character;
                    break;
                }
            }
            line += L'\"';
        }
    } // namespace

    void set_log_role(std::wstring_view role) {
        log_role = role;
    }

    std::wstring app_data_folder() {
        wchar_t buffer[MAX_PATH] = {};
        DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
        // 0 = unset, >= MAX_PATH = buffer too small
        if (length == 0 || length >= MAX_PATH) {
            return {};
        }

        std::wstring folder = std::wstring(buffer, length) + L"\\taskbar-ninja";
        // fails harmlessly if the folder already exists
        CreateDirectoryW(folder.c_str(), nullptr);
        return folder;
    }

    void rotate_log() {
        std::wstring path = log_path();
        if (path.empty()) {
            return;
        }

        WIN32_FILE_ATTRIBUTE_DATA attributes = {};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
            return; // file does not exist
        }

        LONGLONG size_bytes = (static_cast<LONGLONG>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
        if (size_bytes <= max_log_bytes) {
            return;
        }

        // log.txt -> log.old.txt
        const std::wstring old_path = path.substr(0, path.size() - 4) + L".old.txt";
        MoveFileExW(path.c_str(), old_path.c_str(), MOVEFILE_REPLACE_EXISTING);
    }

    void append_log(LogLevel level, std::wstring_view event, std::initializer_list<LogField> fields) {
        std::wstring path = log_path();
        if (path.empty()) {
            return;
        }

        SYSTEMTIME now = {};
        GetLocalTime(&now);
        wchar_t time_text[32];
        std::swprintf(time_text, 32, L"%04u-%02u-%02uT%02u:%02u:%02u.%03u",
                      now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);

        // 2026-11-18T10:00:00.123 ERR app[1234] startup_failed step=window
        std::wstring line = time_text;
        line += L' ';
        line += level_text(level);
        line += L' ';
        line += log_role;
        line += L'[';
        line += std::to_wstring(GetCurrentProcessId());
        line += L"] ";
        line += event;
        for (const LogField& field : fields) {
            line += L' ';
            line += field.key;
            line += L'=';
            append_value(line, field.value);
        }
        line += L'\n';

        // DEBUG: print to console
        OutputDebugStringW(line.c_str());

        std::string utf8 = to_utf8(line);

        // FILE_APPEND_DATA: each write lands at the end, even when two processes writing
        HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return;
        }

        DWORD written_bytes = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written_bytes, nullptr);
        CloseHandle(file);
    }
} // namespace ninja

