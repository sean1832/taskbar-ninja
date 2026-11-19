#pragma once

#include <windows.h>

#include "config.h"

namespace ninja {
    /**
     * Mouse state of one taskbar, and whether the hot zone wants the taskbar shown.
     */
    struct Visibility {
        bool is_shown = false;             ///< hot zone wants the taskbar shown
        bool is_mouse_on = false;          ///< mouse in the hot zone, or over the shown taskbar
        ULONGLONG mouse_changed_at_ms = 0; ///< tick count when is_mouse_on last changed
    };

    /**
     * Applies one mouse reading to the visibility of one taskbar.
     * @param visibility state from the last reading
     * @param settings delays and hot zone height of the monitor
     * @param monitor_bounds_pixels full monitor area
     * @param taskbar_bounds_pixels taskbar window area
     * @param cursor_pixels mouse position
     * @param now_ms tick count of this reading
     * @return new visibility
     * @remarks The taskbar area counts only while the taskbar shows, so a hidden taskbar does not make the hot zone taller.
     */
    [[nodiscard]] Visibility apply_mouse(const Visibility& visibility, const MonitorSettings& settings,
        const RECT& monitor_bounds_pixels, const RECT& taskbar_bounds_pixels, POINT cursor_pixels, ULONGLONG now_ms);
} // namespace ninja
