#pragma once

#include <windows.h>

#include <string_view>

namespace ninja {
    /// Finds the taskbar on the primary monitor, or nullptr when Explorer is not running.
    [[nodiscard]] HWND find_primary_taskbar();

    /// Makes a taskbar invisible and click-through, and returns false when Windows rejects the alpha change.
    /// Remarks: the taskbar does not move.
    [[nodiscard]] bool hide_taskbar(HWND taskbar);

    /// Makes a taskbar opaque and clickable again.
    /// Remarks: keeps WS_EX_LAYERED, because Explorer may use it for its own effects.
    void show_taskbar(HWND taskbar);

    /// Reports whether a taskbar carries the flags that hide_taskbar sets.
    [[nodiscard]] bool is_taskbar_hidden(HWND taskbar);

    /// Shows every hidden taskbar on every monitor and returns how many it changed.
    /// Remarks: needs no saved state, any process can run it any number of times.
    int restore_taskbar();

    /// Runs restore_taskbars and logs the reason and the result.
    void restore_taskbar_and_log(std::wstring_view reason);
}
