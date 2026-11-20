#pragma once

#include <windows.h>

namespace ninja {
    /**
     * Tells whether Windows starts this exe at sign-in.
     * @return true when the Run value of the user holds the path of this exe
     * @remarks False after the exe moves, because the value still holds the old path.
     */
    [[nodiscard]] bool is_autostart_enabled();

    /**
    * Adds this exe to the apps Windows starts at sign-in, or removes it.
    * @param enabled true writes the Run value of the user, false deletes it
    * @return ERROR_SUCCESS, or the Win32 error code
    */
    [[nodiscard]] LSTATUS set_autostart(bool enabled);
} // namespace ninja
