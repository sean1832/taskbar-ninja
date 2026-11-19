#include "hold.h"

#include <windows.h>

#include <cwchar>
#include <span>

namespace ninja {
    namespace {
        constexpr const wchar_t* taskbar_classes[] = {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"};

        // 24H2
        constexpr const wchar_t* shell_classes[] = {
            L"Windows.UI.Core.CoreWindow", // start, search, notification centre, calendar, jump list
            L"ControlCenterWindow", // quick settings
            L"TopLevelWindowForOverflowXamlIsland", // tray overflow
            L"Shell_TrayWnd",
            L"Shell_SecondaryTrayWnd"
        };

        // app preview. covers the whole monitor
        constexpr const wchar_t* preview_classes[] = {L"XamlExplorerHostIslandWindow"};

        // reads the class without a message, so a hung window cannot block this
        bool has_class(HWND window, std::span<const wchar_t* const> class_names) {
            wchar_t class_name[256] = {};
            if (GetClassNameW(window, class_name, 256) == 0) {
                return false;
            }
            for (const wchar_t* candidate : class_names) {
                if (std::wcscmp(class_name, candidate) == 0) {
                    return true;
                }
            }
            return false;
        }
    } // namespace

    HMONITOR resolve_shell_monitor(HWND window) {
        if (window == nullptr) {
            return nullptr;
        }

        if (!has_class(window, shell_classes)) {
            return nullptr;
        }
        return MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    }

    HMONITOR resolve_popup_monitor(POINT cursor_pixels) {
        HWND window = GetAncestor(WindowFromPoint(cursor_pixels), GA_ROOT);
        if (window == nullptr) {
            return nullptr;
        }

        // tooltips and the taskbar's own menu, owned by the taskbar
        HWND owner = GetAncestor(window, GA_ROOTOWNER);
        if (has_class(owner, taskbar_classes)) {
            return MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
        }
        if (has_class(window, preview_classes)) {
            return MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        }
        return nullptr;
    }
} // namespace ninja