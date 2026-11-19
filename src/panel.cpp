#include "panel.h"

#include <windows.h>

#include "edge.h"

namespace ninja {
    namespace {
        // the 3 lines on the taskbar edge, along one axis
        struct EdgeLines {
            LONG panel_pixels = 0;      // panel side that faces the taskbar
            LONG taskbar_pixels = 0;    // taskbar side that faces the screen middle
            LONG monitor_pixels = 0;    // monitor edge under the taskbar
            bool is_horizontal = false; // left or right edge: the move is along x
        };

        EdgeLines make_edge_lines(const RECT& panel_bounds_pixels, const RECT& monitor_bounds_pixels,
            const RECT& taskbar_bounds_pixels) {
            EdgeLines lines;
            switch (resolve_taskbar_edge(monitor_bounds_pixels, taskbar_bounds_pixels)) {
            case TaskbarEdge::bottom:
                lines.panel_pixels = panel_bounds_pixels.bottom;
                lines.taskbar_pixels = taskbar_bounds_pixels.top;
                lines.monitor_pixels = monitor_bounds_pixels.bottom;
                break;
            case TaskbarEdge::top:
                lines.panel_pixels = panel_bounds_pixels.top;
                lines.taskbar_pixels = taskbar_bounds_pixels.bottom;
                lines.monitor_pixels = monitor_bounds_pixels.top;
                break;
            case TaskbarEdge::left:
                lines.panel_pixels = panel_bounds_pixels.left;
                lines.taskbar_pixels = taskbar_bounds_pixels.right;
                lines.monitor_pixels = monitor_bounds_pixels.left;
                lines.is_horizontal = true;
                break;
            case TaskbarEdge::right:
                lines.panel_pixels = panel_bounds_pixels.right;
                lines.taskbar_pixels = taskbar_bounds_pixels.left;
                lines.monitor_pixels = monitor_bounds_pixels.right;
                lines.is_horizontal = true;
                break;
            }
            return lines;
        }
    } // namespace

    POINT make_panel_offset(const RECT& panel_bounds_pixels, const RECT& monitor_bounds_pixels,
        const RECT& taskbar_bounds_pixels, bool is_taskbar_shown) {
        EdgeLines lines = make_edge_lines(panel_bounds_pixels, monitor_bounds_pixels, taskbar_bounds_pixels);

        // placed elsewhere by the shell: leave it
        bool is_on_taskbar = lines.panel_pixels == lines.taskbar_pixels;
        bool is_on_monitor = lines.panel_pixels == lines.monitor_pixels;
        if (!is_on_taskbar && !is_on_monitor) {
            return POINT{0, 0};
        }

        LONG target_pixels = lines.monitor_pixels;
        if (is_taskbar_shown) {
            target_pixels = lines.taskbar_pixels;
        }
        LONG move_pixels = target_pixels - lines.panel_pixels;
        if (lines.is_horizontal) {
            return POINT{move_pixels, 0};
        }
        return POINT{0, move_pixels};
    }
} // namespace ninja
