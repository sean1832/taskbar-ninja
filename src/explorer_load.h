#pragma once

#include <windows.h>

namespace ninja {
    /**
     * Tells if Explorer has the explorer dll and the dll watches the taskbar XAML.
     * @return true when the ready event of the dll is set
     */
    [[nodiscard]] bool is_explorer_ready();

    /**
     * Loads the explorer dll into Explorer, then posts a message to a window.
     * @param window window that gets the message
     * @param ready_message message posted once the dll is ready. Not posted when the load fails
     * @remarks Returns at once, as a worker thread does the load, which waits up to 35 s.
     * Does nothing while a load runs or when the dll is already ready.
     */
    void load_explorer(HWND window, UINT ready_message);
} // namespace ninja