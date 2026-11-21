#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "explorer_dll.h"

namespace ninja {

    /**
     * Settings for the taskbar on one monitor.
     */
    struct MonitorSettings {
        bool auto_hide = false;          ///< hide the taskbar until the mouse touches the hot zone
        bool cover_taskbar_area = false; ///< let windows fill the taskbar strip while the taskbar auto-hides
        int show_delay_ms = 0;           ///< time in the hot zone before the taskbar shows
        int hide_delay_ms = 300;         ///< time away from the taskbar before it hides
        int hot_zone_pixels = 2;         ///< thickness of the strip at the taskbar edge
        int show_animation_ms = 0;       ///< slide in time. 0 = disabled animation (instant)
        int hide_animation_ms = 0;       ///< slide out time. 0 = disabled animation (instant)
    };

    /**
     * Settings for the background of every taskbar.
     */
    struct BackgroundSettings {
        BackgroundStyle style = BackgroundStyle::original;
        std::uint32_t color_argb = 0x40202020; ///< tint, acrylic and blur color, AARRGGBB
        int blur_pixels = 10;                  ///< blur strength
        bool feather = false;                  ///< blur fades in from the inner edge
        int feather_percent = 50;              ///< part of the bar thickness the blur fades in over

        bool operator==(const BackgroundSettings&) const = default;
    };

    /**
     * Closed range of whole numbers that one config key accepts.
     */
    struct IntRange {
        int min;
        int max;
    };

    inline constexpr IntRange delay_ms_range = {0, 10000};      ///< show/hide delay in milliseconds
    inline constexpr IntRange hot_zone_pixels_range = {1, 50};  ///< hot zone pixels
    inline constexpr IntRange animation_ms_range = {0, 2000};   ///< show/hide animation in milliseconds
    inline constexpr IntRange blur_pixels_range = {1, 50};      ///< background blur pixels
    inline constexpr IntRange feather_percent_range = {1, 100}; ///< background feather percentage

    /// section every monitor reads after its own
    inline constexpr wchar_t default_section[] = L"default";

    /**
     * Get the config.ini path in appdata folder
     * @return config.ini path
     */
    [[nodiscard]] std::wstring config_path();

    /**
     * Writes config.ini with the default settings when the file does not exist yet.
     * @return config file path, or empty when APPDATA is unset
     * @remarks Never overwrites the file of the user.
     */
    [[nodiscard]] std::wstring write_default_config();

    /**
     * Replaces config.ini with the default settings and keeps the old file as config.ini.bak.
     * @param config_path path from write_default_config
     * @return true when config.ini has the default text
     * @remarks Removes monitor sections and hand comments too. Logs one line.
     */
    [[nodiscard]] bool restore_default_config(const std::wstring& config_path);

    /**
     * Reads the settings of one monitor from its own section, then from [default], then from the built-in values.
     * @param config_path path from write_default_config
     * @param model_name monitor model name, which is also its section name
     * @return settings, or nullopt when a value breaks its rule
     * @remarks Logs one line for each rejected value.
     */
    [[nodiscard]] std::optional<MonitorSettings> read_monitor_settings(const std::wstring& config_path, const std::wstring& model_name);

    /**
     * Reads the background settings of every taskbar from [default], then from the built-in values.
     * @param config_path path from write_default_config
     * @return settings, or nullopt when a value breaks its rule
     * @remarks One look for every taskbar. A monitor section does not override it. Logs one line for each rejected value.
     */
    [[nodiscard]] std::optional<BackgroundSettings> read_background_settings(const std::wstring& config_path);

    /**
     * Reads the names of the monitor sections in config.ini, every section but [default].
     * @param config_path path from write_default_config
     * @return section names in file order, empty when the file has none
     */
    [[nodiscard]] std::vector<std::wstring> read_monitor_sections(const std::wstring& config_path);

    /**
     * Writes one key into one section of config.ini and keeps every other line.
     * @param config_path path from write_default_config
     * @param section default_section or a monitor model name
     * @param key key name
     * @param value value text in the file format
     * @return true when the file has the new value
     * @remarks Writes key=value with no spaces. Logs one line when the write is refused.
     */
    [[nodiscard]] bool write_config_value(const std::wstring& config_path, const wchar_t* section, const wchar_t* key,
        const std::wstring& value);

    /**
     * Tells whether one section of config.ini sets one key.
     * @param config_path path from write_default_config
     * @param section default_section or a monitor model name
     * @param key key name
     * @return true when the key has a value that is not empty
     * @remarks An empty value counts as missing, as in the readers.
     */
    [[nodiscard]] bool has_config_value(const std::wstring& config_path, const wchar_t* section, const wchar_t* key);

    /**
     * Removes one key from one section of config.ini and keeps every other line.
     * @param config_path path from write_default_config
     * @param section default_section or a monitor model name
     * @param key key name
     * @return true when the file no longer has the key
     * @remarks Logs one line when the write is refused. Leaves the section line, even when it is empty.
     */
    [[nodiscard]] bool delete_config_value(const std::wstring& config_path, const wchar_t* section, const wchar_t* key);

    /**
     * Makes the config text of a bool, "true" or "false".
     */
    [[nodiscard]] std::wstring make_bool_text(bool value);

    /**
     * Makes the config text of a color, 8 hex digits AARRGGBB.
     */
    [[nodiscard]] std::wstring make_color_text(std::uint32_t color_argb);

    /**
     * Reads when the config file was last written.
     * @param config_path path from write_default_config
     * @return write time in FILETIME units, or 0 when the file is missing
     */
    [[nodiscard]] ULONGLONG read_config_write_time(const std::wstring& config_path);
} // namespace ninja
