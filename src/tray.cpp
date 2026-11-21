#include "tray.h"

#include <windows.h>
#include <shellapi.h>

namespace ninja {
    namespace {
        // one icon per window; id tells icons of the same window apart
        constexpr UINT tray_icon_id = 1;

        // icon id in resource/app.rc
        constexpr WORD app_icon_id = 1;
    } // namespace

    bool add_tray_icon(HWND window, const wchar_t* tooltip) {
        NOTIFYICONDATAW icon = {};
        icon.cbSize = sizeof(icon);
        icon.hWnd = window;
        icon.uID = tray_icon_id;
        icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        icon.uCallbackMessage = tray_message;

        // small icon size, so the tray icon isn't shrunk from a large one
        // LR_SHARED: Windows owns the handle, no DestroyIcon
        const int width_pixels = GetSystemMetrics(SM_CXSMICON);
        const int height_pixels = GetSystemMetrics(SM_CYSMICON);
        icon.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(app_icon_id),
            IMAGE_ICON, width_pixels, height_pixels, LR_SHARED));

        // copies and cuts to fit szTip
        lstrcpynW(icon.szTip, tooltip, ARRAYSIZE(icon.szTip));
        return Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
    }

    void remove_tray_icon(HWND window) {
        NOTIFYICONDATAW icon = {};
        icon.cbSize = sizeof(icon);
        icon.hWnd = window;
        icon.uID = tray_icon_id;
        Shell_NotifyIconW(NIM_DELETE, &icon);
    }

    TrayCommand show_tray_menu(HWND window, bool paused, bool autostart_enabled) {
        UINT pause_flags = MF_STRING;
        if (paused) {
            pause_flags |= MF_CHECKED;
        }

        UINT autostart_flags = MF_STRING;
        if (autostart_enabled) {
            autostart_flags |= MF_CHECKED;
        }

        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, pause_flags, static_cast<UINT_PTR>(TrayCommand::toggle_pause), L"Pause");
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(TrayCommand::open_settings), L"Settings");
        AppendMenuW(menu, autostart_flags, static_cast<UINT_PTR>(TrayCommand::toggle_autostart), L"Start With Windows");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(TrayCommand::restore_and_exit), L"Exit");

        POINT cursor = {};
        GetCursorPos(&cursor);

        // foreground first, so a click outside closes the menu
        // See https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-trackpopupmenu#remarks
        SetForegroundWindow(window);
        // TPM_RETURNCMD: return the chosen id instead of posting WM_COMMAND
        UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY;
        BOOL chosen_id = TrackPopupMenu(menu, flags, cursor.x, cursor.y, 0, window, nullptr);
        PostMessageW(window, WM_NULL, 0, 0);
        DestroyMenu(menu);

        switch (const auto chosen = static_cast<TrayCommand>(chosen_id)) {
        case TrayCommand::none:
        case TrayCommand::toggle_pause:
        case TrayCommand::restore_and_exit:
        case TrayCommand::toggle_autostart:
        case TrayCommand::open_settings:
            return chosen;
        }
        // unknown id: treat as no choice
        return TrayCommand::none;
    }
} // namespace ninja