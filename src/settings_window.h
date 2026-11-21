#pragma once

#include <windows.h>

namespace ninja {
    /// posted to the app window when config.ini changed, by the window or by a hand edit the window found
    inline constexpr UINT config_changed_message = WM_APP + 3; // WM_APP + 1 tray, WM_APP + 2 explorer ready

    /// sent to the app window while a background control moves; lparam is a const BackgroundSettings*, valid during the call only
    inline constexpr UINT settings_preview_message = WM_APP + 4;

    /**
     * Opens the settings window, or brings it to the front when it is already open.
     * @param app_window window that gets config_changed_message
     * @remarks One window at most. Runs on the calling thread, so call it from the app UI thread.
     *          Reads config.ini on open, and again on focus when the file is newer.
     */
    void open_settings_window(HWND app_window);
} // namespace ninja
