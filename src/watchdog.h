#pragma once

#include <windows.h>

#include "exit_code.h"

namespace ninja {
    /**
     * Starts "taskbar-ninja.exe --watchdog <this_pid>"
     * @return process handle or nullptr
     */
    [[nodiscard]] HANDLE start_watchdog();

    /**
     * Watches the main app and restores every taskbar when the app ends, freezes, or the restore hotkey is pressed.
     * @param app_process_id pid
     * @return exit code
     */
    [[nodiscard]] ExitCode run_watchdog(DWORD app_process_id);
}