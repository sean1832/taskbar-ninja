#pragma once

#include <cstdint>
#include <string_view>

#include "edge.h"

namespace ninja {
    /// File name of the explorer dll, next to taskbar-ninja.exe.
    inline constexpr wchar_t explorer_file_name[] = L"taskbar-ninja-explorer.dll";

    /// Name of the exported hook procedure that the app passes to SetWindowsHookExW.
    inline constexpr char explorer_hook_procedure_name[] = "hook_procedure";

    /// Event the dll sets once it watches the taskbar XAML. Manual reset.
    inline constexpr wchar_t explorer_ready_event_name[] = L"Local\\taskbar-ninja-explorer-ready";

    /// Event that makes the dll read the look memory and apply it to every taskbar. Auto reset.
    inline constexpr wchar_t explorer_apply_event_name[] = L"Local\\taskbar-ninja-explorer-apply";

    /// Event that puts the original taskbar background back. Auto reset.
    inline constexpr wchar_t explorer_restore_event_name[] = L"Local\\taskbar-ninja-explorer-restore";

    /// Shared memory with one BackgroundLook. The dll makes it and keeps it open.
    inline constexpr wchar_t explorer_look_memory_name[] = L"Local\\taskbar-ninja-explorer-look";

    /**
     * Look of the background of every taskbar.
     */
    enum class BackgroundStyle : std::uint32_t {
        original, ///< what Explorer draws
        clear,    ///< no background, the desktop shows through
        tint,     ///< one see-through color
        acrylic,  ///< Windows acrylic material over the desktop
        blur,     ///< blurred desktop with a color on top
    };

    /**
     * Config word of one background style.
     */
    struct BackgroundStyleName {
        BackgroundStyle style;
        const wchar_t* name;
    };

    /// Config words of every background style. One source for config.ini, its error text, and the log.
    inline constexpr BackgroundStyleName background_style_names[] = {
        {BackgroundStyle::original, L"original"},
        {BackgroundStyle::clear, L"clear"},
        {BackgroundStyle::tint, L"tint"},
        {BackgroundStyle::acrylic, L"acrylic"},
        {BackgroundStyle::blur, L"blur"},
    };

    /**
     * Gives the config word of a background style.
     * @param style background style
     * @return word as written in config.ini, for example blur
     */
    [[nodiscard]] inline std::wstring_view background_style_name(BackgroundStyle style) {
        for (const BackgroundStyleName& entry : background_style_names) {
            if (entry.style == style) {
                return entry.name;
            }
        }
        return L"unknown";
    }

    /**
     * What every taskbar background looks like.
     * @remarks The app writes it into the look memory. Plain data only, as it crosses into Explorer.
     */
    struct BackgroundLook {
        BackgroundStyle style = BackgroundStyle::original;
        std::uint32_t color_argb = 0;           ///< tint, acrylic and blur color, AARRGGBB
        float blur_pixels = 0.0f;               ///< blur strength
        float feather_fraction = 0.0f;          ///< part of the bar thickness the blur fades in over, from the inner edge. 0 = sharp edge
        TaskbarEdge edge = TaskbarEdge::bottom; ///< monitor edge the taskbars dock at, which sets the feather direction
    };
} // namespace ninja
