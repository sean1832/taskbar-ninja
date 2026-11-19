#pragma once

#include <windows.h>

namespace ninja {
    /**
     * Monitor edge a taskbar docks at.
     */
    enum class TaskbarEdge {
        bottom,
        top,
        left,
        right,
    };

    /**
     * Resolves the monitor edge a taskbar docks at from where the taskbar sits.
     * @param monitor_bounds_pixels full monitor area
     * @param taskbar_bounds_pixels taskbar window area
     * @return nearest edge, or bottom when the taskbar is missing or off the monitor
     */
    [[nodiscard]] TaskbarEdge resolve_taskbar_edge(const RECT& monitor_bounds_pixels, const RECT& taskbar_bounds_pixels);
} // namespace ninja
