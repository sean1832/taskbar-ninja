#pragma once

#include <windows.h>

#include "exit_code.h"

namespace ninja {
    /**
     * Starts taskbar-ninja.exe --watchdog with the process id of this app.
     * @return watchdog process handle, or nullptr when the watchdog does not start
     * @remarks The caller owns the handle and closes it.
     */
    [[nodiscard]] HANDLE start_watchdog();

    /**
     * Watches the main app and restores every taskbar when the app ends, freezes, or the restore hotkey is pressed.
     * @param app_process_id process id of the main app
     * @return success, also when the app is already gone
     */
    [[nodiscard]] ExitCode run_watchdog(DWORD app_process_id);
}