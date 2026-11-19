#pragma once

#include <windows.h>

namespace ninja {
    /**
     * Window message the tray icon sends for mouse events.
     * @remarks The WM_APP range is free for app use, so it does not clash with a Windows message.
     */
    inline constexpr UINT tray_message = WM_APP + 1;

    /**
     * Choices in the tray menu.
     * @remarks The values are the menu item ids.
     */
    enum class TrayCommand : UINT {
        none = 0,             ///< menu closed with no choice
        toggle_hide = 1,      ///< hide or show the taskbar
        restore_and_exit = 2, ///< show the taskbar and close the app
    };

    /**
     * Adds the tray icon for a window.
     * @param window window that gets tray_message
     * @param tooltip text shown on hover, cut to 127 characters
     * @return false when the shell rejects the icon
     */
    [[nodiscard]] bool add_tray_icon(HWND window, const wchar_t* tooltip);

    /**
     * Removes the tray icon of a window.
     * @param window window that owns the icon
     */
    void remove_tray_icon(HWND window);

    /**
     * Shows the tray menu at the cursor and waits for a choice.
     * @param window owner window of the menu
     * @param hiding_enabled puts a check mark on "Hide taskbars" when true
     * @return chosen command, or none when the menu closed with no choice
     */
    [[nodiscard]] TrayCommand show_tray_menu(HWND window, bool hiding_enabled);
} // namespace ninja
