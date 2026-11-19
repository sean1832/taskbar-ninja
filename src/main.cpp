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
    using ninja::LogLevel;

    /**
     * Picks the mode from the command line and runs it.
     * @param instance module handle from wWinMain
     * @param argument_count number of command-line arguments, including the exe path
     * @param arguments command-line arguments from CommandLineToArgvW
     * @return exit code of the mode, or arguments_rejected
     */
    ExitCode run_mode(HINSTANCE instance, int argument_count, wchar_t** arguments) {
        if (argument_count <= 1) {
            ninja::set_log_role(L"app");
            ninja::rotate_log();
            ninja::append_log(LogLevel::info, L"start");
            return ninja::run_app(instance);
        }

        std::wstring_view mode = arguments[1];
        if (mode == L"--restore" && argument_count == 2) {
            ninja::set_log_role(L"restore");
            ninja::append_log(LogLevel::info, L"start");
            // restore first: works even if app is frozen
            ninja::restore_taskbar_and_log(L"command");
            // close a running app, so it cannot hide again
            HWND app_window = FindWindowW(ninja::app_window_class, nullptr);
            if (app_window != nullptr) {
                PostMessageW(app_window, WM_CLOSE, 0, 0);
                ninja::append_log(LogLevel::info, L"app_close_requested");
            }
            return ExitCode::success;
        }

        if (mode == L"--configs" && argument_count == 2) {
            ninja::set_log_role(L"configs");
            ninja::append_log(LogLevel::info, L"start");
            std::wstring folder = ninja::app_data_folder();
            if (folder.empty()) {
                ninja::append_log(LogLevel::error, L"folder_not_opened", {{L"message", L"APPDATA must be set"}});
                return ExitCode::folder_not_opened;
            }

            // open folder in explorer
            HINSTANCE result = ShellExecuteW(
                nullptr,                // Parent window handle
                L"open",                // Operation to perform
                folder.c_str(), // Path to the folder
                nullptr,                // Parameters (not needed for folders)
                nullptr,                // Default working directory
                SW_SHOWNORMAL           // Show window normally
            );
            auto result_code = reinterpret_cast<INT_PTR>(result);
            if (result_code <= 32) {
                ninja::append_log(LogLevel::error, L"folder_not_opened", {{L"shell_error", std::to_wstring(result_code)}});
                return ExitCode::folder_not_opened;
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
                ninja::append_log(LogLevel::error, L"arguments_rejected", {
                    {L"message", L"process id must be a whole number above 0"},
                });
                return ExitCode::arguments_rejected;
            }
            ninja::append_log(LogLevel::info, L"start");
            return ninja::run_watchdog(app_process_id);
        }
        ninja::append_log(LogLevel::error, L"arguments_rejected", {
            {L"message", L"arguments must be empty, --restore, --configs or --watchdog <process id>"},
        });
        return ExitCode::arguments_rejected;
    }
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // real pixels for the mouse, monitors and windows, on every monitor scale
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // CommandLineToArgvW handles quotes. lpCmdLine does not split
    int argument_count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    ExitCode exit_code = run_mode(instance, argument_count, arguments);
    LocalFree(arguments);
    return static_cast<int>(exit_code);
}