#include "config.h"

#include <windows.h>

#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>

#include "files.h"
#include "log.h"

namespace ninja {
    namespace {
        constexpr wchar_t default_section[] = L"default";
        constexpr DWORD max_value_characters = 64;

        constexpr int min_delay_ms = 0;
        constexpr int max_delay_ms = 10000;
        constexpr int min_hot_zone_pixels = 1;
        constexpr int max_hot_zone_pixels = 50;

        constexpr int min_blur_pixels = 1;
        constexpr int max_blur_pixels = 50;
        constexpr int min_feather_percent = 1;
        constexpr int max_feather_percent = 100;

        // ASCII only: the profile API reads ASCII and UTF-8 files the same way
        constexpr char default_config_text[] =
            "; [default] applies to every monitor. A section named after a monitor overrides it.\r\n"
            "; Monitor names are in log.txt, on the monitor lines.\r\n"
            "[default]\r\n"
            "auto_hide = false\r\n"
            "cover_taskbar_area = false\r\n" // need auto_hide = true
            "show_delay_ms = 0\r\n"
            "hide_delay_ms = 300\r\n"
            "hot_zone_pixels = 2\r\n"
            "; background of every taskbar, read from [default] only\r\n"
            "; original, clear, tint, acrylic or blur\r\n"
            "background = original\r\n"
            "; AARRGGBB in hex, for tint, acrylic and blur\r\n"
            "background_color = 40202020\r\n"
            "background_blur_pixels = 10\r\n"
            "; blur fades in from the inner edge, over this part of the bar thickness\r\n"
            "background_feather = false\r\n"
            "background_feather_percent = 50\r\n";


        // where values come from, and whether one was rejected
        struct SettingsSource {
            const std::wstring& config_path;
            const std::wstring& model_name;
            bool has_rejected_value = false;
        };

        // own section first, then [default]; empty when neither has the key
        std::wstring read_text(const SettingsSource& source, const wchar_t* key) {
            wchar_t value[max_value_characters] = {};
            if (!source.model_name.empty()) {
                GetPrivateProfileStringW(source.model_name.c_str(), key, L"", value, max_value_characters,
                    source.config_path.c_str());
            }
            if (value[0] != L'\0') {
                return value;
            }
            GetPrivateProfileStringW(default_section, key, L"", value, max_value_characters,
                source.config_path.c_str());
            return value;
        }

        void reject_value(SettingsSource& source, const std::wstring& message) {
            source.has_rejected_value = true;
            append_log(LogLevel::warning, L"config_rejected", {
                {L"monitor", source.model_name},
                {L"message", message}
            });
        }

        bool read_bool(SettingsSource& source, const wchar_t* key, bool fallback) {
            std::wstring text = read_text(source, key);
            if (text.empty()) {
                return fallback;
            }
            if (_wcsicmp(text.c_str(), L"true") == 0) {
                return true;
            }
            if (_wcsicmp(text.c_str(), L"false") == 0) {
                return false;
            }
            reject_value(source, std::wstring(key) + L" must be \"true\" or \"false\"");
            return fallback;
        }

        int read_int(SettingsSource& source, const wchar_t* key, int min, int max, int fallback) {
            std::wstring text = read_text(source, key);
            if (text.empty()) {
                return fallback;
            }

            wchar_t* end = nullptr;
            // too big: wcstol gives LONG_MAX, which the range check rejects
            long number = std::wcstol(text.c_str(), &end, 10);
            bool is_whole_number = end != text.c_str() && *end == L'\0';
            bool is_in_range = number >= min && number <= max;
            if (is_whole_number && is_in_range) {
                return static_cast<int>(number);
            }

            reject_value(source, std::wstring(key) + L" must be a whole number in [" + std::to_wstring(min) +
                L", " + std::to_wstring(max) + L"]");
            return fallback;
        }

        BackgroundStyle read_style(SettingsSource& source, const wchar_t* key, BackgroundStyle fallback) {
            std::wstring text = read_text(source, key);
            if (text.empty()) {
                return fallback;
            }
            for (const BackgroundStyleName& entry : background_style_names) {
                if (_wcsicmp(text.c_str(), entry.name) == 0) {
                    return entry.style;
                }
            }

            std::wstring message = std::wstring(key) + L" must be one of";
            for (const BackgroundStyleName& entry : background_style_names) {
                message += L" ";
                message += entry.name;
            }
            reject_value(source, message);
            return fallback;
        }

        // exactly 8 hex digits: wcstoul alone would take 0x, a sign, or spaces
        std::uint32_t read_color(SettingsSource& source, const wchar_t* key, std::uint32_t fallback) {
            std::wstring text = read_text(source, key);
            if (text.empty()) {
                return fallback;
            }
            bool is_color = text.size() == 8;
            for (wchar_t character : text) {
                if (!std::iswxdigit(character)) {
                    is_color = false;
                }
            }
            if (is_color) {
                return static_cast<std::uint32_t>(std::wcstoul(text.c_str(), nullptr, 16));
            }
            reject_value(source, std::wstring(key) + L" must be 8 hex digits, AARRGGBB");
            return fallback;
        }
    } // namespace

    std::wstring write_default_config() {
        std::wstring folder = appdata_folder();
        if (folder.empty()) {
            return {};
        }
        std::wstring path = folder + L"\\config.ini";

        // CREATE_NEW: fails when the file exists, so the file of the user stays
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            if (error != ERROR_FILE_EXISTS) {
                append_log(LogLevel::error, L"config_not_written", {
                    {L"win32_error", std::to_wstring(error)}
                });
            }
            return path;
        }

        DWORD written_bytes = 0;
        WriteFile(file, default_config_text, sizeof(default_config_text) - 1, &written_bytes, nullptr);
        CloseHandle(file);
        append_log(LogLevel::info, L"config_written", {{L"path", path}});
        return path;
    }

    std::optional<MonitorSettings> read_monitor_settings(const std::wstring& config_path, const std::wstring& model_name) {
        MonitorSettings default_settings;
        // no file path: an empty path makes windows read from its own folder
        if (config_path.empty()) {
            return default_settings;
        }

        SettingsSource source = {config_path, model_name};
        MonitorSettings settings;
        settings.auto_hide = read_bool(source, L"auto_hide", default_settings.auto_hide);
        settings.cover_taskbar_area = read_bool(source, L"cover_taskbar_area", default_settings.cover_taskbar_area);
        settings.show_delay_ms = read_int(source, L"show_delay_ms", min_delay_ms, max_delay_ms, default_settings.show_delay_ms);
        settings.hide_delay_ms = read_int(source, L"hide_delay_ms", min_delay_ms, max_delay_ms, default_settings.hide_delay_ms);
        settings.hot_zone_pixels = read_int(source, L"hot_zone_pixels", min_hot_zone_pixels, max_hot_zone_pixels, default_settings.hot_zone_pixels);

        if (source.has_rejected_value) {
            return std::nullopt;
        }
        return settings;
    }

    std::optional<BackgroundSettings> read_background_settings(const std::wstring& config_path) {
        BackgroundSettings default_settings;
        // no file path: an empty path makes windows read from its own folder
        if (config_path.empty()) {
            return default_settings;
        }

        // empty model name: [default] only
        std::wstring no_model_name;
        SettingsSource source = {config_path, no_model_name};
        BackgroundSettings settings;
        settings.style = read_style(source, L"background", default_settings.style);
        settings.color_argb = read_color(source, L"background_color", default_settings.color_argb);
        settings.blur_pixels = read_int(source, L"background_blur_pixels", min_blur_pixels, max_blur_pixels, default_settings.blur_pixels);
        settings.feather = read_bool(source, L"background_feather", default_settings.feather);
        settings.feather_percent = read_int(source, L"background_feather_percent", min_feather_percent, max_feather_percent, default_settings.feather_percent);

        if (source.has_rejected_value) {
            return std::nullopt;
        }
        return settings;
    }

    HANDLE start_config_watch(const std::wstring& config_path) {
        size_t separator_index = config_path.find_last_of(L"\\");
        if (separator_index == std::wstring::npos) {
            return nullptr;
        }
        std::wstring folder = config_path.substr(0, separator_index);

        // last write: saved in place; file name: saved through a temp file and a rename
        HANDLE watch = FindFirstChangeNotificationW(folder.c_str(), FALSE,
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
        if (watch == INVALID_HANDLE_VALUE) {
            append_log(LogLevel::error, L"config_watch_rejected", {{L"win32_error", std::to_wstring(GetLastError())}});
            return nullptr;
        }
        return watch;
    }

    ULONGLONG read_config_write_time(const std::wstring& config_path) {
        WIN32_FILE_ATTRIBUTE_DATA attributes = {};
        if (!GetFileAttributesExW(config_path.c_str(), GetFileExInfoStandard, &attributes)) {
            return 0;
        }

        ULARGE_INTEGER write_time = {};
        write_time.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
        write_time.HighPart = attributes.ftLastWriteTime.dwHighDateTime;
        return write_time.QuadPart;
    }
} // namespace ninja
