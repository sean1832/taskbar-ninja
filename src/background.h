#pragma once

#include "explorer_dll.h"

namespace ninja {
    /**
     * Gives the background of every taskbar a look.
     * @param look style and its options
     * @remarks Returns at once. A worker thread loads the explorer dll first when Explorer does not have it yet.
     */
    void set_background(const BackgroundLook& look);

    /**
     * Puts the original background back on every taskbar.
     * @remarks Allocates nothing and needs no saved state, so the crash handler and the watchdog can run it.
     */
    void restore_background();
}
