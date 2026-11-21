#pragma once
#include "animation.h"

namespace ninja {
    /// posted to the app window after the settings window writes one key to config.ini
    inline constexpr UINT settings_committed_message = WM_APP + 3; // WM_APP + 1 tray, WM_APP + 2 explorer ready

    /**
     * Opens the settings window, or brings it to the front when it is already open.
     * @remarks One window at most. Runs on the calling thread, so call it from the app UI thread.
     */
    void open_settings_window(HWND app_window);

    /**
    * Makes the open settings window read config.ini again.
    * @remarks Does nothing when the window is closed.
    */
    void reload_settings_window();
} // namespace ninja