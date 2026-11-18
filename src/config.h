#pragma once

#include <optional>
#include <string>

namespace ninja {

    /**
     * Settings for the taskbar on one monitor.
     */
    struct MonitorSettings {
        bool auto_hide = false;         ///< hide the taskbar until the mouse touches the hot zone
        int show_delay_ms = 0;          ///< time in the hot zone before the taskbar shows
        int hide_delay_ms = 300;        ///< time away from the taskbar before it hides
        int hot_zone_pixels = 2;        ///< height of the strip at the bottom edge
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
}
