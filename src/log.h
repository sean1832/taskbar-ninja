#pragma once

#include <string>
#include <string_view>
#include <initializer_list>

namespace ninja {
    /// Severity
    enum class LogLevel {
        info,  // normal event
        warning,
        error, // failure, handled errors
        critical, // unexpected, unhandled crash
    };

    /// key/value pair for log info
    struct LogField {
        std::wstring_view key;
        std::wstring value;
    };

    /// Sets the role written on every log line of this process: app, watchdog or restore.
    /// Remarks: call once at the start of the mode, before any other log call.
    void set_log_role(std::wstring_view role);

    /// Returns the path of %APPDATA%\taskbar-ninja and makes the folder, or empty when APPDATA is unset.
    [[nodiscard]] std::wstring app_data_folder();

    /// Moves a log over 1 MiB to log.old.txt.
    void rotate_log();

    /// Appends one line to the log
    void append_log(LogLevel level, std::wstring_view event, std::initializer_list<LogField> fields = {});
} // namespace ninja