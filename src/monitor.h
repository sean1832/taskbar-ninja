#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace ninja {
    /**
     * One active monitor and the taskbar on it
     */
    struct Monitor {
        HMONITOR handle = nullptr; ///< Windows monitor handles, changes after a display change
        std::wstring device_name; ///< GDI name (like DISPLAY1), change after a reconnect
        std::wstring model_name; ///< model name from EDID, empty when Windows has none
        RECT bounds_pixels = {}; ///< full monitor area in virtual screen pixels
        bool is_primary = false; ///< true for the main monitor
        HWND taskbar = nullptr; ///< taskbar on this monitor, nullptr when it has none
    };

    /**
    * Builds the list of active monitors, each with its model name and taskbar.
    * @return one entry per monitor, in Windows order
    */
    [[nodiscard]] std::vector<Monitor> build_monitor_list();

    /**
     * Writes one log line per monitor, so the user can copy the model name into config.ini.
     * @param monitors list from build_monitor_list
     */
    void log_monitor_list(const std::vector<Monitor>& monitors);
}