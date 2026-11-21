#include "config.h"

#include <complex.h>
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
        constexpr DWORD max_value_characters = 64;

        // ASCII only: the profile API reads ASCII and UTF-8 files the same way
        constexpr char default_config_text[] =
            "; [default] applies to every monitor. Monitor names are in log.txt, on the monitor lines.\r\n"
            "; See detail: https://github.com/sean1832/taskbar-ninja#config\r\n"
            "[default]\r\n"
            "auto_hide = true\r\n"
            "cover_taskbar_area = true\r\n" // need auto_hide = true
            "show_delay_ms = 0\r\n"
            "hide_delay_ms = 300\r\n"
            "hot_zone_pixels = 2\r\n"
            "; 0 = no animation\r\n"
            "show_animation_ms = 300\r\n"
            "hide_animation_ms = 300\r\n"
            "; original, clear, tint, acrylic or blur\r\n"
            "background = original\r\n"
            "; AARRGGBB in hex\r\n"
            "background_color = 40202020\r\n"
            "background_blur_pixels = 10\r\n"
            "background_feather = true\r\n"
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

        int read_int(SettingsSource& source, const wchar_t* key, IntRange range, int fallback) {
            std::wstring text = read_text(source, key);
            if (text.empty()) {
                return fallback;
            }

            wchar_t* end = nullptr;
            // too big: wcstol gives LONG_MAX, which the range check rejects
            long number = std::wcstol(text.c_str(), &end, 10);
            bool is_whole_number = end != text.c_str() && *end == L'\0';
            bool is_in_range = number >= range.min && number <= range.max;
            if (is_whole_number && is_in_range) {
                return static_cast<int>(number);
            }

            reject_value(source, std::wstring(key) + L" must be a whole number in [" + std::to_wstring(range.min) +
                L", " + std::to_wstring(range.max) + L"]");
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

    std::wstring config_path() {
        const std::wstring folder = appdata_folder();
        if (folder.empty()) {
            return {};
        }
        return folder + L"\\config.ini";
    }

    std::wstring write_default_config() {
        std::wstring path = config_path();
        if (path.empty()) {
            return {};
        }

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

    bool restore_default_config(const std::wstring& config_path) {
        if (config_path.empty()) {
            return false; // empty path
        }

        const std::wstring backup_path = config_path + L".bak";
        // REPLACE EXISTING: only single backup is kept
        if (!MoveFileExW(config_path.c_str(), backup_path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DWORD err = GetLastError();
            if (err != ERROR_FILE_NOT_FOUND) {
                append_log(LogLevel::error, L"config_not_restored",{
                    {L"win32_error", std::to_wstring(err)}
                });
                return false; // file locked or no access. unchanged
            }
        }
        const std::wstring written_path = write_default_config();
        if (read_config_write_time(written_path) == 0) {
            return false; // no file after write
        }
        append_log(LogLevel::info, L"config_written", {{L"backup", backup_path}});
        return true;
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
        settings.show_delay_ms = read_int(source, L"show_delay_ms", delay_ms_range, default_settings.show_delay_ms);
        settings.hide_delay_ms = read_int(source, L"hide_delay_ms", delay_ms_range, default_settings.hide_delay_ms);
        settings.hot_zone_pixels = read_int(source, L"hot_zone_pixels", hot_zone_pixels_range, default_settings.hot_zone_pixels);
        settings.show_animation_ms = read_int(source, L"show_animation_ms", animation_ms_range, default_settings.show_animation_ms);
        settings.hide_animation_ms = read_int(source, L"hide_animation_ms", animation_ms_range, default_settings.hide_animation_ms);

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
        settings.blur_pixels = read_int(source, L"background_blur_pixels", blur_pixels_range, default_settings.blur_pixels);
        settings.feather = read_bool(source, L"background_feather", default_settings.feather);
        settings.feather_percent = read_int(source, L"background_feather_percent", feather_percent_range, default_settings.feather_percent);

        if (source.has_rejected_value) {
            return std::nullopt;
        }
        return settings;
    }

    bool write_config_value(const std::wstring& config_path, const wchar_t* section, const wchar_t* key, const std::wstring& value) {
        if (config_path.empty()) {
            return false; // empty path
        }

        if (!WritePrivateProfileStringW(section, key, value.c_str(), config_path.c_str())) {
            append_log(LogLevel::error, L"config_value_not_written", {
                {L"key", key},
                {L"win32_error", std::to_wstring(GetLastError())}
            });
            return false;
        }
        return true;
    }

    std::wstring make_bool_text(bool value) {
        if (value) {
            return L"true";
        }
        return L"false";
    }

    // same format read_color accept
    std::wstring make_color_text(std::uint32_t color_argb) {
        wchar_t text[9] = {}; // 8 digit and the null
        std::swprintf(text, 9, L"%08X", static_cast<unsigned int>(color_argb)); // convert hex
        return text;
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
