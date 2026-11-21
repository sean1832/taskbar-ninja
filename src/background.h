#pragma once

#include "explorer_dll.h"

namespace ninja {
    /**
     * Gives the background of every taskbar a look.
     * @param look style and its options
     * @return false when Explorer does not have the explorer dll, so nothing changed
     */
    [[nodiscard]] bool set_background(const BackgroundLook& look);


    /**
     * Puts the original background back on every taskbar.
     * @remarks Allocates nothing and needs no saved state, so the crash handler and the watchdog can run it.
     */
    void restore_background();
}
