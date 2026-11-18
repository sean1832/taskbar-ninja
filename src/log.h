#pragma once

#include <string>
#include <string_view>
#include <initializer_list>

namespace ninja {
    /**
     * Severity of one log line.
     */
    enum class LogLevel {
        info,     ///< normal event
        warning,  ///< odd, but nothing failed
        error,    ///< handled failure
        critical, ///< unexpected, unhandled crash
    };

    /**
     * One key=value pair on a log line.
     */
    struct LogField {
        std::wstring_view key; ///< snake_case name
        std::wstring value;    ///< text, quoted by append_log when needed
    };

    /**
     * Sets the role written on every log line of this process.
     * @param role app, watchdog or restore
     * @remarks Call once at the start of the mode, before any other log call.
     */
    void set_log_role(std::wstring_view role);

    /**
     * Makes the taskbar-ninja folder in APPDATA.
     * @return folder path, or empty when APPDATA is unset or too long
     */
    [[nodiscard]] std::wstring app_data_folder();

    /**
     * Moves a log over 1 MiB to log.old.txt.
     * @remarks Replaces an older log.old.txt.
     */
    void rotate_log();

    /**
     * Appends one line to the log: time, level, role[pid], event, then the fields as key=value.
     * @param level severity
     * @param event event name, as a snake_case word
     * @param fields extra key=value pairs, in order
     * @remarks Quotes a value that is empty or has a space, quote, equals sign, tab or line break.
     */
    void append_log(LogLevel level, std::wstring_view event, std::initializer_list<LogField> fields = {});
} // namespace ninja