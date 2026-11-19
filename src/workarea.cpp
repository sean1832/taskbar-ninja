#include "workarea.h"

#include <optional>
#include <windows.h>

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
            if (!SystemParametersInfoW(SPI_SETWORKAREA, 0, &current_pixels, 0)) {
                return false;
            }
            EnumWindows(notify_maximized_window, reinterpret_cast<LPARAM>(monitor));
            return true;
        }

        // monitor minus the strip teh taskbar sits on; the taskbar can dock at any edge
        // Windows 11 22H2 later docks the taskbar on all four edges.
        std::optional<RECT> make_reserved_area(const RECT& monitor_pixels, const RECT& taskbar_pixels) {
            RECT overlap_pixels = {};
            if (!IntersectRect(&overlap_pixels, &monitor_pixels, &taskbar_pixels)) {
                return std::nullopt;
            }

            LONG overlap_width = overlap_pixels.right - overlap_pixels.left;
            LONG overlap_height = overlap_pixels.bottom - overlap_pixels.top;
            RECT reserved_pixels = monitor_pixels;
            // wide strip: top or bottom edge, whichever is nearer
            if (overlap_width >= overlap_height) {
                LONG gap_to_top = overlap_pixels.top - monitor_pixels.top;
                LONG gap_to_bottom = overlap_pixels.bottom - monitor_pixels.bottom;
                if (gap_to_top < gap_to_bottom) {
                    reserved_pixels.top = overlap_pixels.bottom;
                } else {
                  reserved_pixels.bottom = overlap_pixels.top;
                }
                return reserved_pixels;
            }

            // tall strip: left or right edge, whichever is nearer
            LONG gap_to_left = overlap_pixels.left - monitor_pixels.left;
            LONG gap_to_right = monitor_pixels.right - overlap_pixels.right;
            if (gap_to_left < gap_to_right) {
                reserved_pixels.left = overlap_pixels.right;
            } else {
                reserved_pixels.right = overlap_pixels.left;
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