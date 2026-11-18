#include "app.h"

#include <windows.h>

#include <cwchar>
#include <string>
#include <optional>
#include <vector>

#include "config.h"
#include "exit_code.h"
#include "log.h"
#include "monitor.h"
#include "taskbar.h"
#include "tray.h"
#include "watchdog.h"

namespace ninja {
    namespace {
        // Local\ = one per signed-in user session
        constexpr wchar_t single_instance_mutex_name[] = L"Local\\taskbar-ninja";

        // stops a watchdog that dies at once from restarting forever
        constexpr int max_watchdog_restart = 10;

        /**
         * State of the running main app.
         * @remarks Lives on the stack of run_app, and the window holds a pointer to it.
         */
        struct AppState {
            HWND window = nullptr;
            bool taskbar_hidden = false;
            HANDLE watchdog_process = nullptr;
            int watchdog_restart_count = 0;
            // Explorer broadcasts it after a restart; 0 = not registered
            UINT taskbar_created_message = 0;
        };

        AppState* app_state_of(HWND window) {
            return reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        void hide_primary_taskbar(AppState& state) {
            HWND taskbar = find_primary_taskbar();
            if (taskbar == nullptr) {
                append_log(LogLevel::warning, L"hide_skipped", {{L"message", L"explorer must be running"}});
                return;
            }

            if (!hide_taskbar(taskbar)) {
                append_log(LogLevel::error, L"hide_rejected", {{L"win32_error", std::to_wstring(GetLastError())}});
                return;
            }
            state.taskbar_hidden = true;
            append_log(LogLevel::info, L"hide");
        }

        void show_primary_taskbar(AppState& state) {
            HWND taskbar = find_primary_taskbar();
            if (taskbar == nullptr) {
                append_log(LogLevel::warning, L"show_skipped", {{L"message", L"explorer must be running"}});
                return;
            }
            show_taskbar(taskbar);
            state.taskbar_hidden = false;
            append_log(LogLevel::info, L"show");
        }

        void exit_app(HWND window) {
            restore_taskbar_and_log(L"exit");
            remove_tray_icon(window);
            // WM_DESTROY follows, which ends the message loop
            DestroyWindow(window);
        }


        void on_watchdog_ended(AppState& state) {
            DWORD exit_code = 0;
            GetExitCodeProcess(state.watchdog_process, &exit_code);
            CloseHandle(state.watchdog_process);
            state.watchdog_process = nullptr;
            append_log(LogLevel::warning, L"watchdog_ended", {
                {L"exit_code", std::to_wstring(exit_code)},
                {L"restart_count", std::to_wstring(state.watchdog_restart_count)}
            });

            if (state.watchdog_restart_count >= max_watchdog_restart) {
                append_log(LogLevel::error, L"watchdog_restart_limit", {
                    {L"max_restarts", std::to_wstring(max_watchdog_restart)}
                });
                return;
            }
            state.watchdog_restart_count += 1;
            state.watchdog_process = start_watchdog();
        }

        // runs every queued message; false on WM_QUIT
        bool pump_messages() {
            MSG message = {};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    return false;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            return true;
        }

        // Explorer restarted: empty tray, new visible taskbar
        void on_taskbar_created(AppState& state) {
            append_log(LogLevel::info, L"taskbar_created");
            if (!add_tray_icon(state.window, L"Taskbar Ninja")) {
                append_log(LogLevel::warning, L"tray_icon_rejected");
            }
            if (!state.taskbar_hidden) {
                return;
            }

            // the new taskbar is visible; hide sets this back to true on success
            state.taskbar_hidden = false;
            hide_primary_taskbar(state);
        }

        void on_tray_activated(AppState& state, LPARAM lparam) {
            // old tray protocol: lParam is the mouse message
            UINT mouse_message = static_cast<UINT>(lparam);
            bool is_click = mouse_message == WM_LBUTTONUP || mouse_message == WM_RBUTTONUP;
            if (!is_click) {
                return;
            }

            TrayCommand command = show_tray_menu(state.window, state.taskbar_hidden);
            switch (command) {
            case TrayCommand::none:
                return;
            case TrayCommand::toggle_hide:
                if (state.taskbar_hidden) {
                    show_primary_taskbar(state);
                    return;
                }
                hide_primary_taskbar(state);
                return;
            case TrayCommand::restore_and_exit:
                exit_app(state.window);
                return;
            }
        }

        LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
            // first message with our pointer: keep it on the window
            if (message == WM_NCCREATE) {
                auto* create = reinterpret_cast<LPCREATESTRUCT>(lparam);
                auto* state = static_cast<AppState*>(create->lpCreateParams);
                state->window = window;
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
                return DefWindowProcW(window, message, wparam, lparam);
            }

            AppState* state = app_state_of(window);
            if (state == nullptr) {
                return DefWindowProcW(window, message, wparam, lparam);
            }

            // registered at run time, not a switch case
            bool is_taskbar_created = state->taskbar_created_message != 0 && message == state->taskbar_created_message;
            if (is_taskbar_created) {
                on_taskbar_created(*state);
                return 0;
            }

            switch (message) {
            case tray_message:
                on_tray_activated(*state, lparam);
                return 0;
            case WM_ENDSESSION:
                // wparam TRUE = sign-out or shutdown goes ahead; the process can end any time after this
                if (wparam != FALSE) {
                    restore_taskbar_and_log(L"session_end");
                }
                return 0;
            case WM_CLOSE:
                exit_app(window);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProcW(window, message, wparam, lparam);
            }
        }

        // one line per monitor: the settings step 3 will use
        void log_monitor_settings(const std::wstring& config_path, const std::vector<Monitor>& monitors) {
            for (const Monitor& monitor : monitors) {
                std::optional<MonitorSettings> settings = read_monitor_settings(config_path, monitor.model_name);
                if (!settings.has_value()) {
                    append_log(LogLevel::warning, L"settings_rejected", {{L"monitor", monitor.model_name}});
                    continue;
                }

                std::wstring auto_hide = L"false";
                if (settings->auto_hide) {
                    auto_hide = L"true";
                }
                append_log(LogLevel::info, L"settings", {
                    {L"monitor", monitor.model_name},
                    {L"auto_hide", auto_hide},
                    {L"show_delay_ms", std::to_wstring(settings->show_delay_ms)},
                    {L"hide_delay_ms", std::to_wstring(settings->hide_delay_ms)},
                    {L"hot_zone_pixels", std::to_wstring(settings->hot_zone_pixels)},
                });
            }
        }


        // last chance before windows ends a crashed app
        LONG WINAPI on_unhandled_exception(EXCEPTION_POINTERS* exception) {
            // restore first: allocates nothing, so it works with a broken heap
            int restored_count = restore_taskbar();

            wchar_t exception_code_text[16];
            std::swprintf(exception_code_text, 16, L"0x%08lX", exception->ExceptionRecord->ExceptionCode);
            append_log(LogLevel::critical, L"crash", {
                {L"exception_code", exception_code_text},
                {L"restored_count", std::to_wstring(restored_count)}
            });

            // windows ends the process and writes its crash report
            return EXCEPTION_CONTINUE_SEARCH;
        }

        HWND create_app_window(HINSTANCE instance, AppState& state) {
            WNDCLASSEXW window_class = {};
            window_class.cbSize = sizeof(window_class);
            window_class.lpfnWndProc = window_procedure;
            window_class.hInstance = instance;
            window_class.lpszClassName = app_window_class;
            if (RegisterClassExW(&window_class) == 0) {
                return nullptr;
            }

            // top-level but never shown; a message-only window misses broadcasts like TaskbarCreated
            return CreateWindowExW(0, app_window_class, L"Taskbar Ninja", WS_OVERLAPPED, 0, 0, 0, 0,
                nullptr, nullptr, instance, &state);
        }
    } // namespace

    ExitCode run_app(HINSTANCE instance) {
        // stack kept free for on_unhandled_exception after a stack overflow
        ULONG crash_stack_bytes = 32 * 1024;
        SetThreadStackGuarantee(&crash_stack_bytes);
        SetUnhandledExceptionFilter(on_unhandled_exception);

        // Windows frees the mutex when the process ends, including crashes
        HANDLE mutex = CreateMutexW(nullptr, FALSE, single_instance_mutex_name);
        if (mutex == nullptr) {
            append_log(LogLevel::error, L"startup_failed", {
                {L"step", L"mutex"},
                {L"win32_error", std::to_wstring(GetLastError())}
            });
            return ExitCode::startup_failed;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            append_log(LogLevel::warning, L"already_running");
            CloseHandle(mutex);
            return ExitCode::already_running;
        }

        restore_taskbar_and_log(L"start");
        std::wstring config_path = write_default_config();
        std::vector<Monitor> monitors = build_monitor_list();
        log_monitor_list(monitors);
        log_monitor_settings(config_path, monitors);

        AppState state;
        state.taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");
        if (state.taskbar_created_message == 0) {
            append_log(LogLevel::warning, L"taskbar_created_rejected", {{L"win32_error", std::to_wstring(GetLastError())}});
        }

        HWND window = create_app_window(instance, state);
        if (window == nullptr) {
            append_log(LogLevel::error, L"startup_failed", {
                {L"step", L"window"},
                {L"win32_error", std::to_wstring(GetLastError())}
            });
            CloseHandle(mutex);
            return ExitCode::startup_failed;
        }

        if (!add_tray_icon(window, L"Taskbar Ninja")) {
            append_log(LogLevel::warning, L"tray_icon_rejected");
        }

        // after the window, so the watchdog can find it
        state.watchdog_process = start_watchdog();

        for (;;) {
            // no watchdog: wait on message only
            DWORD handle_count = 0;
            if (state.watchdog_process != nullptr) {
                handle_count = 1;
            }

            DWORD wait = MsgWaitForMultipleObjectsEx(handle_count, &state.watchdog_process, INFINITE, QS_ALLINPUT,
                MWMO_INPUTAVAILABLE);
            if (wait == WAIT_FAILED) {
                append_log(LogLevel::error, L"app_wait_rejected", {{L"win32_error", std::to_wstring(GetLastError())}});
                exit_app(window);
                break;
            }

            // message index = handle count
            if (wait == WAIT_OBJECT_0 + handle_count) {
                if (!pump_messages()) {
                    break;
                }
                continue;
            }
            on_watchdog_ended(state);
        }

        // the watchdog sees the app end, restores and exits by itself
        if (state.watchdog_process != nullptr) {
            CloseHandle(state.watchdog_process);
        }
        append_log(LogLevel::info, L"exit");
        CloseHandle(mutex);
        return ExitCode::success;
    }
} // namespace ninja