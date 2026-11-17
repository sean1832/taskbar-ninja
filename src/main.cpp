#include <windows.h>
#include <shellapi.h>

#include <cwchar>
#include <string_view>

#include "exit_code.h"

namespace {
    using ninja::ExitCode;

    /// Picks the mode from the commandline and executes
    ExitCode run_mode(int argument_count, wchar_t** arguments) {
        if (argument_count <= 1) {
            // TODO: return ninja::rum_app(instance)
            return ExitCode::success;
        }

        std::wstring_view mode = arguments[1];
        if (mode == L"--restore" && argument_count == 2) {
            // TODO: restore every taskbar, close the running app
            return ExitCode::success;
        }

        if (mode == L"--watchdog" && argument_count == 3) {
            wchar_t* end = nullptr;
            unsigned long app_process_id = std::wcstoul(arguments[2], &end, 10); // wcstoul: convert string to unsigned long
            // whole text must be digits, and pid 0 is never a real app
            bool is_whole_number = end != arguments[2] && *end == L'\0';
            if (!is_whole_number || app_process_id == 0) {
                return ExitCode::arguments_rejected;
            }
            // TODO: return ninja::run_watchdog(app_process_id)
            return ExitCode::success;
        }
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