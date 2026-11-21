#pragma once

#include <windows.h>

#include "edge.h"
#include "explorer_dll.h"

namespace ninja {
    /**
     * Makes the row that animates the content of one taskbar in or out, with the curve of that animation.
     * @param taskbar Shell_TrayWnd or Shell_SecondaryTrayWnd
     * @param is_hidden true = animate out, false = animate in
     * @param duration_ms animation time. 0 or less = change at once
     * @param edge monitor edge the taskbar docks at
     * @return row for the animation table
     */
    [[nodiscard]] AnimationRow make_animation_row(HWND taskbar, bool is_hidden, int duration_ms, TaskbarEdge edge);

    /**
     * Sends where the content of every taskbar must be to the explorer dll.
     * @param table one row for each taskbar. A taskbar with no row slides in at once
     * @return false when Explorer does not have the explorer dll, so nothing moved
     */
    [[nodiscard]] bool send_animation_table(const AnimationTable& table);
} // namespace ninja