#include "panel.h"

#include <windows.h>
#include <dwmapi.h>

#include <cwchar>
#include <vector>

#include "edge.h"

namespace ninja {
    namespace {
        // one kind of shell panel, found by its process and window class
        struct PanelKind {
            const wchar_t* process_name;
            const wchar_t* class_name;
            bool should_move_while_open; // the panel puts itself back when it opens
        };

        // 24H2. add a row to move one more panel
        constexpr PanelKind panel_kinds[] = {
            {L"StartMenuExperienceHost.exe", L"Windows.UI.Core.CoreWindow", false}, // start
            {L"SearchHost.exe", L"Windows.UI.Core.CoreWindow", true},               // search
        };

        void read_process_name(HWND window, wchar_t* name, DWORD name_length) {
            name[0] = L'\0';
            DWORD process_id = 0;
            GetWindowThreadProcessId(window, &process_id);
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
            if (process == nullptr) {
                return;
            }
            wchar_t path[MAX_PATH] = {};
            DWORD path_length = MAX_PATH;
            if (QueryFullProcessImageNameW(process, 0, path, &path_length)) {
                const wchar_t* slash = std::wcsrchr(path, L'\\');
                const wchar_t* file_name = path;
                if (slash != nullptr) {
                    file_name = slash + 1;
                }
                wcsncpy_s(name, name_length, file_name, MAX_PATH);
            }
            CloseHandle(process);
        }

        // row of the panel table the window belongs to, otherwise nullptr
        const PanelKind* find_panel_kind(HWND window) {
            // class first: no process opened for most windows
            wchar_t class_name[256] = {};
            if (GetClassNameW(window, class_name, 256) == 0) {
                return nullptr;
            }
            wchar_t process_name[MAX_PATH] = {};
            for (const PanelKind& kind : panel_kinds) {
                if (std::wcscmp(class_name, kind.class_name) != 0) {
                    continue;
                }
                // read once, only for a class match
                if (process_name[0] == L'\0') {
                    read_process_name(window, process_name, MAX_PATH);
                }
                if (_wcsicmp(process_name, kind.process_name) == 0) {
                    return &kind;
                }
            }
            return nullptr;
        }

        // cloaked = hidden by the compositor. Start and Search are cloaked while closed
        bool is_panel_closed(HWND window) {
            DWORD cloaked = 0;
            if (FAILED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
                return false;
            }
            return cloaked != 0;
        }

        // the 3 lines on the taskbar edge, along one axis
        struct EdgeLines {
            LONG panel_pixels = 0;      // panel side that faces the taskbar
            LONG taskbar_pixels = 0;    // taskbar side that faces the screen middle
            LONG monitor_pixels = 0;    // monitor edge under the taskbar
            bool is_horizontal = false; // left or right edge: the move is along x
        };

        EdgeLines make_edge_lines(const RECT& panel_bounds_pixels, const RECT& monitor_bounds_pixels,
            const RECT& taskbar_bounds_pixels) {
            EdgeLines lines;
            switch (resolve_taskbar_edge(monitor_bounds_pixels, taskbar_bounds_pixels)) {
            case TaskbarEdge::bottom:
                lines.panel_pixels = panel_bounds_pixels.bottom;
                lines.taskbar_pixels = taskbar_bounds_pixels.top;
                lines.monitor_pixels = monitor_bounds_pixels.bottom;
                break;
            case TaskbarEdge::top:
                lines.panel_pixels = panel_bounds_pixels.top;
                lines.taskbar_pixels = taskbar_bounds_pixels.bottom;
                lines.monitor_pixels = monitor_bounds_pixels.top;
                break;
            case TaskbarEdge::left:
                lines.panel_pixels = panel_bounds_pixels.left;
                lines.taskbar_pixels = taskbar_bounds_pixels.right;
                lines.monitor_pixels = monitor_bounds_pixels.left;
                lines.is_horizontal = true;
                break;
            case TaskbarEdge::right:
                lines.panel_pixels = panel_bounds_pixels.right;
                lines.taskbar_pixels = taskbar_bounds_pixels.left;
                lines.monitor_pixels = monitor_bounds_pixels.right;
                lines.is_horizontal = true;
                break;
            }
            return lines;
        }
    } // namespace

    POINT make_panel_offset(const RECT& panel_bounds_pixels, const RECT& monitor_bounds_pixels,
        const RECT& taskbar_bounds_pixels, bool is_taskbar_shown) {
        EdgeLines lines = make_edge_lines(panel_bounds_pixels, monitor_bounds_pixels, taskbar_bounds_pixels);

        // placed elsewhere by the shell: leave it
        bool is_on_taskbar = lines.panel_pixels == lines.taskbar_pixels;
        bool is_on_monitor = lines.panel_pixels == lines.monitor_pixels;
        if (!is_on_taskbar && !is_on_monitor) {
            return POINT{0, 0};
        }

        LONG target_pixels = lines.monitor_pixels;
        if (is_taskbar_shown) {
            target_pixels = lines.taskbar_pixels;
        }
        LONG move_pixels = target_pixels - lines.panel_pixels;
        if (lines.is_horizontal) {
            return POINT{move_pixels, 0};
        }
        return POINT{0, move_pixels};
    }

    bool is_panel(HWND window) {
        return find_panel_kind(window) != nullptr;
    }

    std::vector<PanelWindow> build_panel_list() {
        std::vector<PanelWindow> panels;
        // FindWindowExW, not EnumWindows: EnumWindows skips the windows of store apps such as Start
        HWND window = FindWindowExW(nullptr, nullptr, nullptr, nullptr);
        while (window != nullptr) {
            const PanelKind* kind = find_panel_kind(window);
            if (kind != nullptr) {
                panels.push_back(PanelWindow{window, kind->should_move_while_open});
            }
            window = FindWindowExW(nullptr, window, nullptr, nullptr);
        }
        return panels;
    }

    void align_panel(const PanelWindow& panel, const RECT& monitor_bounds_pixels, const RECT& taskbar_bounds_pixels,
        bool is_taskbar_shown) {
        if (!panel.should_move_while_open && !is_panel_closed(panel.window)) {
            return;
        }
        RECT panel_bounds_pixels = {};
        if (!GetWindowRect(panel.window, &panel_bounds_pixels)) {
            return;
        }

        POINT offset_pixels = make_panel_offset(panel_bounds_pixels, monitor_bounds_pixels, taskbar_bounds_pixels,
            is_taskbar_shown);
        if (offset_pixels.x == 0 && offset_pixels.y == 0) {
            return;
        }
        // async: a hung shell window cannot block this app. same target each tick until the move lands
        SetWindowPos(panel.window, nullptr, panel_bounds_pixels.left + offset_pixels.x,
            panel_bounds_pixels.top + offset_pixels.y, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    }

    void restore_panels(HWND taskbar) {
        RECT taskbar_bounds_pixels = {};
        if (!GetWindowRect(taskbar, &taskbar_bounds_pixels)) {
            return;
        }
        HMONITOR monitor = MonitorFromWindow(taskbar, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitor_info = {};
        monitor_info.cbSize = sizeof(monitor_info);
        if (!GetMonitorInfoW(monitor, &monitor_info)) {
            return;
        }

        HWND window = FindWindowExW(nullptr, nullptr, nullptr, nullptr);
        while (window != nullptr) {
            const PanelKind* kind = find_panel_kind(window);
            bool is_on_monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) == monitor;
            if (kind != nullptr && is_on_monitor) {
                PanelWindow panel{window, kind->should_move_while_open};
                align_panel(panel, monitor_info.rcMonitor, taskbar_bounds_pixels, true);
            }
            window = FindWindowExW(nullptr, window, nullptr, nullptr);
        }
    }
} // namespace ninja
