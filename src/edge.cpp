#include "edge.h"

#include <windows.h>

namespace ninja {
    TaskbarEdge resolve_taskbar_edge(const RECT& monitor_bounds_pixels, const RECT& taskbar_bounds_pixels) {
        RECT overlap_pixels = {};
        // missing or off-monitor taskbar: the Windows default edge
        if (!IntersectRect(&overlap_pixels, &monitor_bounds_pixels, &taskbar_bounds_pixels)) {
            return TaskbarEdge::bottom;
        }

        LONG overlap_width = overlap_pixels.right - overlap_pixels.left;
        LONG overlap_height = overlap_pixels.bottom - overlap_pixels.top;
        // wide strip: top or bottom edge, whichever is nearer
        if (overlap_width >= overlap_height) {
            LONG gap_to_top = overlap_pixels.top - monitor_bounds_pixels.top;
            LONG gap_to_bottom = monitor_bounds_pixels.bottom - overlap_pixels.bottom;
            if (gap_to_top < gap_to_bottom) {
                return TaskbarEdge::top;
            }
            return TaskbarEdge::bottom;
        }

        // tall strip: left or right edge, whichever is nearer
        LONG gap_to_left = overlap_pixels.left - monitor_bounds_pixels.left;
        LONG gap_to_right = monitor_bounds_pixels.right - overlap_pixels.right;
        if (gap_to_left < gap_to_right) {
            return TaskbarEdge::left;
        }
        return TaskbarEdge::right;
    }
} // namespace ninja
