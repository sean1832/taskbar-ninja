#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

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
     * Look of the background of every taskbar.
     */
    enum class BackgroundStyle {
        original, ///< what Explorer draws
        clear,    ///< no background, the desktop shows through
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
     * Reads the background style of every taskbar.
     * @param config_path path from write_default_config
     * @return background from [default], or original when it is missing or breaks its rule
     * @remarks One style for every taskbar. A monitor section does not override it.
     */
    [[nodiscard]] BackgroundStyle read_background_style(const std::wstring& config_path);

    /**
     * Gives the config word of a background style.
     * @param style background style
     * @return word as written in config.ini, for example clear
     */
    [[nodiscard]] std::wstring_view background_style_name(BackgroundStyle style);

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
