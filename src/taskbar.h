#pragma once

#include <windows.h>

#include <string_view>

namespace ninja {
    /**
     * Finds the taskbar on one monitor.
     * @param monitor monitor handle
     * @return taskbar window, or nullptr when the monitor has no taskbar
     */
    [[nodiscard]] HWND find_taskbar(HMONITOR monitor);

    /**
     * Makes a taskbar invisible and click-through.
     * @param taskbar taskbar window
     * @return false when Windows rejects the alpha change
     * @remarks The taskbar does not move.
     */
    [[nodiscard]] bool hide_taskbar(HWND taskbar);

    /**
     * Makes a taskbar opaque and clickable again.
     * @param taskbar taskbar window
     * @remarks Keeps WS_EX_LAYERED, because Explorer may use it for its own effects.
     */
    void show_taskbar(HWND taskbar);

    /**
     * Puts a taskbar above every other window, maximized windows included.
     * @param taskbar taskbar window
     * @remarks Explorer drops the taskbar behind a window that fills the whole monitor, which a covered work area allows.
     */
    void raise_taskbar(HWND taskbar);

    /**
     * Checks whether a taskbar carries the flags that hide_taskbar sets.
     * @param taskbar taskbar window
     * @return true when the taskbar is hidden
     */
    [[nodiscard]] bool is_taskbar_hidden(HWND taskbar);

    /**
     * Shows every hidden taskbar on every monitor.
     * @return number of taskbars it changed
     * @remarks Needs no saved state and allocates nothing, so any process, including the crash handler, can run it any number of times.
     */
    int restore_taskbar();

    /**
     * Runs restore_taskbar, then logs the reason and the restored count.
     * @param reason why the restore runs, as a snake_case word
     */
    void restore_taskbar_and_log(std::wstring_view reason);
}
