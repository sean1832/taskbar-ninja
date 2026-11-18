#include "monitor.h"

#include <windows.h>

#include <string>
#include <vector>

#include "log.h"
#include "taskbar.h"

namespace ninja {
    namespace {
        // one display path: GDI name to EDID model name
        struct ModelName {
            std::wstring device_name;
            std::wstring model_name;
        };

        // empty list when Windows rejects the query; monitors then have no model name
        std::vector<ModelName> build_model_names() {
            std::vector<ModelName> names;
            UINT32 path_count = 0;
            UINT32 mode_count = 0;
            if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) {
                return names;
            }

            std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
            std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
            // ERROR_INSUFFICIENT_BUFFER when a display changes between the two calls
            LONG queried = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count,
                modes.data(), nullptr);
            if (queried != ERROR_SUCCESS) {
                return names;
            }

            for (UINT32 i = 0; i < path_count; i++) {
                // source = GPU output, gives the GDI name
                DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
                source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                source.header.size = sizeof(source);
                source.header.adapterId = paths[i].sourceInfo.adapterId;
                source.header.id = paths[i].sourceInfo.id;

                // target = the monitor, gives the EDID name
                DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};
                target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
                target.header.size = sizeof(target);
                target.header.adapterId = paths[i].targetInfo.adapterId;
                target.header.id = paths[i].targetInfo.id;

                bool has_source = DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS;
                bool has_target = DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS;
                if (has_source && has_target) {
                    names.push_back({source.viewGdiDeviceName, target.monitorFriendlyDeviceName});
                }
            }
            return names;
        }

        std::wstring find_model_name(const std::vector<ModelName>& names, const std::wstring& device_name) {
            for (const ModelName& name : names) {
                if (name.device_name == device_name) {
                    return name.model_name;
                }
            }
            return {};
        }

        // EnumDisplayMonitors callback; lparam = the list to fill
        BOOL CALLBACK add_monitor(HMONITOR handle, HDC, LPRECT, LPARAM lparam) {
            auto* monitors = reinterpret_cast<std::vector<Monitor>*>(lparam);

            MONITORINFOEXW info = {};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(handle, &info)) {
                // monitor gone during the walk: skip it, keep walking
                return TRUE;
            }

            Monitor monitor;
            monitor.handle = handle;
            monitor.device_name = info.szDevice;
            monitor.bounds_pixels = info.rcMonitor;
            monitor.is_primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
            monitor.taskbar = find_taskbar(handle);
            monitors->push_back(monitor);
            return TRUE;
        }

        std::wstring yes_no(bool value) {
            if (value) {
                return L"yes";
            }
            return L"no";
        }
    } // namespace

    std::vector<Monitor> build_monitor_list() {
        std::vector<Monitor> monitors;
        EnumDisplayMonitors(nullptr, nullptr, add_monitor, reinterpret_cast<LPARAM>(&monitors));

        std::vector<ModelName> names = build_model_names();
        for (Monitor& monitor : monitors) {
            monitor.model_name = find_model_name(names, monitor.device_name);
        }
        return monitors;
    }

    void log_monitor_list(const std::vector<Monitor>& monitors) {
        for (const Monitor& monitor : monitors) {
            const RECT& bounds = monitor.bounds_pixels;
            append_log(LogLevel::info, L"monitor", {
                {L"model_name", monitor.model_name},
                {L"device", monitor.device_name},
                {L"primary", yes_no(monitor.is_primary)},
                {L"width_pixels", std::to_wstring(bounds.right - bounds.left)},
                {L"height_pixels", std::to_wstring(bounds.bottom - bounds.top)},
                {L"has_taskbar", yes_no(monitor.taskbar != nullptr)},
            });
        }
    }
} // namespace ninja
