#pragma once

#include <windows.h>

namespace ninja {
    /**
     * Sets the work area of one monitor to the full monitor, so windows can fill the taskbar strip.
     * @param monitor monitor handle
     * @return false when Windows rejects the change
     * @remarks Sends no broadcast, because Explorer answers a broadcast by reserving the strip again.
     */
    [[nodiscard]] bool cover_work_area(HMONITOR monitor);

    /**
     * Gives the taskbar strip of one monitor back to the taskbar.
     * @param monitor monitor handle
     * @param taskbar taskbar window on that monitor
     * @return false when the taskbar is off the monitor, or Windows rejects the change
     * @remarks Works for a taskbar on any edge. Other monitors keep their work area.
     */
    [[nodiscard]] bool reset_work_area(HMONITOR monitor, HWND taskbar);

    /**
     * Asks Explorer to reserve the taskbar strip on every monitor again, when any monitor is covered.
     * @return true when it sent the request
     * @remarks Needs no saved state, never waits and allocates nothing, so the crash handler can run it.
     */
    bool reset_all_work_areas();
} // namespace ninja