#include "settings_window.h"

#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <string>
#include <string_view>
#include <optional>

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

#include "autostart.h"
#include "config.h"
#include "explorer_dll.h"
#include "files.h"
#include "log.h"
#include "monitor.h"

// imgui leaves it out of its header, so the header needs no windows.h
// See third_party/imgui/backends/imgui_impl_win32.h
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace ninja {
    namespace {
        using Microsoft::WRL::ComPtr;

        constexpr wchar_t settings_window_class[] = L"taskbar-ninja-settings";
        constexpr wchar_t settings_window_title[] = L"Taskbar Ninja Settings";

        // keys a monitor section can set; background keys are [default] only
        constexpr const wchar_t* monitor_keys[] = {
            L"auto_hide", L"cover_taskbar_area", L"show_delay_ms", L"hide_delay_ms",
            L"hot_zone_pixels", L"show_animation_ms", L"hide_animation_ms"
        };

        // icon id from resources/app.rc
        constexpr WORD app_icon_id = 1;

        // client size at 100% scale
        constexpr int default_dpi = 96;
        constexpr int client_width_at_96_dpi = 440;
        constexpr int client_height_at_96_dpi = 600;

        // fixed size, no resize, no minimize
        constexpr DWORD window_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

        // imgui shows some results on frame late
        constexpr int extra_frames_after_input = 2;

        // "Open config file" button; ShellExecuteW runs a message loop, so never inside a frame
        constexpr UINT open_config_message = WM_APP + 1;

        // gpu first, then cpu renderer
        struct DriverChoice {
            D3D_DRIVER_TYPE type;
            const wchar_t* name;
        };
        constexpr DriverChoice driver_choices[] = {
            {D3D_DRIVER_TYPE_HARDWARE, L"hardware"},
            {D3D_DRIVER_TYPE_WARP, L"warp"}
        };

        /**
         * One entry of the monitor dropdown.
         * @remarks One entry per section, so identical monitors share one.
         */
        struct SettingsTarget {
            std::wstring section; // model name; empty = [default], All monitors
            int monitor_count = 0; // connected monitors with this model name; 0 = not connected
            bool has_primary = false; // one of them is the main monitor
            std::string label; // dropdown text, UTF-8
            std::vector<const wchar_t*> set_keys; // monitor_keys this section sets; empty for All monitors
        };

        /**
         * State of the open settings window.
         * @remarks Made on WM_NCCREATE, deleted on WM_NCDESTROY. The window holds a pointer to it.
         */
        struct SettingsWindowState {
            HWND window = nullptr; // this settings window
            HWND app_window = nullptr; // gets config_changed_message
            std::wstring config_path;
            ULONGLONG config_write_time = 0; // file write time the settings below match; 0 = file missing
            MonitorSettings monitor_settings; // values the selected target uses: its section, then [default]
            BackgroundSettings background_settings; // background keys as the file has them
            BackgroundSettings previewed_background; // last look sent to the app; a difference sends a preview
            std::vector<SettingsTarget> targets; // [0] = All monitors, then connected, then not connected
            size_t target_index = 0; // selected entry in targets
            int unnamed_monitor_count = 0; // connected monitors with no model name: [default] only
            bool has_monitor_sections = false; // file has a section other than [default]
            bool autostart_enabled = false; // not a config key. set at runtime by checking registry.
            ComPtr<ID3D11Device> device;
            ComPtr<ID3D11DeviceContext> device_context;
            ComPtr<IDXGISwapChain> swap_chain;
            ComPtr<ID3D11RenderTargetView> render_target; // nullptr after a failed resize
            bool imgui_started = false;
            int frames_left = 0; // frames still to draw after the last input
        };

        SettingsWindowState* settings_state_of(HWND window) {
            return reinterpret_cast<SettingsWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        std::wstring make_hresult_text(HRESULT result) {
            wchar_t text[16] = {};
            std::swprintf(text, 16, L"0x%08lX", static_cast<unsigned long>(result));
            return text;
        }

        bool create_device(HWND window, SettingsWindowState& state) {
            DXGI_SWAP_CHAIN_DESC swap_chain_desc = {};
            swap_chain_desc.BufferCount = 2; // flip model needs at least 2
            swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swap_chain_desc.OutputWindow = window; // width and height 0: client size of this window
            swap_chain_desc.SampleDesc.Count = 1;
            swap_chain_desc.Windowed = TRUE;
            swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

            const D3D_FEATURE_LEVEL feature_levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
            for (const DriverChoice& driver : driver_choices) {
                HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, driver.type, nullptr, 0,
                    feature_levels, ARRAYSIZE(feature_levels), D3D11_SDK_VERSION, &swap_chain_desc,
                    state.swap_chain.GetAddressOf(), state.device.GetAddressOf(), nullptr,
                    state.device_context.GetAddressOf());
                if (SUCCEEDED(result)) {
                    return true;
                }
                append_log(LogLevel::warning, L"settings_device_rejected", {
                    {L"driver", driver.name},
                    {L"hresult", make_hresult_text(result)}
                });
            }
            return false;
        }

        // view of back buffer 0, the target imgui draws into
        bool create_render_target(SettingsWindowState& state) {
            ComPtr<ID3D11Texture2D> back_buffer;
            HRESULT result = state.swap_chain->GetBuffer(0, IID_PPV_ARGS(back_buffer.GetAddressOf()));
            if (SUCCEEDED(result)) {
                result = state.device->CreateRenderTargetView(back_buffer.Get(), nullptr, state.render_target.GetAddressOf());
            }
            if (FAILED(result)) {
                append_log(LogLevel::error, L"settings_window_rejected", {
                    {L"step", L"render_target"},
                    {L"hresult", make_hresult_text(result)}
                });
                return false;
            }
            return true;
        }

        void resize_swap_chain(SettingsWindowState& state, UINT width_pixels, UINT height_pixels) {
            if (state.swap_chain == nullptr) {
                return;
            }
            // ResizeBuffers fails while a view of a buffer is alive
            state.render_target.Reset();
            HRESULT result = state.swap_chain->ResizeBuffers(0, width_pixels, height_pixels, DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(result)) {
                append_log(LogLevel::error, L"settings_window_rejected", {
                    {L"step", L"resize"},
                    {L"hresult", make_hresult_text(result)}
                });
                return;
            }
            create_render_target(state);
        }

        float dpi_scale_of(HWND window) {
            return static_cast<float>(GetDpiForWindow(window)) / static_cast<float>(default_dpi);
        }

        // ScaleAllSizes adds up, so start from the default style each time
        void apply_style(float dpi_scale) {
            ImGuiStyle& style = ImGui::GetStyle();
            style = ImGuiStyle();
            ImGui::StyleColorsDark(&style);
            style.ScaleAllSizes(dpi_scale);
            style.FontScaleDpi = dpi_scale;
        }

        // all or nothing: a failure undoes the parts already started
        bool start_imgui(HWND window, SettingsWindowState& state) {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr; // no imgui.ini
            io.LogFilename = nullptr; // no imgui_log.txt
            io.ConfigInputTextCursorBlink = false; // a blink needs a frame every half second
            apply_style(dpi_scale_of(window));

            if (!ImGui_ImplWin32_Init(window)) {
                ImGui::DestroyContext();
                append_log(LogLevel::error, L"settings_window_rejected", {{L"step", L"imgui_win32"}});
                return false;
            }
            if (!ImGui_ImplDX11_Init(state.device.Get(), state.device_context.Get())) {
                ImGui_ImplWin32_Shutdown();
                ImGui::DestroyContext();
                append_log(LogLevel::error, L"settings_window_rejected", {{L"step", L"imgui_dx11"}});
                return false;
            }
            return true;
        }

        // imgui takes UTF-8
        std::string make_utf8_text(std::wstring_view text) {
            if (text.empty()) {
                return {};
            }
            const int text_length = static_cast<int>(text.size());
            const int byte_count = WideCharToMultiByte(CP_UTF8, 0, text.data(), text_length, nullptr, 0, nullptr, nullptr);
            std::string utf8_text(static_cast<size_t>(byte_count), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), text_length, utf8_text.data(), byte_count, nullptr, nullptr);
            return utf8_text;
        }

        // section names ignore case, as in the profile api
        std::optional<size_t> find_target(const std::vector<SettingsTarget>& targets, const std::wstring& section) {
            for (size_t i = 0; i < targets.size(); i++) {
                if (_wcsicmp(targets[i].section.c_str(), section.c_str()) == 0) {
                    return i;
                }
            }
            return std::nullopt;
        }

        std::string make_target_label(const SettingsTarget& target) {
            if (target.section.empty()) {
                return "All monitors";
            }
            std::wstring label = target.section;
            if (target.monitor_count > 1) {
                label += L" (" + std::to_wstring(target.monitor_count) + L" monitors)";
            }
            if (target.has_primary) {
                label += L" (primary)";
            }
            return make_utf8_text(label);
        }

        // All monitors, connected models, then sections of monitors not connected
        std::vector<SettingsTarget> build_targets(const std::vector<Monitor>& monitors,
            const std::vector<std::wstring>& sections) {
            std::vector<SettingsTarget> targets;
            targets.push_back({}); // All monitors

            for (const Monitor& monitor : monitors) {
                if (monitor.model_name.empty()) {
                    continue; // no section possible
                }
                std::optional<size_t> index = find_target(targets, monitor.model_name);
                if (!index.has_value()) {
                    index = targets.size();
                    targets.push_back({monitor.model_name});
                }
                SettingsTarget& target = targets[*index];
                target.monitor_count += 1; // identical monitors share one section
                target.has_primary = target.has_primary || monitor.is_primary;
            }

            // primary screen right after 'All', other keep windows order
            std::stable_partition(targets.begin() + 1, targets.end(),
                [](const SettingsTarget& target) {
                    return target.has_primary;
                });

            for (const std::wstring& section : sections) {
                if (!find_target(targets, section).has_value()) {
                    targets.push_back({section}); // monitor_count 0: not connected
                }
            }

            for (SettingsTarget& target : targets) {
                target.label = make_target_label(target);
            }
            return targets;
        }

        // monitor_keys the section sets; none for [default], the base
        std::vector<const wchar_t*> read_set_keys(const std::wstring& config_path, const std::wstring& section) {
            std::vector<const wchar_t*> set_keys;
            if (section.empty()) {
                return set_keys; // All monitors: nothing inherits
            }
            for (const wchar_t* key : monitor_keys) {
                if (has_config_value(config_path, section.c_str(), key)) {
                    set_keys.push_back(key);
                }
            }
            return set_keys;
        }

        // wcscmp, not ==: callers pass their own literals, not monitor_keys pointers
        bool has_key(const SettingsTarget& target, const wchar_t* key) {
            for (const wchar_t* set_key : target.set_keys) {
                if (std::wcscmp(set_key, key) == 0) {
                    return true;
                }
            }
            return false;
        }

        // one monitor and no sections: All monitors is the only target that does something
        bool should_show_targets(const SettingsWindowState& state) {
            return state.targets.size() > 2 || state.has_monitor_sections;
        }

        void load_monitor_targets(SettingsWindowState& state) {
            std::wstring selected_section; // empty: All monitors
            if (state.target_index < state.targets.size()) {
                selected_section = state.targets[state.target_index].section;
            }

            const std::vector<Monitor> monitors = build_monitor_list();
            const std::vector<std::wstring> sections = read_monitor_sections(state.config_path);
            state.targets = build_targets(monitors, sections);
            for (SettingsTarget& target : state.targets) {
                target.set_keys = read_set_keys(state.config_path, target.section); // marker needs every section
            }
            state.has_monitor_sections = !sections.empty();
            state.unnamed_monitor_count = 0;
            for (const Monitor& monitor : monitors) {
                if (monitor.model_name.empty()) {
                    state.unnamed_monitor_count += 1;
                }
            }

            state.target_index = find_target(state.targets, selected_section).value_or(0); // gone: All monitors
            if (!should_show_targets(state)) {
                state.target_index = 0; // no dropdown, no hidden target
            }
        }

        std::wstring target_selection(const SettingsWindowState& state) {
            const std::wstring& section = state.targets[state.target_index].section;
            if (section.empty()) {
                return default_section;
            }
            return section;
        }

        void load_target_settings(SettingsWindowState& state) {
            SettingsTarget& target = state.targets[state.target_index];
            state.monitor_settings = read_monitor_settings(state.config_path, target.section).value_or(MonitorSettings{});
            target.set_keys = read_set_keys(state.config_path, target.section); // own write or reset changes them
        }

        void load_settings(SettingsWindowState& state) {
            state.config_path = config_path();
            // time first: a save during the read gives a new time, so the next focus reads again
            state.config_write_time = read_config_write_time(state.config_path);
            state.background_settings = read_background_settings(state.config_path).value_or(BackgroundSettings{});
            state.previewed_background = state.background_settings; // file look: nothing to preview
            load_monitor_targets(state); // sections can change with the file
            load_target_settings(state); // selection can change with the targets
        }

        // a hand edit since the last read: show the file, and the app reloads too
        void reload_changed_settings(SettingsWindowState& state) {
            if (read_config_write_time(state.config_path) == state.config_write_time) {
                return;
            }
            load_settings(state);
            append_log(LogLevel::info, L"config_changed");
            PostMessageW(state.app_window, config_changed_message, 0, 0);
        }

        bool on_create(HWND window, SettingsWindowState& state) {
            load_settings(state);
            state.autostart_enabled = is_autostart_enabled();
            // hand edits made while the window was closed
            PostMessageW(state.app_window, config_changed_message, 0, 0);
            if (!create_device(window, state)) {
                return false;
            }
            if (!create_render_target(state)) {
                return false;
            }
            if (!start_imgui(window, state)) {
                return false;
            }
            state.imgui_started = true;
            state.frames_left = extra_frames_after_input; // first frames lay out the window

            // dark title bar, to match the dark imgui colors
            BOOL use_dark_mode = TRUE;
            DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &use_dark_mode, sizeof(use_dark_mode));
            return true;
        }

        // reverse order of on_create; ComPtr releases the device when the state is deleted
        void on_destroy(SettingsWindowState& state) {
            if (!state.imgui_started) {
                return;
            }
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            state.imgui_started = false;
        }

        // 0..1 to 0..255, rounded
        std::uint32_t make_color_byte(float channel) {
            const float clamped = std::clamp(channel, 0.0f, 1.0f);
            return static_cast<std::uint32_t>(clamped * 255.0f + 0.5f);
        }

        std::uint32_t make_color_argb(const float rgba[4]) {
            const std::uint32_t red = make_color_byte(rgba[0]);
            const std::uint32_t green = make_color_byte(rgba[1]);
            const std::uint32_t blue = make_color_byte(rgba[2]);
            const std::uint32_t alpha = make_color_byte(rgba[3]);
            return (alpha << 24) | (red << 16) | (green << 8) | blue;
        }

        // writes one key into the section, then the app reloads after this frame
        void commit_value(SettingsWindowState& state, const wchar_t* section, const wchar_t* key, const std::wstring& value) {
            if (!write_config_value(state.config_path, section, key, value)) {
                return; // logged; the app keeps the file values
            }
            state.config_write_time = read_config_write_time(state.config_path); // own write: no reload on focus
            load_target_settings(state); // key now set in the section
            PostMessageW(state.app_window, config_changed_message, 0, 0);
        }

        // key out of the section: the monitor follows [default] again
        void reset_value(SettingsWindowState& state, const wchar_t* section, const wchar_t* key) {
            if (!delete_config_value(state.config_path, section, key)) {
                return; // logged
            }
            state.config_write_time = read_config_write_time(state.config_path); // own write: no reload on focus
            load_target_settings(state); // shows the [default] value, grey
            PostMessageW(state.app_window, config_changed_message, 0, 0);
        }

        // monitor section without the key: the [default] value shows
        bool is_key_inherited(const SettingsWindowState& state, const wchar_t* section, const wchar_t* key) {
            if (_wcsicmp(section, default_section) == 0) {
                return false; // [default] is the base
            }
            return !has_key(state.targets[state.target_index], key);
        }

        // x next to a key the monitor section sets
        void draw_reset_button(SettingsWindowState& state, const wchar_t* section, const wchar_t* key, bool is_inherited) {
            const bool is_section_key = !is_inherited && _wcsicmp(section, default_section) != 0;
            if (!is_section_key) {
                return;
            }
            ImGui::SameLine();
            ImGui::PushID(static_cast<const void*>(key)); // one x per key
            if (ImGui::SmallButton("x")) {
                reset_value(state, section, key);
            }
            ImGui::SetItemTooltip("Use the All monitors value");
            ImGui::PopID();
        }

        // on All monitors: which monitor sections keep their own value for the key
        void draw_override_marker(const SettingsWindowState& state, const wchar_t* section, const wchar_t* key) {
            if (_wcsicmp(section, default_section) != 0) {
                return; // only All monitors edits [default]
            }
            int override_count = 0;
            for (const SettingsTarget& target : state.targets) {
                if (has_key(target, key)) {
                    override_count += 1;
                }
            }
            if (override_count == 0) {
                return; // background keys land here too: never in a section
            }

            ImGui::SameLine();
            ImGui::TextDisabled("(set on %d)", override_count);
            if (!ImGui::BeginItemTooltip()) {
                return;
            }
            ImGui::TextUnformatted("These keep their own value:");
            for (const SettingsTarget& target : state.targets) {
                if (!has_key(target, key)) {
                    continue;
                }
                if (target.monitor_count == 0) {
                    ImGui::BulletText("%s (not connected)", target.label.c_str());
                } else {
                    ImGui::BulletText("%s", target.label.c_str());
                }
            }
            ImGui::EndTooltip();
        }

        void restore_default(SettingsWindowState& state) {
            if (!restore_default_config(state.config_path)) {
                return; // failed. logged. window kept the old values
            }
            load_settings(state); // also stores the new write time. no reload on focus
            PostMessageW(state.app_window, config_changed_message, 0, 0);
        }

        void draw_checkbox(SettingsWindowState& state, const wchar_t* section, const char* label, const wchar_t* key, bool& value) {
            const bool is_inherited = is_key_inherited(state, section, key);
            if (is_inherited) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            }

            const bool is_clicked = ImGui::Checkbox(label, &value);
            if (is_inherited) {
                ImGui::PopStyleColor();
            }
            if (is_clicked) {
                commit_value(state, section, key, make_bool_text(value));
            }
            draw_reset_button(state, section, key, is_inherited);
            draw_override_marker(state, section, key);
        }

        // clamped to the config range, so the window never writes a rejected value
        void draw_int_slider(SettingsWindowState& state, const wchar_t* section, const char* label, const wchar_t* key, int& value, IntRange range, const char* format) {
            const bool is_inherited = is_key_inherited(state, section, key);
            if (is_inherited) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            }
            ImGui::SliderInt(label, &value, range.min, range.max, format, ImGuiSliderFlags_AlwaysClamp);
            const bool is_committed = ImGui::IsItemDeactivatedAfterEdit(); // before PopStyleColor: same item
            if (is_inherited) {
                ImGui::PopStyleColor();
            }
            if (is_committed) {
                commit_value(state, section, key, std::to_wstring(value));
            }
            draw_reset_button(state, section, key, is_inherited);
            draw_override_marker(state, section, key);
        }

        void draw_style_combo(SettingsWindowState& state) {
            BackgroundSettings& settings = state.background_settings;
            const std::string current_name = make_utf8_text(background_style_name(settings.style));
            if (!ImGui::BeginCombo("Style", current_name.c_str())) {
                return;
            }
            for (const BackgroundStyleName& entry : background_style_names) {
                const bool is_selected = entry.style == settings.style;
                const std::string name = make_utf8_text(entry.name);
                if (ImGui::Selectable(name.c_str(), is_selected) && !is_selected) {
                    settings.style = entry.style;
                    commit_value(state, default_section, L"background", entry.name);
                }
            }
            ImGui::EndCombo();
        }

        void draw_color_edit(SettingsWindowState& state) {
            std::uint32_t& color_argb = state.background_settings.color_argb;
            float rgba[4] = {
                static_cast<float>((color_argb >> 16) & 0xFF) / 255.0f,
                static_cast<float>((color_argb >> 8) & 0xFF) / 255.0f,
                static_cast<float>(color_argb & 0xFF) / 255.0f,
                static_cast<float>((color_argb >> 24) & 0xFF) / 255.0f,
            };
            const ImGuiColorEditFlags flags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;
            if (ImGui::ColorEdit4("Color", rgba, flags)) {
                color_argb = make_color_argb(rgba);
            }
            // also after a drag in the picker popup, see EndGroup in imgui.cpp
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                commit_value(state, default_section, L"background_color", make_color_text(color_argb));
            }
        }

        void draw_autostart_checkbox(SettingsWindowState& state) {
            if (!ImGui::Checkbox("Start with Windows", &state.autostart_enabled)) {
                return;
            }
            const LSTATUS status = set_autostart(state.autostart_enabled);
            if (status == ERROR_SUCCESS) {
                return;
            }
            append_log(LogLevel::error, L"autostart_not_set", {
                {L"enabled", make_bool_text(state.autostart_enabled)},
                {L"win32_error", std::to_wstring(status)} // registry call return the code, no GetLastError
            });
            state.autostart_enabled = is_autostart_enabled(); // check again, shows real value.
        }

        void draw_restore_defaults(SettingsWindowState& state) {
            constexpr char popup_name[] = "Restore defaults?"; // OpenPopup and BeginPopupModal need same id
            if (ImGui::Button("Restore Default")) {
                ImGui::OpenPopup(popup_name);
            }

            // centre of the window, set once when it opens
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (!ImGui::BeginPopupModal(popup_name, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                return; // popup closed
            }
            ImGui::TextUnformatted("config.ini restored default values.");
            ImGui::Spacing();
            if (ImGui::Button("Restore")) {
                restore_default(state);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // monitor targets
        void draw_monitor_target_combo(SettingsWindowState& state) {
            if (!should_show_targets(state)) {
                return;
            }
            const SettingsTarget& selected = state.targets[state.target_index];
            if (ImGui::BeginCombo("Monitor", selected.label.c_str())) {
                bool is_separator_drawn = false;
                for (size_t i = 0; i < state.targets.size(); i++) {
                    const SettingsTarget& target = state.targets[i];
                    // non-connected monitor come last. index 0 is all monitor
                    const bool is_first_not_connected = i > 0 && target.monitor_count == 0 && !is_separator_drawn;
                    if (is_first_not_connected) {
                        ImGui::SeparatorText("Not connected");
                        is_separator_drawn = true;
                    }

                    ImGui::PushID(static_cast<int>(i)); // [Dell] and [DELL] give same label
                    if (ImGui::Selectable(target.label.c_str(), i == state.target_index)) {
                        state.target_index = i;
                        load_target_settings(state);
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (state.unnamed_monitor_count > 0) {
                ImGui::TextDisabled("%d monitor(s) with no name use All monitors.", state.unnamed_monitor_count);
            }
        }

         // every [default] key
        void draw_settings(SettingsWindowState& state) {
            MonitorSettings& monitor = state.monitor_settings;
            BackgroundSettings& background = state.background_settings;
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f); // room for label, x and marker

            draw_monitor_target_combo(state);
            // copy: restore defaults rebuilds the targets inside this frame
            const std::wstring target = target_selection(state);
            const wchar_t* section = target.c_str();

            ImGui::SeparatorText("Hiding");
            draw_checkbox(state, section, "Auto hide", L"auto_hide", monitor.auto_hide);
            ImGui::BeginDisabled(!monitor.auto_hide); // no effect without auto hide
            draw_checkbox(state, section, "Cover taskbar area", L"cover_taskbar_area", monitor.cover_taskbar_area);
            ImGui::EndDisabled();
            draw_int_slider(state, section, "Hot zone", L"hot_zone_pixels", monitor.hot_zone_pixels, hot_zone_pixels_range, "%d px");
            draw_int_slider(state, section, "Show delay", L"show_delay_ms", monitor.show_delay_ms, delay_ms_range, "%d ms");
            draw_int_slider(state, section,"Hide delay", L"hide_delay_ms", monitor.hide_delay_ms, delay_ms_range, "%d ms");

            ImGui::SeparatorText("Animation");
            draw_int_slider(state, section, "Show animation", L"show_animation_ms", monitor.show_animation_ms, animation_ms_range, "%d ms");
            draw_int_slider(state, section, "Hide animation", L"hide_animation_ms", monitor.hide_animation_ms, animation_ms_range, "%d ms");

            // background keys: [default] only
            const bool is_monitor_target = state.target_index != 0;
            ImGui::SeparatorText("Background");
            if (is_monitor_target) {
                ImGui::TextDisabled("Applies to all monitors.");
            }
            ImGui::BeginDisabled(is_monitor_target);
            draw_style_combo(state);
            draw_color_edit(state);
            draw_int_slider(state, default_section, "Blur", L"background_blur_pixels", background.blur_pixels, blur_pixels_range, "%d px");
            draw_checkbox(state, default_section, "Feather", L"background_feather", background.feather);
            ImGui::BeginDisabled(!background.feather); // no effect without feather
            draw_int_slider(state, default_section, "Feather size", L"background_feather_percent", background.feather_percent,
                feather_percent_range, "%d%%");
            ImGui::EndDisabled();
            ImGui::EndDisabled();

            ImGui::SeparatorText("Configuration");
            draw_autostart_checkbox(state);

            ImGui::Spacing();
            if (ImGui::Button("Open config file")) {
                PostMessageW(state.window, open_config_message,0, 0); // runs after this frame
            }
            ImGui::SameLine();
            draw_restore_defaults(state);

            // a background control moved: draft look on the taskbar now, the file only on commit
            if (background != state.previewed_background) {
                state.previewed_background = background;
                // same thread: direct call, pumps no messages, so safe inside the frame
                SendMessageW(state.app_window, settings_preview_message, 0, reinterpret_cast<LPARAM>(&background));
            }
            ImGui::PopItemWidth();
        }

        void open_config_file(const SettingsWindowState& state) {
            INT_PTR result = 0;
            if (!open_path(state.config_path, &result)) {
                append_log(LogLevel::error, L"open_config_file", {{L"shell_error", std::to_wstring(result)}});
            }
        }

        // false when the device is gone
        bool draw_frame(SettingsWindowState& state) {
            if (state.render_target == nullptr) {
                return true; // failed resize: nothing to draw into, device still fine
            }

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            // one imgui window that fills the client area
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);
            ImGui::Begin("settings", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
            draw_settings(state);
            ImGui::End();
            ImGui::Render();

            // window background color, opaque
            const ImVec4 background = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
            const float clear_color[4] = {background.x, background.y, background.z, 1.0f};
            ID3D11RenderTargetView* render_targets[] = {state.render_target.Get()};
            state.device_context->OMSetRenderTargets(1, render_targets, nullptr);
            state.device_context->ClearRenderTargetView(state.render_target.Get(), clear_color);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

            HRESULT result = state.swap_chain->Present(1, 0); // 1 = wait for one vsync
            // driver update or gpu reset
            bool is_device_lost = result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET;
            if (is_device_lost) {
                append_log(LogLevel::error, L"settings_device_lost", {{L"hresult", make_hresult_text(result)}});
                return false;
            }
            return true;
        }

        void request_frames(HWND window, SettingsWindowState& state) {
            state.frames_left = extra_frames_after_input;
            InvalidateRect(window, nullptr, FALSE); // WM_PAINT once the queue is empty
        }

        bool is_input_message(UINT message) {
            bool is_mouse = message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
            bool is_key = message >= WM_KEYFIRST && message <= WM_KEYLAST;
            bool is_focus = message == WM_SETFOCUS || message == WM_KILLFOCUS;
            return is_mouse || is_key || is_focus || message == WM_MOUSELEAVE;
        }

        LRESULT CALLBACK settings_window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
            if (message == WM_NCCREATE) {
                const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
                auto* state = new SettingsWindowState(); // owned by the window, deleted on WM_NCDESTROY
                state->window = window;
                state->app_window = static_cast<HWND>(create->lpCreateParams); // from open_settings_window
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
                return DefWindowProcW(window, message, wparam, lparam);
            }

            SettingsWindowState* state = settings_state_of(window);
            if (state == nullptr) {
                return DefWindowProcW(window, message, wparam, lparam);
            }

            if (message == WM_NCDESTROY) {
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                delete state;
                return DefWindowProcW(window, message, wparam, lparam);
            }

            if (state->imgui_started) {
                // TRUE: imgui used it, for example it set the cursor
                if (ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam)) {
                    return TRUE;
                }
            }
            if (is_input_message(message)) {
                request_frames(window, *state);
            }

            switch (message) {
            case WM_CREATE:
                return on_create(window, *state) ? 0 : -1; // -1: CreateWindowExW returns nullptr
            case WM_SIZE:
                if (wparam == SIZE_MINIMIZED) {
                    return 0; // no client area to draw
                }
                resize_swap_chain(*state, LOWORD(lparam), HIWORD(lparam));
                request_frames(window, *state);
                return 0;
            case WM_DPICHANGED: {
                apply_style(static_cast<float>(HIWORD(wparam)) / static_cast<float>(default_dpi));
                // size windows suggests for the new dpi
                const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
                SetWindowPos(window, nullptr, suggested->left, suggested->top,
                    suggested->right - suggested->left, suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
                request_frames(window, *state);
                return 0;
            }
            case WM_PAINT: {
                bool is_device_ok = draw_frame(*state);
                ValidateRect(window, nullptr); // stops the WM_PAINT repeat
                if (!is_device_ok) {
                    DestroyWindow(window); // deletes state; the next open makes a new device
                    return 0;
                }
                if (state->frames_left > 0) {
                    state->frames_left -= 1;
                    InvalidateRect(window, nullptr, FALSE);
                }
                return 0;
            }
            case WM_DISPLAYCHANGE:
                load_monitor_targets(*state);
                load_target_settings(*state);
                request_frames(window, *state);
                return 0;
            case WM_ACTIVATE:
                if (LOWORD(wparam) != WA_INACTIVE) {
                    reload_changed_settings(*state); // back from the editor
                    state->autostart_enabled = is_autostart_enabled(); // can change outside the app
                    request_frames(window, *state);
                }
                return DefWindowProcW(window, message, wparam, lparam); // sets the keyboard focus
            case open_config_message:
                open_config_file(*state);
                return 0;
            case WM_DESTROY:
                on_destroy(*state);
                //  a preview with no commit (closed during a drag): bars back to the file look
                PostMessageW(state->app_window, config_changed_message, 0, 0);
                append_log(LogLevel::info, L"settings_window_closed");
                return 0; // not PostQuitMessage: the app keeps running
            default:
                return DefWindowProcW(window, message, wparam, lparam);
            }
        }

        bool register_settings_window_class(HINSTANCE instance) {
            WNDCLASSEXW window_class = {};
            window_class.cbSize = sizeof(window_class);
            window_class.lpfnWndProc = settings_window_procedure;
            window_class.hInstance = instance;
            window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            // LR_SHARED: windows owns the handles, no DestroyIcon
            window_class.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(app_icon_id),
                IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
            window_class.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(app_icon_id),
                IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
            window_class.lpszClassName = settings_window_class;
            if (RegisterClassExW(&window_class) != 0) {
                return true;
            }
            // second open: the class stays registered after the first window closes
            return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        }
    } // namespace

    void open_settings_window(HWND app_window) {
        HWND existing = FindWindowW(settings_window_class, nullptr);
        if (existing != nullptr) {
            if (IsIconic(existing)) {
                ShowWindow(existing, SW_RESTORE); // minimised: back on screen first
            }
            SetForegroundWindow(existing);
            return;
        }

        HINSTANCE instance = GetModuleHandleW(nullptr);
        if (!register_settings_window_class(instance)) {
            append_log(LogLevel::error, L"settings_window_rejected", {
                {L"step", L"class"},
                {L"win32_error", std::to_wstring(GetLastError())}
            });
            return;
        }

        // monitor of the cursor: the tray menu was just there
        POINT cursor = {};
        GetCursorPos(&cursor);
        MONITORINFO monitor_info = {};
        monitor_info.cbSize = sizeof(monitor_info);
        GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor_info);
        const RECT& work_area = monitor_info.rcWork;

        // hidden on that monitor first, so the window takes the dpi of that monitor
        HWND window = CreateWindowExW(0, settings_window_class, settings_window_title, window_style,
            work_area.left, work_area.top, client_width_at_96_dpi, client_height_at_96_dpi,
            nullptr, nullptr, instance, app_window); // lpCreateParams, read on WM_NCCREATE
        if (window == nullptr) {
            append_log(LogLevel::error, L"settings_window_rejected", {
                {L"step", L"window"},
                {L"win32_error", std::to_wstring(GetLastError())}
            });
            return;
        }

        // client size at the dpi of the window, plus the frame
        const int dpi = static_cast<int>(GetDpiForWindow(window));
        RECT frame = {0, 0, MulDiv(client_width_at_96_dpi, dpi, default_dpi), MulDiv(client_height_at_96_dpi, dpi, default_dpi)};
        AdjustWindowRectExForDpi(&frame, window_style, FALSE, 0, static_cast<UINT>(dpi));
        const int width_pixels = frame.right - frame.left;
        const int height_pixels = frame.bottom - frame.top;

        // centred in the work area
        const int x = work_area.left + (work_area.right - work_area.left - width_pixels) / 2;
        const int y = work_area.top + (work_area.bottom - work_area.top - height_pixels) / 2;
        SetWindowPos(window, nullptr, x, y, width_pixels, height_pixels, SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
        append_log(LogLevel::info, L"settings_window_opened", {{L"dpi", std::to_wstring(dpi)}});
    }
} // namespace ninja