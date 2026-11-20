#pragma once

namespace ninja {
    /// File name of the explorer dll, next to taskbar-ninja.exe.
    inline constexpr wchar_t explorer_file_name[] = L"taskbar-ninja-explorer.dll";

    /// Name of the exported hook procedure that the app passes to SetWindowsHookExW.
    inline constexpr char explorer_hook_procedure_name[] = "hook_procedure";

    // Event the dll sets once it watches the taskbar XAML. Manual reset.
    inline constexpr wchar_t explorer_ready_event_name[] = L"Local\\taskbar-ninja-explorer-ready";

    /// Event that makes every taskbar background clear. Auto reset.
    inline constexpr wchar_t explorer_clear_event_name[] = L"Local\\taskbar-ninja-explorer-clear";

    /// Event that puts the original taskbar background back. Auto reset.
    inline constexpr wchar_t explorer_restore_event_name[] = L"Local\\taskbar-ninja-explorer-restore";
} // namespace ninja