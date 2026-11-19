#pragma once

#include <windows.h>

#include <optional>
#include <string>

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
    };

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
