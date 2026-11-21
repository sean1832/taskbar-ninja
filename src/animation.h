#pragma once

#include <windows.h>

#include "edge.h"
#include "explorer_dll.h"

namespace ninja {
    /**
    * Makes the row that slides the content of one taskbar in or out, with the curve of that slide.
    * @param taskbar Shell_TrayWnd or Shell_SecondaryTrayWnd
    * @param is_slid_out true = slide out, false = slide in
    * @param duration_ms slide time. 0 or less = move at once
    * @param edge monitor edge the taskbar docks at
    * @return row for the animation table
    */
    [[nodiscard]] AnimationRow make_animation_row(HWND taskbar, bool is_slid_out, int duration_ms, TaskbarEdge edge);

    /**
     * Sends where the content of every taskbar must be to the explorer dll.
     * @param table one row for each taskbar. A taskbar with no row slides in at once
     * @return false when Explorer does not have the explorer dll, so nothing moved
     */
    [[nodiscard]] bool send_animation_table(const AnimationTable& table);
}