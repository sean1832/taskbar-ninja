#pragma once

#include <string>
#include <string_view>

namespace ninja {
    /// Sets the role written on every log line of this process: app, watchdog or restore.
    /// Remarks: call once at the start of the mode, before any other log call.
    void set_log_role(std::wstring_view role);

    /// Returns the path of %APPDATA%\taskbar-ninja and makes the folder, or empty when APPDATA is unset.
    [[nodiscard]] std::wstring app_data_folder();

    /// Moves a log over 1 MiB to log.old.txt.
    void rotate_log();

    /// Appends one line of key=value fields to the log, with time, process id and role in front.
    /// Remarks: opens and closes the file each call, so several processes can share it.
    void append_log(std::wstring_view fields);
} // namespace ninja