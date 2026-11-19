#pragma once

#include <windows.h>

#include "config.h"

namespace ninja {
    /**
     * Reasons other than the hot zone to keep one taskbar shown.
     */
    struct Hold {
        bool is_over_popup = false;     ///< mouse over a popup of the taskbar, such as an app preview
        bool is_shell_active = false;   ///< shell UI is in front, or a popup menu is open, on this monitor
        bool is_taskbar_active = false; ///< the taskbar itself is in front, as after Win+B
    };

    /**
     * Wanted state of one taskbar, and whether it should show now.
     */
    struct Visibility {
        bool is_shown = false;              ///< taskbar should show now
        bool is_wanted = false;             ///< mouse on the hot zone, the shown taskbar or its popup, or a hold is on
        ULONGLONG wanted_changed_at_ms = 0; ///< tick count when is_wanted last changed
    };

    /**
    * Applies one mouse reading and the holds to the visibility of one taskbar.
    * @param visibility state from the last reading
    * @param settings delays and hot zone thickness of the monitor
    * @param monitor_bounds_pixels full monitor area
    * @param taskbar_bounds_pixels taskbar window area
    * @param cursor_pixels mouse position
    * @param hold holds of this monitor
    * @param now_ms tick count of this reading
    * @return new visibility
    * @remarks The hot zone follows the taskbar edge. The taskbar area, its popups,
    * shell UI and popup menus count only while the taskbar shows, so a hotkey or
    * a menu never shows a hidden taskbar. An active taskbar shows it at once, without the show delay.
    * The hide delay counts from the end of the last reason to show.
    */
    [[nodiscard]] Visibility apply_mouse(const Visibility& visibility, const MonitorSettings& settings,
        const RECT& monitor_bounds_pixels, const RECT& taskbar_bounds_pixels, POINT cursor_pixels, const Hold& hold,
        ULONGLONG now_ms);

} // namespace ninja
