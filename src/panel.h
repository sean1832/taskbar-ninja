#pragma once

#include <windows.h>

#include <vector>

namespace ninja {
    /**
    * Makes the move that puts the taskbar side of a shell panel, such as Start, on the visible edge.
    * @param panel_bounds_pixels panel window area
    * @param monitor_bounds_pixels full monitor area
    * @param taskbar_bounds_pixels taskbar window area
    * @param is_taskbar_shown taskbar shows now
    * @return move in pixels, or 0,0 when the panel is already there or sits elsewhere
    * @remarks The visible edge is the inner side of a shown taskbar, or the monitor edge when the taskbar is hidden.
    * Only a panel that touches one of these 2 lines moves, so a panel the shell places elsewhere stays.
    */
    [[nodiscard]] POINT make_panel_offset(const RECT& panel_bounds_pixels, const RECT& monitor_bounds_pixels,
        const RECT& taskbar_bounds_pixels, bool is_taskbar_shown);

    /**
     * One shell panel window this app moves, such as Start or Search.
     */
    struct PanelWindow {
        HWND window;                         ///< panel window
        bool should_move_while_open = false; ///< the panel put itself back when it opens, so it also moves while open
    };

    /**
     * Checks whether a window is a shell panel from the panel table.
     * @param window any window
     * @return true for Start, Search and the other panels in the table
     */
    [[nodiscard]] bool is_panel(HWND window);

    /**
     * Builds the list of shell panel windows that exist now, from the panel table.
     * @return found panels, in window order
     */
    [[nodiscard]] std::vector<PanelWindow> build_panel_list();

    /**
    * Moves one shell panel so its taskbar side sits on the visible edge.
    * @param panel panel window
    * @param monitor_bounds_pixels full monitor area of the panel
    * @param taskbar_bounds_pixels taskbar window area on that monitor
    * @param is_taskbar_shown taskbar shows now
    * @remarks A closed panel always moves. An open panel moves only when should_move_while_open is set, so Start never jumps while open.
    */
    void align_panel(const PanelWindow& panel, const RECT& monitor_bounds_pixels, const RECT& taskbar_bounds_pixels,
        bool is_taskbar_shown);

    /**
     * Puts every shell panel on the monitor of a taskbar back on that taskbar.
     * @param taskbar taskbar window
     * @remarks Needs no saved state and allocates nothing, so the restore after a crash can run it too.
     */
    void restore_panels(HWND taskbar);
} // namespace ninja
