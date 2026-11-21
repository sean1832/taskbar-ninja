#include "animation.h"

#include <windows.h>

#include <cstdint>

#include "explorer_dll.h"

namespace ninja {
    namespace {
        // curves. control points from https://easings.net/#easeOutQuint and https://easings.net/#easeInQuint
        // show: starts fast, ends slow
        constexpr AnimationCurve show_curve = {0.22f, 1.0f, 0.36f, 1.0f};
        // hide: starts slow, ends fast
        constexpr AnimationCurve hide_curve = {0.64f, 0.0f, 0.78f, 0.0f};

        bool set_explorer_event(const wchar_t* event_name) {
            HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, event_name);
            if (event == nullptr) {
                return false; // dll has not made the event, no Explorer has the dll
            }
            SetEvent(event);
            CloseHandle(event);
            return true;
        }
    } // namespace

    AnimationRow make_animation_row(HWND taskbar, bool is_slid_out, int duration_ms, TaskbarEdge edge) {
        AnimationRow row;
        // HWND as a number, the same value is valid in Explorer
        row.taskbar_window = reinterpret_cast<std::uintptr_t>(taskbar);
        row.is_slid_out = is_slid_out;
        row.duration_ms = 0;
        if (duration_ms > 0) {
            row.duration_ms = static_cast<std::uint32_t>(duration_ms);
        }
        row.curve = show_curve;
        if (is_slid_out) {
            row.curve = hide_curve;
        }
        row.edge = edge;
        return row;
    }

    bool send_animation_table(const AnimationTable& table) {
        HANDLE animation_memory = OpenFileMappingW(FILE_MAP_WRITE, FALSE, explorer_animation_memory_name);
        if (animation_memory == nullptr) {
            return false;
        }
        auto* shared_table = static_cast<AnimationTable*>(
            MapViewOfFile(animation_memory, FILE_MAP_WRITE, 0, 0, sizeof(AnimationTable)));
        if (shared_table == nullptr) {
            CloseHandle(animation_memory);
            return false;
        }
        *shared_table = table;
        UnmapViewOfFile(shared_table);
        CloseHandle(animation_memory);
        // table first, then the event, so the dll reads the new table
        return set_explorer_event(explorer_animate_event_name);
    }
} // namespace ninja