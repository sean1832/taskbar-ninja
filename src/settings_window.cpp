#include "settings_window.h"

#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <wrl/client.h>

#include <cwchar>
#include <string>

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

#include "config.h"
#include "log.h"

// imgui leaves it out of its header, so the header needs no windows.h
// See third_party/imgui/backends/imgui_impl_win32.h
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace ninja {
    namespace {
        using Microsoft::WRL::ComPtr;

        constexpr wchar_t settings_window_class[] = L"taskbar-ninja-settings";
        constexpr wchar_t settings_window_title[] = L"Taskbar Ninja Settings";

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

        // reload_settings_window to the window itself
        constexpr UINT settings_reload_message = WM_APP + 1;

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
         * State of the open settings window.
         * @remarks Made on WM_NCCREATE, deleted on WM_NCDESTROY. The window holds a pointer to it.
         */
        struct SettingsWindowState {
            HWND app_window = nullptr; // gets settings_commited_message
            std::wstring config_path;
            MonitorSettings monitor_settings; // [default] keys as the file has them
            BackgroundSettings background_settings; // background key as the file has them
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

        void load_settings(SettingsWindowState& state) {
            state.config_path = config_path();
            // empty model name: [default] only
            state.monitor_settings = read_monitor_settings(state.config_path, L"").value_or(MonitorSettings{});
            state.background_settings = read_background_settings(state.config_path).value_or(BackgroundSettings{});
        }

        bool on_create(HWND window, SettingsWindowState& state) {
            load_settings(state);
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
            ImGui::TextUnformatted("Taskbar Ninja settings");
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
            case settings_reload_message:
                load_settings(*state);
                request_frames(window, *state);
                return 0;
            case WM_DESTROY:
                on_destroy(*state);
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

    void reload_settings_window() {
        HWND window = FindWindowW(settings_window_class, nullptr);
        if (window == nullptr) {
            return; // closed: the next open reads the file
        }
        PostMessageW(window, settings_reload_message, 0, 0);
    }
} // namespace ninja