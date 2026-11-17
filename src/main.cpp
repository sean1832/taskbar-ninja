#include <windows.h>
#include <shellapi.h>

#include <cwchar>
#include <string_view>

#include "exit_code.h"
#include "log.h"

namespace {
    using ninja::ExitCode;

    /// Picks the mode from the commandline and executes
    ExitCode run_mode(int argument_count, wchar_t** arguments) {
        if (argument_count <= 1) {
            ninja::set_log_role(L"app");
            ninja::rotate_log();
            ninja::append_log(L"event=start");
            // TODO: return ninja::run_app(instance)
            return ExitCode::success;
        }

        std::wstring_view mode = arguments[1];
        if (mode == L"--restore" && argument_count == 2) {
            ninja::set_log_role(L"restore");
            ninja::append_log(L"event=start");
            // TODO: restore every taskbar, close the running app
            return ExitCode::success;
        }

        if (mode == L"--watchdog" && argument_count == 3) {
            ninja::set_log_role(L"watchdog");
            wchar_t* end = nullptr;
            unsigned long app_process_id = std::wcstoul(arguments[2], &end, 10); // wcstoul: convert string to unsigned long
            // whole text must be digits, and pid 0 is never a real app
            bool is_whole_number = end != arguments[2] && *end == L'\0';
            if (!is_whole_number || app_process_id == 0) {
                // error text: <subject> must <constraint>
                ninja::append_log(L"event=arguments_rejected message=\"process id must be a whole number above 0\"");
                return ExitCode::arguments_rejected;
            }
            ninja::append_log(L"event=start");
            // TODO: return ninja::run_watchdog(app_process_id)
            return ExitCode::success;
        }

        ninja::append_log(L"event=arguments_rejected message=\"arguments must be empty, --restore or --watchdog <process id>\"");
        return ExitCode::arguments_rejected;
    }
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // CommandLineToArgvW handles quotes. lpCmdLine does not split
    int argument_count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    ExitCode exit_code = run_mode(argument_count, arguments);
    LocalFree(arguments);
    return static_cast<int>(exit_code);
}