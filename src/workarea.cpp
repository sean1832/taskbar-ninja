#include "workarea.h"

#include <windows.h>

#include <optional>

#include "edge.h"

namespace ninja {
    namespace {
        // tells one maximized window on the monitor to fit the new work area
        BOOL CALLBACK notify_maximized_window(HWND window, LPARAM lparam) {
            HMONITOR monitor = reinterpret_cast<HMONITOR>(lparam);
            if (!IsWindowVisible(window) || !IsZoomed(window)) {
                return TRUE;
            }
            if (MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) != monitor) {
                return TRUE;
            }

            // notify: a hung window cannot block this
            SendNotifyMessageW(window, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
            return TRUE;
        }

        // no change when the work area is already right
        bool set_work_area(HMONITOR monitor, const RECT& current_pixels, RECT target_pixels) {
            if (EqualRect(&current_pixels, &target_pixels)) {
                return true;
            }
            // no SPIF_SENDCHANGE: Explorer takes that broadcast as a cue to reserve the strip again
            if (!SystemParametersInfoW(SPI_SETWORKAREA, 0, &target_pixels, 0)) {
                return false;
            }
            EnumWindows(notify_maximized_window, reinterpret_cast<LPARAM>(monitor));
            return true;
        }

        // monitor minus the strip the taskbar sits on
        std::optional<RECT> make_reserved_area(const RECT& monitor_pixels, const RECT& taskbar_pixels) {
            RECT overlap_pixels = {};
            if (!IntersectRect(&overlap_pixels, &monitor_pixels, &taskbar_pixels)) {
                return std::nullopt;
            }

            RECT reserved_pixels = monitor_pixels;
            switch (resolve_taskbar_edge(monitor_pixels, taskbar_pixels)) {
            case TaskbarEdge::bottom:
                reserved_pixels.bottom = overlap_pixels.top;
                break;
            case TaskbarEdge::top:
                reserved_pixels.top = overlap_pixels.bottom;
                break;
            case TaskbarEdge::left:
                reserved_pixels.left = overlap_pixels.right;
                break;
            case TaskbarEdge::right:
                reserved_pixels.right = overlap_pixels.left;
                break;
            }
            return reserved_pixels;
        }

        BOOL CALLBACK find_covered_monitor(HMONITOR monitor, HDC, LPRECT, LPARAM lparam) {
            bool* has_covered_monitor = reinterpret_cast<bool*>(lparam);
            MONITORINFO info = {sizeof(info)};
            if (!GetMonitorInfoW(monitor, &info)) {
                return TRUE;
            }
            if (!EqualRect(&info.rcWork, &info.rcMonitor)) {
                return TRUE;
            }
            *has_covered_monitor = true;
            // found one, stop the walk
            return FALSE;
        }
    } // namespace

    bool cover_work_area(HMONITOR monitor) {
        MONITORINFO info = {sizeof(info)};
        if (!GetMonitorInfoW(monitor, &info)) {
            return false;
        }
        return set_work_area(monitor, info.rcWork, info.rcMonitor);
    }

    bool reset_work_area(HMONITOR monitor, HWND taskbar) {
        MONITORINFO info = {sizeof(info)};
        if (!GetMonitorInfoW(monitor, &info)) {
            return false;
        }
        RECT taskbar_bounds_pixels = {};
        if (!GetWindowRect(taskbar, &taskbar_bounds_pixels)) {
            return false;
        }

        std::optional<RECT> reserved_pixels = make_reserved_area(info.rcMonitor, taskbar_bounds_pixels);
        if (!reserved_pixels.has_value()) {
            return false;
        }
        return set_work_area(monitor, info.rcWork, *reserved_pixels);
    }

    bool reset_all_work_areas() {
        bool has_covered_monitor = false;
        EnumDisplayMonitors(nullptr, nullptr, find_covered_monitor, reinterpret_cast<LPARAM>(&has_covered_monitor));
        if (!has_covered_monitor) {
            return false;
        }
        // Explorer answers by reserving every taskbar strip again; notify never waits
        SendNotifyMessageW(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
        return true;
    }
} // namespace ninja