#include "workarea.h"

#include <windows.h>

#include <optional>

#include "edge.h"

namespace ninja {
    namespace {
        // one work area change on one monitor
        struct WorkAreaChange {
            HMONITOR monitor;
            RECT current_pixels;
            RECT target_pixels;
        };

        // moves each edge of one maximized window on the monitor by the work area change
        BOOL CALLBACK resize_maximized_window(HWND window, LPARAM lparam) {
            const WorkAreaChange* change = reinterpret_cast<const WorkAreaChange*>(lparam);
            if (!IsWindowVisible(window) || !IsZoomed(window)) {
                return TRUE;
            }
            if (MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) != change->monitor) {
                return TRUE;
            }
            RECT window_pixels = {};
            if (!GetWindowRect(window, &window_pixels)) {
                return TRUE;
            }

            // keeps the frame overhang past the work area. the window stays maximized
            window_pixels.left += change->target_pixels.left - change->current_pixels.left;
            window_pixels.top += change->target_pixels.top - change->current_pixels.top;
            window_pixels.right += change->target_pixels.right - change->current_pixels.right;
            window_pixels.bottom += change->target_pixels.bottom - change->current_pixels.bottom;
            const LONG width_pixels = window_pixels.right - window_pixels.left;
            const LONG height_pixels = window_pixels.bottom - window_pixels.top;
            // async: a hung window cannot block this process
            SetWindowPos(window, nullptr, window_pixels.left, window_pixels.top, width_pixels, height_pixels,
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
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

            WorkAreaChange chagne = {monitor, current_pixels, target_pixels};
            EnumWindows(resize_maximized_window, reinterpret_cast<LPARAM>(&chagne));
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
            if (!is_work_area_covered(monitor)) {
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

    bool is_work_area_covered(HMONITOR monitor) {
        MONITORINFO info = {sizeof(info)};
        if (!GetMonitorInfoW(monitor, &info)) {
            return false;
        }
        return EqualRect(&info.rcWork, &info.rcMonitor) != FALSE;
    }
} // namespace ninja