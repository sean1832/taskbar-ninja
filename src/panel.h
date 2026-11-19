#pragma once

#include <windows.h>

namespace ninja {
    /**
    * Makes the move that puts the taskbar side of a shell panel, such as Start, on the visible edge.
    * @param panel_bounds_pixels panel window area
    * @param monitor_bounds_pixels full monitor area
    * @param taskbar_bounds_pixels taskbar window area
    * @param is_taskbar_shown taskbar shows now
    * @return move in pixels, or 0,0 when the panel is already there or sits elsewhere
    * @remarks The visible edge is the inner side of a shown taskbar, or the monitor edge when the taskbar is hidden.
    * Only a panel that touches one of these 2 lines moves, so a panel the shell places elsewhere stays.
    */
    [[nodiscard]] POINT make_panel_offset(const RECT& panel_bounds_pixels, const RECT& monitor_bounds_pixels,
        const RECT& taskbar_bounds_pixels, bool is_taskbar_shown);
} // namespace ninja
