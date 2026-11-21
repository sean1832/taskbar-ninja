#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>

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
     * Starts watching the config folder for written and renamed files.
     * @param config_path path from write_default_config
     * @return change handle to wait on, or nullptr when the folder cannot be watched
     * @remarks Every file in the folder signals, log.txt included. Call FindNextChangeNotification after each signal.
     */
    [[nodiscard]] HANDLE start_config_watch(const std::wstring& config_path);

    /**
     * Reads when the config file was last written.
     * @param config_path path from write_default_config
     * @return write time in FILETIME units, or 0 when the file is missing
     */
    [[nodiscard]] ULONGLONG read_config_write_time(const std::wstring& config_path);
} // namespace ninja
