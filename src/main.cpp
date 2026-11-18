#include <windows.h>
#include <shellapi.h>

#include <cwchar>
#include <string_view>

#include "app.h"
#include "exit_code.h"
#include "log.h"
#include "taskbar.h"
#include "watchdog.h"

namespace {
    using ninja::ExitCode;

    /// Picks the mode from the commandline and executes
    ExitCode run_mode(HINSTANCE instance, int argument_count, wchar_t** arguments) {
        if (argument_count <= 1) {
            ninja::set_log_role(L"app");
            ninja::rotate_log();
            ninja::append_log(ninja::LogLevel::info, L"start");
            return ninja::run_app(instance);
        }

        std::wstring_view mode = arguments[1];
        if (mode == L"--restore" && argument_count == 2) {
            ninja::set_log_role(L"restore");
            ninja::append_log(ninja::LogLevel::info, L"start");
            // restore first: works even if app is frozen
            ninja::restore_taskbar_and_log(L"command");
            // close a running app, so it cannot hide again
            HWND app_window = FindWindowW(ninja::app_window_class, nullptr);
            if (app_window != nullptr) {
                PostMessageW(app_window, WM_CLOSE, 0, 0);
                ninja::append_log(ninja::LogLevel::info, L"app_close_requested");
            }
            return ExitCode::success;
        }

        if (mode == L"--watchdog" && argument_count == 3) {
            ninja::set_log_role(L"watchdog");
            wchar_t* end = nullptr;
            unsigned long app_process_id = std::wcstoul(arguments[2], &end, 10); // wcstoul: convert string to unsigned long
            // whole text must be digits, and pid 0 is never a real app
            bool is_whole_number = end != arguments[2] && *end == L'\0';
            if (!is_whole_number || app_process_id == 0) {
                ninja::append_log(ninja::LogLevel::error, L"arguments_rejected", {
                    {L"message", L"process id must be a whole number above 0"},
                });
                return ExitCode::arguments_rejected;
            }
            ninja::append_log(ninja::LogLevel::info, L"start");
            return ninja::run_watchdog(app_process_id);
        }
        ninja::append_log(ninja::LogLevel::error, L"arguments_rejected", {
            {L"message", L"arguments must be empty, --restore or --watchdog <process id>"},
        });
        return ExitCode::arguments_rejected;
    }
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // CommandLineToArgvW handles quotes. lpCmdLine does not split
    int argument_count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    ExitCode exit_code = run_mode(instance, argument_count, arguments);
    LocalFree(arguments);
    return static_cast<int>(exit_code);
}