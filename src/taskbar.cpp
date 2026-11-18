// Hide and show method from Per-Monitor Taskbar.
// reference https://github.com/nikoleinonen/per-monitor-taskbar/blob/22cf088b6e97a2bd5b9e623c3fae852664ac4488/src/taskbar.cpp

#include "taskbar.h"

#include <windows.h>

#include <string>
#include <string_view>

#include "log.h"

namespace ninja {
    namespace {
        // primary screen first
        constexpr const wchar_t* taskbar_classes[] = {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"};

        int restore_taskbars_of_class(const wchar_t* class_name) {
            int restored_count = 0;

            // walk every top-level window of this class; one per monitor
            HWND taskbar = FindWindowExW(nullptr, nullptr, class_name, nullptr);
            while (taskbar != nullptr) {
                if (is_taskbar_hidden(taskbar)) {
                    show_taskbar(taskbar);
                    restored_count += 1;
                }
                taskbar = FindWindowExW(nullptr, taskbar, class_name, nullptr);
            }
            return restored_count;
        }
    } // namespace

    HWND find_primary_taskbar() {
        return FindWindowW(taskbar_classes[0], nullptr);
    }

    bool hide_taskbar(HWND taskbar) {
        LONG_PTR extended_style = GetWindowLongPtrW(taskbar, GWL_EXSTYLE);
        SetWindowLongPtrW(taskbar, GWL_EXSTYLE, extended_style | WS_EX_LAYERED | WS_EX_TRANSPARENT);
        return SetLayeredWindowAttributes(taskbar, 0, 0, LWA_ALPHA) != FALSE;
    }

    void show_taskbar(HWND taskbar) {
        LONG_PTR extended_style = GetWindowLongPtrW(taskbar, GWL_EXSTYLE);

        // opaque first, then style; a layered change at alpha 0 can leave it blank
        if (extended_style & WS_EX_LAYERED) {
            SetLayeredWindowAttributes(taskbar, 0, 255, LWA_ALPHA);
        }
        SetWindowLongPtrW(taskbar, GWL_EXSTYLE, extended_style & ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT));

        // async, a hung explorer cannot block this process
        SetWindowPos(taskbar, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS);
        RedrawWindow(taskbar, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
    }

    bool is_taskbar_hidden(HWND taskbar) {
        LONG_PTR extended_style = GetWindowLongPtrW(taskbar, GWL_EXSTYLE);
        // click-through along counts: a taskbar never needs it
        if (extended_style & WS_EX_TRANSPARENT) {
            return true;
        }
        if (!(extended_style & WS_EX_LAYERED)) {
            return false;
        }

        BYTE alpha = 255;
        DWORD flags = 0;
        // fails when Explorer draws with UpdateLayeredWindow; not our hide
        if (!GetLayeredWindowAttributes(taskbar, nullptr, &alpha, &flags)) {
            return false;
        }

        bool has_alpha = (flags & LWA_ALPHA) != 0;
        return has_alpha && alpha == 0;
    }

    int restore_taskbar() {
        int restored_count = 0;
        for (const wchar_t* class_name : taskbar_classes) {
            restored_count += restore_taskbars_of_class(class_name);
        }
        return restored_count;
    }

    void restore_taskbar_and_log(std::wstring_view reason) {
        int restored_count = restore_taskbar();
        append_log(LogLevel::info, L"restore", {
            {L"reason", static_cast<std::wstring>(reason)},
            {L"restored_count", std::to_wstring(restored_count)},
        });
    }
} // namespace ninja
