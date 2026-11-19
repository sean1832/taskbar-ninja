#include "app.h"

#include <windows.h>

#include <cwchar>
#include <string>
#include <optional>
#include <vector>

#include "config.h"
#include "exit_code.h"
#include "hold.h"
#include "log.h"
#include "monitor.h"
#include "panel.h"
#include "taskbar.h"
#include "tray.h"
#include "visibility.h"
#include "watchdog.h"
#include "workarea.h"

namespace ninja {
    namespace {
        // Local\ = one per signed-in user session
        constexpr wchar_t single_instance_mutex_name[] = L"Local\\taskbar-ninja";

        // stops a watchdog that dies at once from restarting forever
        constexpr int max_watchdog_restart = 10;

        constexpr UINT_PTR mouse_timer_id = 1;
        constexpr UINT mouse_timer_interval_ms = 50;

        constexpr UINT_PTR rebuild_timer_id = 2;
        // editors save in steps; Explorer makes new taskbars a little after a display change
        constexpr UINT rebuild_delay_ms = 1000;

        constexpr UINT_PTR work_area_timer_id = 3;
        // explorer finishes its own change first. cap a fight over the taskbar at once over a second
        constexpr UINT work_area_check_delay_ms = 1000;

        // one monitor, its settings, and whether its taskbar is hidden now
        struct MonitorState {
            Monitor monitor;
            MonitorSettings settings;
            Visibility visibility;
            bool taskbar_hidden = false;
            bool work_area_covered = false;
        };

        /**
         * State of the running main app.
         * @remarks Lives on the stack of run_app, and the window holds a pointer to it.
         */
        struct AppState {
            HWND window = nullptr;
            // tray switch; off = every taskbar shows
            bool hiding_enabled = true;
            std::wstring config_path;
            // nullptr = no live reload
            HANDLE config_watch = nullptr;
            // last seen write time of the config file; 0 = file missing
            ULONGLONG config_write_time = 0;
            std::vector<MonitorState> monitors;
            bool mouse_timer_running = false;
            HANDLE watchdog_process = nullptr;
            int watchdog_restart_count = 0;
            // Explorer broadcasts it after a restart; 0 = not registered
            UINT taskbar_created_message = 0;
            HWND foreground_window = nullptr; // front window. nullptr = none
            HMONITOR shell_monitor = nullptr; // monitor whose taskbar the front window keeps shown. nullptr = none
            HMONITOR panel_monitor = nullptr; // monitor whose taskbar the front panel keeps shown. nullptr = none
            int open_menu_count = 0; // popup menu open now
            HMONITOR menu_monitor = nullptr; // monitor of the last one opened
            HWINEVENTHOOK window_event_hook = nullptr; // foreground and menu events. nullptr = not hooked
            std::vector<PanelWindow> panels; // Start, Search and other panels that follow the visible edge
        };

        AppState* app_state_of(HWND window) {
            return reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        // settings of the same monitor in the old list; built-in values for a new monitor
        MonitorSettings find_previous_settings(const std::vector<MonitorState>& previous, const std::wstring& model_name) {
            for (const MonitorState& monitor_state : previous) {
                if (monitor_state.monitor.model_name == model_name) {
                    return monitor_state.settings;
                }
            }
            return {};
        }

        // rejected settings: the last good ones of that monitor
        std::vector<MonitorState> build_monitor_states(const std::wstring& config_path, const std::vector<MonitorState>& previous) {
            std::vector<Monitor> monitors = build_monitor_list();
            log_monitor_list(monitors);

            std::vector<MonitorState> states;
            for (const Monitor& monitor : monitors) {
                MonitorState monitor_state;
                monitor_state.monitor = monitor;
                // real state: a rebuild can find a taskbar that this app hid before
                monitor_state.taskbar_hidden = monitor.taskbar != nullptr && is_taskbar_hidden(monitor.taskbar);
                monitor_state.work_area_covered = monitor.taskbar != nullptr && is_work_area_covered(monitor.handle);
                std::optional<MonitorSettings> settings = read_monitor_settings(config_path, monitor.model_name);
                if (settings.has_value()) {
                    monitor_state.settings = *settings;
                } else {
                    monitor_state.settings = find_previous_settings(previous, monitor.model_name);
                    append_log(LogLevel::warning, L"settings_rejected", {{L"monitor", monitor.model_name}});
                }
                states.push_back(monitor_state);
            }
            return states;
        }

        // no change when the taskbar is already in the wanted state
        void set_taskbar_hidden(MonitorState& monitor_state, bool should_hide) {
            HWND taskbar = monitor_state.monitor.taskbar;
            if (taskbar == nullptr) {
                return;
            }
            if (should_hide == monitor_state.taskbar_hidden) {
                return;
            }

            const std::wstring& model_name = monitor_state.monitor.model_name;
            if (!should_hide) {
                show_taskbar(taskbar);
                // a window that fills the monitor would sit above the bar
                if (monitor_state.work_area_covered) {
                    raise_taskbar(taskbar);
                }
                monitor_state.taskbar_hidden = false;
                append_log(LogLevel::info, L"show", {{L"monitor", model_name}});
                return;
            }

            if (!hide_taskbar(taskbar)) {
                append_log(LogLevel::error, L"hide_rejected", {
                    {L"monitor", model_name},
                    {L"win32_error", std::to_wstring(GetLastError())}
                });
                return;
            }
            monitor_state.taskbar_hidden = true;
            append_log(LogLevel::info, L"hide", {{L"monitor", model_name}});
        }

        // no change when the work area is already in the wanted state; resets only a monitor this app covered
        void set_work_area_covered(MonitorState& monitor_state, bool should_cover) {
            HWND taskbar = monitor_state.monitor.taskbar;
            if (taskbar == nullptr) {
                return;
            }
            if (should_cover == monitor_state.work_area_covered) {
                return;
            }

            // wanted state even on failure: one log line (not one per tick)
            monitor_state.work_area_covered = should_cover;
            HMONITOR monitor = monitor_state.monitor.handle;
            const std::wstring& model_name = monitor_state.monitor.model_name;
            if (!should_cover) {
                if (!reset_work_area(monitor, taskbar)) {
                    append_log(LogLevel::error, L"work_area_reset_rejected", {{L"monitor", model_name}});
                    return;
                }
                append_log(LogLevel::info, L"work_area_reset", {{L"monitor", model_name}});
                return;
            }

            if (!cover_work_area(monitor)) {
                append_log(LogLevel::error, L"work_area_cover_rejected", {
                    {L"monitor", model_name},
                    {L"win32_error", std::to_wstring(GetLastError())}
                });
                return;
            }
            append_log(LogLevel::info, L"work_area_covered", {{L"monitor", model_name}});
        }

        // true when hiding is on and at least one monitor auto-hides a taskbar
        bool should_read_mouse(const AppState& state) {
            if (!state.hiding_enabled) {
                return false;
            }
            for (const MonitorState& monitor_state : state.monitors) {
                if (monitor_state.settings.auto_hide && monitor_state.monitor.taskbar != nullptr) {
                    return true;
                }
            }

            return false;
        }

        // starts or stops the timer; no change when it is already right
        void apply_mouse_timer(AppState& state) {
            bool should_run = should_read_mouse(state);
            if (should_run == state.mouse_timer_running) {
                return;
            }

            if (!should_run) {
                KillTimer(state.window, mouse_timer_id);
                state.mouse_timer_running = false;
                append_log(LogLevel::info, L"mouse_timer_stopped");
                return;
            }

            // coalescable: Windows may wake us together with other timers
            UINT_PTR timer = SetCoalescableTimer(state.window, mouse_timer_id, mouse_timer_interval_ms, nullptr,
                TIMERV_DEFAULT_COALESCING);
            if (timer == 0) {
                append_log(LogLevel::error, L"mouse_timer_rejected", {{L"win32_error", std::to_wstring(GetLastError())}});
                return;
            }
            state.mouse_timer_running = true;
            append_log(LogLevel::info, L"mouse_timer_started");
        }

        // state of the monitor a window is on, otherwise nullptr
        MonitorState* find_monitor_state(AppState& state, HWND window) {
            HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
            for (MonitorState& monitor_state : state.monitors) {
                if (monitor_state.monitor.handle == monitor) {
                    return &monitor_state;
                }
            }
            return nullptr;
        }

        // moves each panel to the visible edge of its monitor: the shown taskbar, or the monitor edge
        void align_panels(AppState& state) {
            for (const PanelWindow& panel : state.panels) {
                // Start or Search process ended: the next list after a foreground change finds the new window
                if (!IsWindow(panel.window)) {
                    continue;
                }
                MonitorState* monitor_state = find_monitor_state(state, panel.window);
                if (monitor_state == nullptr || monitor_state->monitor.taskbar == nullptr) {
                    continue;
                }
                RECT taskbar_bounds_pixels = {};
                GetWindowRect(monitor_state->monitor.taskbar, &taskbar_bounds_pixels);
                bool is_taskbar_shown = !monitor_state->taskbar_hidden;
                align_panel(panel, monitor_state->monitor.bounds_pixels, taskbar_bounds_pixels, is_taskbar_shown);
            }
        }


        // covers the work area of each auto-hiding monitor that wants it, hides each auto-hide taskbar the hot zone
        // does not want shown, shows the others, then fixes the timer
        void apply_hiding(AppState& state) {
            for (MonitorState& monitor_state : state.monitors) {
                bool is_auto_hiding = state.hiding_enabled && monitor_state.settings.auto_hide;
                // cover first, so a taskbar that shows below knows to go topmost
                bool should_cover = is_auto_hiding && monitor_state.settings.cover_taskbar_area;
                set_work_area_covered(monitor_state, should_cover);

                bool should_hide = is_auto_hiding && !monitor_state.visibility.is_shown;
                set_taskbar_hidden(monitor_state, should_hide);
            }
            // after the taskbar, panels follow the new state
            align_panels(state);
            apply_mouse_timer(state);
        }

        void rebuild_monitors(AppState& state) {
            state.monitors = build_monitor_states(state.config_path, state.monitors);
            apply_hiding(state);
        }

        // a new event during the delay starts the delay again
        void start_rebuild_timer(AppState& state) {
            if (SetTimer(state.window, rebuild_timer_id, rebuild_delay_ms, nullptr) == 0) {
                append_log(LogLevel::error, L"rebuild_timer_rejected", {{L"win32_error", std::to_wstring(GetLastError())}});
            }
        }

        void on_rebuild_timer(AppState& state) {
            // SetTimer repeats; one rebuild per burst of events
            KillTimer(state.window, rebuild_timer_id);
            rebuild_monitors(state);
        }

        // a new change during the delay starts the delay again
        void start_work_area_timer(AppState& state) {
            if (SetTimer(state.window, work_area_timer_id, work_area_check_delay_ms, nullptr) == 0) {
                append_log(LogLevel::error, L"work_area_timer_rejected", {{L"win32_error", std::to_wstring(GetLastError())}});
            }
        }

        // cover again each monitor whose strip explorer took back
        void on_work_area_timer(AppState& state) {
            // SetTimer repeats. one check per burst of changes
            KillTimer(state.window, work_area_timer_id);
            for (MonitorState& monitor_state : state.monitors) {
                // only a lost cover. a monitor this app never covered stay as is
                if (monitor_state.work_area_covered && !is_work_area_covered(monitor_state.monitor.handle)) {
                    monitor_state.work_area_covered = false;
                    append_log(LogLevel::info, L"work_area_lost", {{L"monitor", monitor_state.monitor.model_name}});
                }
            }
            // covers each monitor that still wants it
            apply_hiding(state);
        }

        // any file in the config folder changed, log.txt included
        void on_config_folder_changed(AppState& state) {
            // watch again first, so a change during the work below still signals
            if (!FindNextChangeNotification(state.config_watch)) {
                // a handle left signalled would wake the loop forever
                append_log(LogLevel::error, L"config_watch_rejected", {{L"win32_error", std::to_wstring(GetLastError())}});
                FindCloseChangeNotification(state.config_watch);
                state.config_watch = nullptr;
                return;
            }

            // log lines change the folder too; only a new config write time counts
            ULONGLONG write_time = read_config_write_time(state.config_path);
            if (write_time == state.config_write_time) {
                return;
            }
            state.config_write_time = write_time;
            append_log(LogLevel::info, L"config_changed");
            start_rebuild_timer(state);
        }

        // reads the mouse and the holds, then hides or shows each taskbar
        void apply_visibility(AppState& state) {
            POINT cursor_pixels = {};
            // fails on the secure desktop (lock screen, UAC prompt). next tick tries again
            if (!GetCursorPos(&cursor_pixels)) {
                return;
            }

            HMONITOR popup_monitor = resolve_popup_monitor(cursor_pixels);
            bool is_menu_open = state.open_menu_count > 0;
            ULONGLONG now_ms = GetTickCount64();
            for (MonitorState& monitor_state : state.monitors) {
                HMONITOR monitor = monitor_state.monitor.handle;
                Hold hold;
                hold.is_over_popup = popup_monitor == monitor;
                hold.is_menu_open = is_menu_open && state.menu_monitor == monitor;
                hold.is_panel_active = state.panel_monitor == monitor;
                HWND taskbar = monitor_state.monitor.taskbar;
                // a monitor without a taskbar never matches, even before the first front window event
                bool is_taskbar_active = taskbar != nullptr && state.foreground_window == taskbar;
                hold.is_shell_active = state.shell_monitor == monitor || is_taskbar_active;

                RECT taskbar_bounds_pixels = {};
                // fails for a missing taskbar: empty area, so only the hot zone counts
                GetWindowRect(monitor_state.monitor.taskbar, &taskbar_bounds_pixels);
                monitor_state.visibility = apply_mouse(monitor_state.visibility, monitor_state.settings,
                    monitor_state.monitor.bounds_pixels, taskbar_bounds_pixels, cursor_pixels, hold, now_ms);
            }
            apply_hiding(state);
        }

        // WinEvent callbacks carry no user data; set while the hook lives
        AppState* hooked_state = nullptr;

        void on_foreground_changed(AppState& state, HWND window) {
            state.foreground_window = window;
            state.shell_monitor = resolve_shell_monitor(window);
            state.panel_monitor = nullptr;
            // panel only keep the taskbar. they follow the visible edge instead
            if (is_panel(window)) {
                state.panel_monitor = state.shell_monitor;
                state.shell_monitor = nullptr;
            }

            // finds Start or Search again after their process restarts
            state.panels = build_panel_list();
            // a missed menu end cannot hold forever
            state.open_menu_count = 0;
            apply_visibility(state);
        }

        void on_menu_opened(AppState& state, HWND menu) {
            state.open_menu_count += 1;
            if (menu != nullptr) {
                state.menu_monitor = MonitorFromWindow(menu, MONITOR_DEFAULTTONEAREST);
            }
            apply_visibility(state);
        }

        // a submenu closes on its own. the menu it came from stays open
        void on_menu_closed(AppState& state) {
            if (state.open_menu_count > 0) {
                state.open_menu_count -= 1;
            }
            apply_visibility(state);
        }

        void CALLBACK on_window_event(HWINEVENTHOOK, DWORD event, HWND window, LONG, LONG, DWORD, DWORD) {
            if (hooked_state == nullptr) {
                return;
            }

            switch (event) {
            case EVENT_SYSTEM_FOREGROUND:
                on_foreground_changed(*hooked_state, window);
                return;
            case EVENT_SYSTEM_MENUPOPUPSTART:
                on_menu_opened(*hooked_state, window);
                return;
            case EVENT_SYSTEM_MENUPOPUPEND:
                on_menu_closed(*hooked_state);
                return;
            default:
                // menu bar start and end sit inside the hooked range
                return;
            }
        }

        void start_window_events(AppState& state) {
            hooked_state = &state;
            // one range: foreground (3) to popup menu end (7). out of context, so nothing loads into other processes
            // own process included, so the tray menu of this app holds too
            state.window_event_hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_MENUPOPUPEND, nullptr,
                on_window_event, 0, 0, WINEVENT_OUTOFCONTEXT);
            if (state.window_event_hook == nullptr) {
                append_log(LogLevel::warning, L"window_event_hook_rejected");
            }
        }

        void stop_window_events(AppState& state) {
            if (state.window_event_hook != nullptr) {
                UnhookWinEvent(state.window_event_hook);
                state.window_event_hook = nullptr;
            }
            hooked_state = nullptr;
        }

        void exit_app(AppState& state) {
            stop_window_events(state); // no event may hide a taskbar after the restore
            restore_taskbar_and_log(L"exit");
            remove_tray_icon(state.window);
            // WM_DESTROY follows, which ends the message loop
            DestroyWindow(state.window);
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

            // new taskbar windows, all visible; the old handles are gone
            rebuild_monitors(state);
        }

        void on_tray_activated(AppState& state, LPARAM lparam) {
            // old tray protocol: lParam is the mouse message
            UINT mouse_message = static_cast<UINT>(lparam);
            bool is_click = mouse_message == WM_LBUTTONUP || mouse_message == WM_RBUTTONUP;
            if (!is_click) {
                return;
            }

            TrayCommand command = show_tray_menu(state.window, state.hiding_enabled);
            switch (command) {
            case TrayCommand::none:
                return;
            case TrayCommand::toggle_hide:
                state.hiding_enabled = !state.hiding_enabled;
                apply_hiding(state);
                return;
            case TrayCommand::restore_and_exit:
                exit_app(state);
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
            case WM_TIMER:
                if (wparam == mouse_timer_id) {
                    apply_visibility(*state);
                }
                if (wparam == rebuild_timer_id) {
                    on_rebuild_timer(*state);
                }
                if (wparam == work_area_timer_id) {
                    on_work_area_timer(*state);
                }
                return 0;
            case WM_SETTINGCHANGE:
                // explorer tells every window after it takes the strip back
                if (wparam == SPI_SETWORKAREA) {
                    start_work_area_timer(*state);
                }
            case WM_DISPLAYCHANGE:
                // monitor added or removed, or a new resolution
                append_log(LogLevel::info, L"display_changed");
                start_rebuild_timer(*state);
                return 0;
            case WM_ENDSESSION:
                // wparam TRUE = sign-out or shutdown goes ahead; the process can end any time after this
                if (wparam != FALSE) {
                    restore_taskbar_and_log(L"session_end");
                }
                return 0;
            case WM_CLOSE:
                exit_app(*state);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProcW(window, message, wparam, lparam);
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
        AppState state;
        state.config_path = write_default_config();
        state.monitors = build_monitor_states(state.config_path, {});
        // watch first, then read the time, so a save between the two still signals
        state.config_watch = start_config_watch(state.config_path);
        state.config_write_time = read_config_write_time(state.config_path);

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
        start_window_events(state);
        state.panels = build_panel_list();
        // after the watchdog, so a crash from here on still restores
        apply_hiding(state);

        for (;;) {
            // missing handles are left out, so the list has no gaps
            HANDLE handles[2] = {};
            DWORD handle_count = 0;
            if (state.config_watch != nullptr) {
                handles[handle_count] = state.config_watch;
                handle_count += 1;
            }
            if (state.watchdog_process != nullptr) {
                handles[handle_count] = state.watchdog_process;
                handle_count += 1;
            }

            DWORD wait = MsgWaitForMultipleObjectsEx(handle_count, handles, INFINITE, QS_ALLINPUT,
                MWMO_INPUTAVAILABLE);
            if (wait == WAIT_FAILED) {
                append_log(LogLevel::error, L"app_wait_rejected", {{L"win32_error", std::to_wstring(GetLastError())}});
                exit_app(state);
                break;
            }

            // message index = handle count
            if (wait == WAIT_OBJECT_0 + handle_count) {
                if (!pump_messages()) {
                    break;
                }
                continue;
            }

            HANDLE signalled = handles[wait - WAIT_OBJECT_0];
            if (signalled == state.config_watch) {
                on_config_folder_changed(state);
                continue;
            }
            on_watchdog_ended(state);
        }

        // the watchdog sees the app end, restores and exits by itself
        if (state.watchdog_process != nullptr) {
            CloseHandle(state.watchdog_process);
        }
        if (state.config_watch != nullptr) {
            FindCloseChangeNotification(state.config_watch);
        }
        append_log(LogLevel::info, L"exit");
        CloseHandle(mutex);
        return ExitCode::success;
    }
} // namespace ninja