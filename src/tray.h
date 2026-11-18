#pragma once

#include <windows.h>

namespace ninja {
    /// Window message the tray icon sends for mouse events.
    inline constexpr UINT tray_message = WM_APP + 1; // WM_APP + 1: range is free to use, not clash with a windows message.

    /// Choices in the tray menu. The values are the menu item ids.
    /// Zero means the menu closed with no choice.
    enum class TrayCommand : UINT {
        none = 0,
        toggle_hide = 1,
        restore_and_exit = 2,
    };

    /// Adds the tray icon for a window, with a tooltip
    /// returns false when the shell rejected
    [[nodiscard]] bool add_tray_icon(HWND window, const wchar_t* tooltip);

    /// Remove the tray icon of a window
    void remove_tray_icon(HWND window);

    /// Shows the tray menu at the cursor and returns the choice
    [[nodiscard]] TrayCommand show_tray_menu(HWND window, bool taskbar_hidden);
} // namespace ninja
