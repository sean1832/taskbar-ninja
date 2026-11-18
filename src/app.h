#pragma once

#include <windows.h>

#include "exit_code.h"

namespace ninja {
    /**
     * Window class of the main app window.
     * @remarks --restore closes this window, and the watchdog pings it.
     */
    inline constexpr wchar_t app_window_class[] = L"taskbar-ninja";

    /**
     * Runs the main app until the user exits, with the tray icon, taskbar hiding and the watchdog.
     * @param instance module handle from wWinMain
     * @return success, startup_failed, or already_running when another app holds the single instance mutex
     */
    [[nodiscard]] ExitCode run_app(HINSTANCE instance);
}
