#include "visibility.h"

#include <windows.h>

#include "config.h"
#include "edge.h"

namespace ninja {
    namespace {
        // strip along the taskbar edge, full monitor length
        RECT make_hot_zone(const RECT& monitor_bounds_pixels, const RECT& taskbar_bounds_pixels, int hot_zone_pixels) {
            RECT hot_zone_bounds_pixels = monitor_bounds_pixels;
            switch (resolve_taskbar_edge(monitor_bounds_pixels, taskbar_bounds_pixels)) {
            case TaskbarEdge::bottom:
                hot_zone_bounds_pixels.top = monitor_bounds_pixels.bottom - hot_zone_pixels;
                break;
            case TaskbarEdge::top:
                hot_zone_bounds_pixels.bottom = monitor_bounds_pixels.top + hot_zone_pixels;
                break;
            case TaskbarEdge::left:
                hot_zone_bounds_pixels.right = monitor_bounds_pixels.left + hot_zone_pixels;
                break;
            case TaskbarEdge::right:
                hot_zone_bounds_pixels.left = monitor_bounds_pixels.right - hot_zone_pixels;
                break;
            }
            return hot_zone_bounds_pixels;
        }
    } // namespace

    Visibility apply_mouse(const Visibility& visibility, const MonitorSettings& settings,
        const RECT& monitor_bounds_pixels, const RECT& taskbar_bounds_pixels, POINT cursor_pixels, ULONGLONG now_ms) {
        RECT hot_zone_bounds_pixels = make_hot_zone(monitor_bounds_pixels, taskbar_bounds_pixels, settings.hot_zone_pixels);

        bool is_in_hot_zone = PtInRect(&hot_zone_bounds_pixels, cursor_pixels) != FALSE;
        bool is_over_taskbar = visibility.is_shown && PtInRect(&taskbar_bounds_pixels, cursor_pixels) != FALSE;
        bool is_mouse_on = is_in_hot_zone || is_over_taskbar;

        Visibility next = visibility;
        if (is_mouse_on != visibility.is_mouse_on) {
            next.is_mouse_on = is_mouse_on;
            next.mouse_changed_at_ms = now_ms;
        }

        ULONGLONG elapsed_ms = now_ms - next.mouse_changed_at_ms;
        ULONGLONG show_delay_ms = static_cast<ULONGLONG>(settings.show_delay_ms);
        ULONGLONG hide_delay_ms = static_cast<ULONGLONG>(settings.hide_delay_ms);
        if (next.is_mouse_on && elapsed_ms >= show_delay_ms) {
            next.is_shown = true;
        }
        if (!next.is_mouse_on && elapsed_ms >= hide_delay_ms) {
            next.is_shown = false;
        }
        return next;
    }
} // namespace ninja
