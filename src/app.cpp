#include "app.h"

#include <windows.h>

#include <string>

#include "exit_code.h"
#include "log.h"
#include "taskbar.h"
#include "tray.h"

namespace ninja {
    namespace {
        // Local\ = one per signed-in user session
        constexpr wchar_t single_instance_mutex_name[] = L"Local\\taskbar-ninja";

        /// State of the running main app. Lives on the stack of run_app; the window holds a pointer to it.
        struct AppState {
            HWND window = nullptr;
            bool taskbar_hidden = false;
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

            switch (message) {
            case tray_message:
                on_tray_activated(*state, lparam);
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

        AppState state;
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

        // > 0: GetMessageW returns -1 on error an d0 on WM_QUIT
        MSG message = {};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        append_log(LogLevel::info, L"exit");
        CloseHandle(mutex);
        return ExitCode::success;
    }
} // namespace ninja