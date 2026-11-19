#pragma once

#include <windows.h>

namespace ninja {
    /**
     * Resolves the monitor whose taskbar a front window keeps shown.
     * @param window front window
     * @return monitor of the window when it is shell UI or a taskbar, otherwise nullptr
     * @remarks Shell UI is Start, search, quick settings, notification centre, calendar, jump lists and tray overflow.
     */
    [[nodiscard]] HMONITOR resolve_shell_monitor(HWND window);

    /**
     * Resolves the monitor whose taskbar has a popup under the mouse.
     * @param cursor_pixels mouse position
     * @return monitor of the taskbar that owns the popup, otherwise nullptr
     * @remarks Popups are the app preview, tooltips and the taskbar's own right-click menu. Task view and Alt+Tab use the same window class as the app preview, so they count too.
     */
    [[nodiscard]] HMONITOR resolve_popup_monitor(POINT cursor_pixels);
} // namespace ninja