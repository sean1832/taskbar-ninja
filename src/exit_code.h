#pragma once

namespace ninja {

    /**
     * Process exit codes for every mode.
     */
    enum class ExitCode : int {
        success = 0,            ///< normal exit
        startup_failed = 1,     ///< mutex or window not made
        already_running = 2,    ///< another app holds the single instance mutex
        arguments_rejected = 3, ///< command line not known
        killed_by_watchdog = 4, ///< app frozen or not closing, ended by the watchdog
        folder_not_opened = 5,  ///< Explorer did not open the app data folder
    };
}