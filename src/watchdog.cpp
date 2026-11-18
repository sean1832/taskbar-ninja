#include "watchdog.h"

#include <windows.h>

#include <string>

#include "app.h"
#include "exit_code.h"
#include "log.h"
#include "taskbar.h"

namespace ninja {
    namespace {
        constexpr DWORD freeze_check_interval_milliseconds = 1000;
        constexpr UINT  freeze_timeout_milliseconds = 3000;
        constexpr DWORD close_wait_milliseconds = 1000;

        // Ctrl + Alt + Shift + T
        constexpr int restore_hotkey_id = 1;
        constexpr UINT restore_hotkey_modifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT;
        constexpr UINT restore_hotkey_key = 'T';

        /**
         * Why the watchdog stopped watching.
         */
        enum class WatchEnd {
            app_ended,
            app_frozen,
            hotkey_pressed,
            wait_failed,
        };

        std::wstring_view watch_end_name(WatchEnd end) {
            switch (end) {
            case WatchEnd::app_ended:
                return L"app_ended";
            case WatchEnd::app_frozen:
                return L"app_frozen";
            case WatchEnd::hotkey_pressed:
                return L"hotkey_pressed";
            case WatchEnd::wait_failed:
                return L"wait_failed";
            }
            return L"unknown"; // MSVC: not all path return
        }

        // the app window of current pid only, not of another copy
        HWND find_app_window(DWORD app_process_id) {
            HWND window = FindWindowW(app_window_class, nullptr);
            if (window == nullptr) {
                return nullptr;
            }

            DWORD window_process_id = 0;
            GetWindowThreadProcessId(window, &window_process_id);
            if (window_process_id != app_process_id) {
                return nullptr;
            }
            return window;
        }

        bool is_app_frozen(HWND app_window) {
            // no window yet, or gone: the process wait covers a gone app
            if (app_window == nullptr) {
                return false;
            }

            // SMTO_ABORTIFHUNG: give up at once when Windows already marks the window hung
            DWORD_PTR result = 0;
            UINT flags = SMTO_BLOCK | SMTO_ABORTIFHUNG;
            LRESULT answered = SendMessageTimeoutW(app_window, WM_NULL, 0, 0, flags, freeze_timeout_milliseconds, &result);
            if (answered != 0) {
                return false;
            }

            // window destroyed during the wait = app ending, not frozen
            return IsWindow(app_window) != FALSE;
        }

        // empties the queue; true when the restore hotkey was in it
        bool take_restore_hotkey_press() {
            bool pressed = false;
            MSG message = {};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                const bool is_restore_hotkey = message.message == WM_HOTKEY && message.wParam == restore_hotkey_id;
                if (is_restore_hotkey) {
                    pressed = true;
                }
            }
            return pressed;
        }

        WatchEnd watch_app(HANDLE app_process, DWORD app_process_id) {
            HWND app_window = nullptr;
            for (;;) {
                // wakes on: app ended, a message (hotkey), or 1s timeout
                DWORD wait = MsgWaitForMultipleObjects(1, &app_process, FALSE, freeze_check_interval_milliseconds, QS_ALLINPUT);
                if (wait == WAIT_OBJECT_0) {
                    return WatchEnd::app_ended;
                }
                if (wait == WAIT_FAILED) {
                    append_log(LogLevel::error, L"watch_wait_rejected", {{L"win32_error", std::to_wstring(GetLastError())}});
                    return WatchEnd::wait_failed;
                }
                if (wait == WAIT_OBJECT_0 + 1) {
                    if (take_restore_hotkey_press()) {
                        return WatchEnd::hotkey_pressed;
                    }
                    continue;
                }

                // WAIT_TIMEOUT: freeze check
                if (!IsWindow(app_window)) {
                    app_window = find_app_window(app_process_id);
                }
                if (is_app_frozen(app_window)) {
                    return WatchEnd::app_frozen;
                }
            }
        }

        // asks the app to close, then ends it when it does not
        void close_app(HANDLE app_process, DWORD app_process_id) {
            HWND app_window = find_app_window(app_process_id);
            if (app_window != nullptr) {
                PostMessageW(app_window, WM_CLOSE, 0, 0 );
            }

            DWORD wait = WaitForSingleObject(app_process, close_wait_milliseconds);
            if (wait == WAIT_OBJECT_0) {
                return;
            }
            TerminateProcess(app_process, static_cast<UINT>(ExitCode::killed_by_watchdog));
            append_log(LogLevel::error, L"app_killed", {{L"reason", L"close_timeout"}});
        }
    } // namespace

    HANDLE start_watchdog() {
        wchar_t exe_path[MAX_PATH];
        DWORD length = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            append_log(LogLevel::error, L"watchdog_not_started", {{L"message", L"exe path must fit in 260 characters"}});
            return nullptr;
        }

        std::wstring command_line = L"\"";
        command_line += exe_path;
        command_line += L"\" --watchdog ";
        command_line += std::to_wstring(GetCurrentProcessId());

        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        // no busy cursor: the watchdog never shows a window
        startup.dwFlags = STARTF_FORCEOFFFEEDBACK;
        PROCESS_INFORMATION process = {};

        // leave the job (process group), so stopping the app's job does not end the watchdog too
        BOOL started = CreateProcessW(exe_path, command_line.data(), nullptr, nullptr, FALSE, CREATE_BREAKAWAY_FROM_JOB,
            nullptr, nullptr, &startup, &process);
        std::wstring left_job = L"yes";
        if (!started) {
            // the job does not allow leaving: start inside it
            left_job = L"no";
            started = CreateProcessW(exe_path, command_line.data(), nullptr, nullptr, FALSE, 0,
                nullptr, nullptr, &startup, &process);
        }
        if (!started) {
            append_log(LogLevel::error, L"watchdog_not_started", {{L"win32_error", std::to_wstring(GetLastError())}});
            return nullptr;
        }

        CloseHandle(process.hThread);
        append_log(LogLevel::info, L"watchdog_started", {
            {L"watchdog_pid", std::to_wstring(process.dwProcessId)},
            {L"left_job", left_job}
        });
        return process.hProcess;
    }

    ExitCode run_watchdog(DWORD app_process_id) {
        // SYNCHRONIZE: wait on it, PROCESS_TERMINATE: end it when frozen
        HANDLE app_process= OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, app_process_id);
        if (app_process == nullptr) {
            append_log(LogLevel::warning, L"app_not_found", {
                {L"app_pid", std::to_wstring(app_process_id)},
                {L"win32_error", std::to_wstring(GetLastError())}
            });
            restore_taskbar_and_log(L"app_not_found");
            return ExitCode::success;
        }

        // hotkey on this thread's queue, so it works while the app is frozen
        bool hotkey_registered = RegisterHotKey(nullptr, restore_hotkey_id, restore_hotkey_modifiers, restore_hotkey_key);
        if (!hotkey_registered) {
            append_log(LogLevel::warning, L"hotkey_rejected", {
            {L"hotkey", L"Ctrl+Alt+Shift+T"},
            {L"win32_error", std::to_wstring(GetLastError())},
            });
        }

        WatchEnd end = watch_app(app_process, app_process_id);
        append_log(LogLevel::info, L"watch_end", {{L"reason", std::wstring(watch_end_name(end))}});

        switch (end) {
        case WatchEnd::app_ended:
        case WatchEnd::wait_failed:
            break;
        case WatchEnd::app_frozen:
            TerminateProcess(app_process, static_cast<UINT>(ExitCode::killed_by_watchdog));
            append_log(LogLevel::error, L"app_killed", {{L"reason", L"frozen"}});
            break;
        case WatchEnd::hotkey_pressed:
            close_app(app_process, app_process_id);
            break;
        }

        restore_taskbar_and_log(watch_end_name(end));
        if (hotkey_registered) {
            UnregisterHotKey(nullptr, restore_hotkey_id);
        }
        CloseHandle(app_process);
        append_log(LogLevel::info, L"exit");
        return ExitCode::success;
    }
} // namespace ninja