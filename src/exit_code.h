#pragma once

namespace ninja {

    /// Process exit codes for every mode.
    enum class ExitCode : int {
        success = 0,
        startup_failed = 1,
        already_running = 2,
        arguments_rejected = 3,
        killed_by_watchdog = 4,
    };
}