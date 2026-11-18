#pragma once

#include <windows.h>

#include "exit_code.h"

namespace ninja {
    /// window class of the main app window. --restore closes it, and the watchdog pings it.
    inline constexpr wchar_t app_window_class[] = L"taskbar-ninja";

    /// Runs the main app until the user exits: tray icon, taskbar hiding.
    /// Remarks: returns already_running when another app holds the single instance mutex
    [[nodiscard]] ExitCode run_app(HINSTANCE instance);
}
