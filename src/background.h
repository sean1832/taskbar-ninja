#pragma once

namespace ninja {
    /**
     * Makes the background of every taskbar clear.
     * @remarks Returns at once. A worker thread loads the explorer dll first when Explorer does not have it yet.
     */
    void clear_background();

    /**
     * Puts the original background back on every taskbar.
     * @remarks Allocates nothing and needs no saved state, so the crash handler and the watchdog can run it.
     */
    void restore_background();
}