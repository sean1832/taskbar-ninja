// explorer dll. app loads it into explorer.exe with a windows hook.
// inside explorer, it starts XAML diagnostics on its own process, finds the taskbar background, and sets
// the brush of the look the app writes, or the original brush when the app sets the restore event.
// Referenced TranslucentTB ExplorerTAp, see https://github.com/TranslucentTB/TranslucentTB/tree/d4636e439865df0a1a1419db408e055740ce5c74/ExplorerTAP

#include "explorer_dll.h"

#include "log.h"

#include <windows.h>
#undef GetCurrentTime // clashes with winrt
#include <unknwn.h> // classic COM support
#include <d2d1_1.h> // D2D1_COMPOSITE_MODE
#include <d2d1effects.h>
#include <ocidl.h>
#include <windows.graphics.effects.interop.h>
#include <xamlOM.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h> // IDesktopWindowXamlSourceNative

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>

#include <chrono>
#include <cstdint>
#include <cwchar>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <format>

namespace ninja {
    namespace {
        namespace xaml = winrt::Windows::UI::Xaml;
        namespace composition = winrt::Windows::UI::Composition;
        namespace numerics = winrt::Windows::Foundation::Numerics;
        namespace effects = winrt::Windows::Graphics::Effects;
        namespace effects_abi = ABI::Windows::Graphics::Effects;

        // {6B0F6E7C-3D1A-4C55-9C2E-6A5E2B7D9F11}
        constexpr CLSID explorer_class_id = {0x6b0f6e7c, 0x3d1a, 0x4c55, {0x9c, 0x2e, 0x6a, 0x5e, 0x2b, 0x7d, 0x9f, 0x11}};
        constexpr int max_initialize_attempts = 60;
        constexpr DWORD initialize_retry_ms = 500;

        /**
         * One taskbar background shape and the brush Explorer gave it.
         */
        struct Background {
            InstanceHandle handle;              ///< XAML diagnostics handle, the only field valid on remove
            xaml::Shapes::Rectangle rectangle;  ///< BackgroundFill or BackgroundStroke
            xaml::Media::Brush original_fill;   ///< fill before the first change
            bool is_stroke = false;             ///< BackgroundStroke, the thin edge line
        };

        /**
         * One taskbar: the XAML element that holds all of its content, and its window.
         */
        struct Taskbar {
            InstanceHandle frame_handle; ///< XAML diagnostics handle of its TaskbarFrame, the only field valid on remove
            xaml::UIElement content;     ///< content of the XAML source: icons, tray, clock, background
            HWND taskbar;                ///< Shell_TrayWnd or Shell_SecondaryTrayWnd
            AnimationRow row;            ///< state the content last went to. default = slid in
        };

        HMODULE dll_module = nullptr;

        // XAML thread only
        std::vector<Background> backgrounds;
        std::vector<InstanceHandle> source_handles; // every DesktopWindowXamlSource
        std::vector<Taskbar> bars;

        // written on the signal thread and the XAML thread
        std::mutex look_mutex;
        BackgroundLook look;
        AnimationTable animation_table; // last table from the app. empty = all content slid in
        winrt::Windows::System::DispatcherQueue xaml_queue = nullptr;

        std::wstring format_hresult(const HRESULT result) {
            return std::format(L"0x{:08X}", static_cast<unsigned>(result));
        }

        std::wstring take_bstr(BSTR text) {
            std::wstring result;
            if (text != nullptr) {
                result.assign(text, SysStringLen(text));
            }
            SysFreeString(text);
            return result;
        }

        bool has_parent_named(const xaml::FrameworkElement& element, std::wstring_view name) {
            xaml::DependencyObject parent = xaml::Media::VisualTreeHelper::GetParent(element);
            while (parent != nullptr) {
                auto framework_element = parent.try_as<xaml::FrameworkElement>();
                if (framework_element != nullptr && framework_element.Name() == name) {
                    return true;
                }
                parent = xaml::Media::VisualTreeHelper::GetParent(parent);
            }
            return false;
        }

        winrt::Windows::UI::Color make_color(std::uint32_t argb) {
            winrt::Windows::UI::Color color{};
            color.A = static_cast<std::uint8_t>((argb >> 24) & 0xFF);
            color.R = static_cast<std::uint8_t>((argb >> 16) & 0xFF);
            color.G = static_cast<std::uint8_t>((argb >> 8) & 0xFF);
            color.B = static_cast<std::uint8_t>(argb & 0xFF);
            return color;
        }

        /**
         * One Direct2D effect for the compositor: an effect id, its sources, and its properties by index.
         * @remarks One type for blur, flood and composite, which differ only in data.
         */
        struct Effect : winrt::implements<Effect, effects::IGraphicsEffect, effects::IGraphicsEffectSource,
                            effects_abi::IGraphicsEffectD2D1Interop> {
            GUID effect_id{};
            std::vector<effects::IGraphicsEffectSource> sources;
            std::vector<winrt::Windows::Foundation::IInspectable> properties; ///< index = Direct2D property index
            winrt::hstring name; ///< must differ per effect: the compositor finds each effect by name

            winrt::hstring Name() {
                return name;
            }

            void Name(const winrt::hstring& value) {
                name = value;
            }

            HRESULT STDMETHODCALLTYPE GetEffectId(GUID* id) noexcept override {
                *id = effect_id;
                return S_OK;
            }

            // only for animated properties, which these effects do not have
            HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(LPCWSTR, UINT*, effects_abi::GRAPHICS_EFFECT_PROPERTY_MAPPING*) noexcept override {
                return E_INVALIDARG;
            }

            HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT* count) noexcept override {
                *count = static_cast<UINT>(properties.size());
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE GetProperty(UINT index, ABI::Windows::Foundation::IPropertyValue** value) noexcept override {
                if (index >= properties.size()) {
                    return E_BOUNDS;
                }
                return winrt::get_unknown(properties[index])->QueryInterface(value);
            }

            HRESULT STDMETHODCALLTYPE GetSource(UINT index, effects_abi::IGraphicsEffectSource** source) noexcept override {
                if (index >= sources.size()) {
                    return E_BOUNDS;
                }
                winrt::copy_to_abi(sources[index], *reinterpret_cast<void**>(source));
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE GetSourceCount(UINT* count) noexcept override {
                *count = static_cast<UINT>(sources.size());
                return S_OK;
            }
        };

        /**
         * XAML brush that blurs what is behind the taskbar and lays a color on top.
         * @remarks Based on TranslucentTB XamlBlurBrush, see https://github.com/TranslucentTB/TranslucentTB/blob/release/ExplorerTAP/XamlBlurBrush.cpp
         */
        struct BlurBrush : xaml::Media::XamlCompositionBrushBaseT<BlurBrush> {
            composition::Compositor compositor;
            BackgroundLook blur_look;

            BlurBrush(composition::Compositor compositor_value, BackgroundLook look_value)
                : compositor(std::move(compositor_value)), blur_look(look_value) {
            }

            // clear at the inner edge, which faces the middle of the monitor, opaque after feather_fraction
            composition::CompositionLinearGradientBrush make_feather_mask() {
                composition::CompositionLinearGradientBrush gradient = compositor.CreateLinearGradientBrush();
                gradient.MappingMode(composition::CompositionMappingMode::Relative);
                // relative points: 0 and 1 are the two sides of the brush
                winrt::Windows::Foundation::Numerics::float2 inner_point{0.0f, 0.0f};
                winrt::Windows::Foundation::Numerics::float2 outer_point{0.0f, 1.0f};
                switch (blur_look.edge) {
                case TaskbarEdge::bottom:
                    break;
                case TaskbarEdge::top:
                    inner_point = {0.0f, 1.0f};
                    outer_point = {0.0f, 0.0f};
                    break;
                case TaskbarEdge::left:
                    inner_point = {1.0f, 0.0f};
                    outer_point = {0.0f, 0.0f};
                    break;
                case TaskbarEdge::right:
                    inner_point = {0.0f, 0.0f};
                    outer_point = {1.0f, 0.0f};
                    break;
                }
                gradient.StartPoint(inner_point);
                gradient.EndPoint(outer_point);
                gradient.ColorStops().Append(compositor.CreateColorGradientStop(0.0f, winrt::Windows::UI::Color{0, 0, 0, 0}));
                gradient.ColorStops().Append(compositor.CreateColorGradientStop(blur_look.feather_fraction, winrt::Windows::UI::Color{255, 0, 0, 0}));
                return gradient;
            }

            composition::CompositionEffectBrush make_effect_brush() {
                auto blur = winrt::make_self<Effect>();
                blur->name = L"Blur";
                blur->effect_id = CLSID_D2D1GaussianBlur;
                blur->sources.push_back(composition::CompositionEffectSourceParameter(L"backdrop"));
                blur->properties.push_back(winrt::Windows::Foundation::PropertyValue::CreateSingle(blur_look.blur_pixels));
                blur->properties.push_back(winrt::Windows::Foundation::PropertyValue::CreateUInt32(D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED));
                blur->properties.push_back(winrt::Windows::Foundation::PropertyValue::CreateUInt32(D2D1_BORDER_MODE_HARD));

                auto flood = winrt::make_self<Effect>();
                flood->name = L"Color";
                flood->effect_id = CLSID_D2D1Flood;
                winrt::Windows::UI::Color color = make_color(blur_look.color_argb);
                float color_rgba[] = {color.R / 255.0f, color.G / 255.0f, color.B / 255.0f, color.A / 255.0f};
                flood->properties.push_back(winrt::Windows::Foundation::PropertyValue::CreateSingleArray(color_rgba));

                // color over the blur
                auto composite = winrt::make_self<Effect>();
                composite->name = L"Composite";
                composite->effect_id = CLSID_D2D1Composite;
                composite->sources.push_back(*blur);
                composite->sources.push_back(*flood);
                composite->properties.push_back(winrt::Windows::Foundation::PropertyValue::CreateUInt32(D2D1_COMPOSITE_MODE_SOURCE_OVER));

                composition::CompositionEffectBrush effect_brush = compositor.CreateEffectFactory(*composite).CreateBrush();
                effect_brush.SetSourceParameter(L"backdrop", compositor.CreateBackdropBrush());
                return effect_brush;
            }

            void OnConnected() {
                if (CompositionBrush() != nullptr) {
                    return;
                }
                try {
                    composition::CompositionEffectBrush effect_brush = make_effect_brush();
                    if (blur_look.feather_fraction <= 0.0f) {
                        CompositionBrush(effect_brush);
                        return;
                    }
                    composition::CompositionMaskBrush mask_brush = compositor.CreateMaskBrush();
                    mask_brush.Source(effect_brush);
                    mask_brush.Mask(make_feather_mask());
                    CompositionBrush(mask_brush);
                } catch (...) {
                    append_log(LogLevel::error, L"explorer_blur_failed", {{L"hresult", format_hresult(winrt::to_hresult())}});
                }
            }

            void OnDisconnected() {
                composition::CompositionBrush brush = CompositionBrush();
                if (brush == nullptr) {
                    return;
                }
                brush.Close();
                CompositionBrush(nullptr);
            }
        };

        xaml::Media::Brush make_solid_brush(std::uint32_t argb) {
            xaml::Media::SolidColorBrush brush;
            brush.Color(make_color(argb));
            return brush;
        }

        xaml::Media::Brush make_fill_brush(const Background& background, const BackgroundLook& next_look) {
            switch (next_look.style) {
            case BackgroundStyle::original:
                return background.original_fill;
            case BackgroundStyle::clear:
                return make_solid_brush(0);
            case BackgroundStyle::tint:
                return make_solid_brush(next_look.color_argb);
            case BackgroundStyle::acrylic: {
                xaml::Media::AcrylicBrush brush;
                // Backdrop: what is behind the XAML, which is the desktop, as the taskbar window is clear
                brush.BackgroundSource(xaml::Media::AcrylicBackgroundSource::Backdrop);
                brush.TintColor(make_color(next_look.color_argb));
                brush.FallbackColor(make_color(next_look.color_argb));
                return brush;
            }
            case BackgroundStyle::blur: {
                composition::Compositor compositor =
                    xaml::Hosting::ElementCompositionPreview::GetElementVisual(background.rectangle).Compositor();
                return winrt::make<BlurBrush>(compositor, next_look);
            }
            }
            return background.original_fill;
        }

        void apply_look(const Background& background, const BackgroundLook& next_look) {
            // the edge line stays clear under every style but the original
            bool is_hidden_stroke = background.is_stroke && next_look.style != BackgroundStyle::original;
            if (is_hidden_stroke) {
                background.rectangle.Fill(make_solid_brush(0));
                return;
            }
            background.rectangle.Fill(make_fill_brush(background, next_look));
        }

        // runs on the XAML thread through the dispatcher queue
        void apply_look_to_all(BackgroundLook next_look) {
            try {
                for (const Background& background : backgrounds) {
                    apply_look(background, next_look);
                }
                append_log(LogLevel::info, L"background_applied", {
                    {L"style", std::wstring(background_style_name(next_look.style))},
                    {L"count", std::to_wstring(backgrounds.size())}
                });
            } catch (...) {
                append_log(LogLevel::error, L"explorer_apply_failed", {{L"hresult", format_hresult(winrt::to_hresult())}});
            }
        }

        void add_background(InstanceHandle handle, const xaml::Shapes::Rectangle& rectangle, bool is_stroke) {
            BackgroundLook current_look;
            {
                std::lock_guard lock(look_mutex);
                if (xaml_queue == nullptr) {
                    xaml_queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
                }
                current_look = look;
            }
            Background background{handle, rectangle, rectangle.Fill(), is_stroke};
            apply_look(background, current_look);
            backgrounds.push_back(background);
        }

        void remove_background(InstanceHandle handle) {
            std::erase_if(backgrounds, [handle](const Background& background) {
                return background.handle == handle;
            });
        }

        // move in XAML units that puts the content past the docked edge
        // XAML units, not pixels: a 72 pixel bar is 48 units at 150 % scale
        numerics::float3 make_out_translation(const xaml::UIElement& content, TaskbarEdge edge) {
            auto element = content.as<xaml::FrameworkElement>();
            float width_xaml_units = static_cast<float>(element.ActualWidth());
            float height_xaml_units = static_cast<float>(element.ActualHeight());
            switch (edge) {
            case TaskbarEdge::bottom:
                return {0.0f, height_xaml_units, 0.0f};
            case TaskbarEdge::top:
                return {0.0f, -height_xaml_units, 0.0f};
            case TaskbarEdge::left:
                return {-width_xaml_units, 0.0f, 0.0f};
            case TaskbarEdge::right:
                return {width_xaml_units, 0.0f, 0.0f};
            }
            return {0.0f, height_xaml_units, 0.0f};
        }

        [[nodiscard]] bool slide_content(const Taskbar& bar, const AnimationRow& row) {
            // layout overwrites Offset, but not Translation
            // see https://learn.microsoft.com/en-us/uwp/api/windows.ui.xaml.hosting.elementcompositionpreview.setistranslationenabled
            xaml::Hosting::ElementCompositionPreview::SetIsTranslationEnabled(bar.content, true);
            composition::Visual visual = xaml::Hosting::ElementCompositionPreview::GetElementVisual(bar.content);

            numerics::float3 end_translation{0.0f, 0.0f, 0.0f};
            if (row.is_slid_out) {
                end_translation = make_out_translation(bar.content, row.edge);
                bool has_size = end_translation.x != 0.0f || end_translation.y != 0.0f;
                if (!has_size) {
                    return false;
                }
            }

            if (row.duration_ms == 0) {
                visual.StopAnimation(L"Translation");
                visual.Properties().InsertVector3(L"Translation", end_translation);
                return true;
            }

            // see https://learn.microsoft.com/en-us/uwp/api/windows.ui.composition.compositor.createcubicbeziereasingfunction
            composition::Compositor compositor = visual.Compositor();
            composition::CubicBezierEasingFunction easing = compositor.CreateCubicBezierEasingFunction(
                {row.curve.start_control_x, row.curve.start_control_y},
                {row.curve.end_control_x, row.curve.end_control_y}
            );
            // one keyframe at the end. The slide starts from where the content is, thus a turn-around has no jump
            // see https://learn.microsoft.com/en-us/uwp/api/windows.ui.composition.vector3keyframeanimation
            composition::Vector3KeyFrameAnimation animation = compositor.CreateVector3KeyFrameAnimation();
            animation.InsertKeyFrame(1.0f, end_translation, easing);
            // see https://learn.microsoft.com/en-us/uwp/api/windows.ui.composition.keyframeanimation.duration
            animation.Duration(std::chrono::milliseconds(row.duration_ms));
            visual.StartAnimation(L"Translation", animation);
            return true;
        }

        AnimationRow find_row(const AnimationTable& table, HWND taskbar) {
            std::size_t row_count = table.row_count;
            if (row_count > max_animation_rows) {
                row_count = max_animation_rows;
            }
            for (std::size_t i = 0; i < row_count; ++i) {
                if (table.rows[i].taskbar_window == reinterpret_cast<std::uintptr_t>(taskbar)) {
                    return table.rows[i];
                }
            }
            return AnimationRow{};
        }

        void apply_animation_table(const AnimationTable& table) {
            for (Taskbar& bar: bars) {
                AnimationRow row = find_row(table, bar.taskbar);
                bool is_same = row.is_slid_out == bar.row.is_slid_out && row.edge == bar.row.edge;
                if (is_same) {
                    continue;
                }
                // try for each taskbar: one bar should not stop the other
                try {
                    if (slide_content(bar, row)) {
                        bar.row = row;
                    }
                } catch (...) {
                    append_log(LogLevel::error, L"explorer_slide_failed", {{L"hresult", format_hresult(winrt::to_hresult())}});
                }
            }
        }

        // taskbar window of the XAML source whose content is this element, or nullptr
        // method from TranslucentTB, see https://github.com/TranslucentTB/TranslucentTB/blob/d4636e439865df0a1a1419db408e055740ce5c74/ExplorerTAP/visualtreewatcher.cpp#L45-L72
        HWND find_taskbar_of_content(IXamlDiagnostics& diagnostics, const xaml::UIElement& element) {
            for (InstanceHandle source_handle : source_handles) {
                try {
                    winrt::Windows::Foundation::IInspectable object;
                    winrt::check_hresult(diagnostics.GetIInspectableFromHandle(
                       source_handle, reinterpret_cast<::IInspectable**>(winrt::put_abi(object))
                    ));
                    auto source = object.as<xaml::Hosting::DesktopWindowXamlSource>();
                    if (source.Content() != element) {
                        continue;
                    }
                    // island window: child window that hosts the XAML. its top parent is the taskbar
                    // see https://learn.microsoft.com/en-us/windows/win32/api/windows.ui.xaml.hosting.desktopwindowxamlsource/nf-windows-ui-xaml-hosting-desktopwindowxamlsource-idesktopwindowxamlsourcenative-get_windowhandle
                    HWND island_window = nullptr;
                    winrt::check_hresult(source.as<IDesktopWindowXamlSourceNative>()->get_WindowHandle(&island_window));
                    return GetAncestor(island_window, GA_ROOT);
                }catch (...) {
                    // a source of another thread or a closed source: not ours
                }
            }
            return nullptr;
        }

        void add_bar(IXamlDiagnostics& diagnostics, InstanceHandle frame_handle, const xaml::DependencyObject& frame) {
            // the content is a parent of the frame, but not the top parent
            xaml::DependencyObject ancestor = xaml::Media::VisualTreeHelper::GetParent(frame);
            while (ancestor != nullptr) {
                HWND taskbar = nullptr;
                auto element = ancestor.try_as<xaml::UIElement>();
                if (element != nullptr) {
                    taskbar = find_taskbar_of_content(diagnostics, element);
                }
                if (taskbar != nullptr) {
                    AnimationTable current_table;
                    {
                        std::lock_guard lock(look_mutex);
                        // the frame arrives before its background, so the queue is set here
                        if (xaml_queue == nullptr) {
                            xaml_queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
                        }
                        current_table = animation_table;
                    }
                    Taskbar bar {
                        frame_handle, element, taskbar, AnimationRow{}
                    };
                    AnimationRow row = find_row(current_table, taskbar);
                    // a bar that appears later goes to its state at once
                    row.duration_ms = 0;
                    if (row.is_slid_out && slide_content(bar, row)) {
                        bar.row = row;
                    }
                    // a new frame for a known taskbar takes its place
                    std::erase_if(bars, [taskbar](const Taskbar& known) {
                        return known.taskbar == taskbar;
                    });
                    bars.push_back(bar);
                    append_log(LogLevel::info, L"explorer_taskbar_found", {{L"count", std::to_wstring(bars.size())}});
                    return;
                }
                ancestor = xaml::Media::VisualTreeHelper::GetParent(ancestor);
            }
            append_log(LogLevel::warning, L"explorer_bar_content_missing");
        }

        void remove_bar(InstanceHandle handle) {
            std::erase(source_handles, handle);
            std::erase_if(bars, [handle](const Taskbar& bar) {
                return bar.frame_handle == handle;
            });
        }

        void wait_for_signals() {
            HANDLE apply_event = CreateEventW(nullptr, FALSE, FALSE, explorer_apply_event_name);
            HANDLE restore_event = CreateEventW(nullptr, FALSE, FALSE, explorer_restore_event_name);
            // never closed, like the events: the app opens it by name
            HANDLE look_memory = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                sizeof(BackgroundLook), explorer_look_memory_name);
            HANDLE animate_event = CreateEventW(nullptr, FALSE, FALSE, explorer_animate_event_name);
            // never close
            HANDLE animation_memory = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                sizeof(AnimationTable), explorer_animation_memory_name);
            if (animate_event == nullptr || animation_memory == nullptr) {
                append_log(LogLevel::error, L"explorer_signal_failed", {{L"win32_error", std::to_wstring(GetLastError())}});
                return;
            }
            auto* shared_table = static_cast<const AnimationTable*>(
                MapViewOfFile(animation_memory, FILE_MAP_READ, 0, 0, sizeof(AnimationTable)));
            if (shared_table == nullptr) {
                append_log(LogLevel::error, L"explorer_signal_failed", {{L"win32_error", std::to_wstring(GetLastError())}});
                return;
            }
            if (apply_event == nullptr || restore_event == nullptr || look_memory == nullptr) {
                append_log(LogLevel::error, L"explorer_signal_failed", {{L"win32_error", std::to_wstring(GetLastError())}});
                return;
            }
            auto* shared_look = static_cast<const BackgroundLook*>(
                MapViewOfFile(look_memory, FILE_MAP_READ, 0, 0, sizeof(BackgroundLook)));
            if (shared_look == nullptr) {
                append_log(LogLevel::error, L"explorer_signal_failed", {{L"win32_error", std::to_wstring(GetLastError())}});
                return;
            }

            HANDLE events[] = {apply_event, restore_event, animate_event};
            while (true) {
                DWORD result = WaitForMultipleObjects(3, events, FALSE, INFINITE);
                bool is_apply = result == WAIT_OBJECT_0;
                bool is_restore = result == WAIT_OBJECT_0 + 1;
                bool is_animate = result == WAIT_OBJECT_0 + 2;
                if (!is_apply && !is_restore && !is_animate) {
                    append_log(LogLevel::error, L"explorer_signal_failed", {{L"win32_error", std::to_wstring(GetLastError())}});
                    return;
                }

                std::lock_guard lock(look_mutex);
                if (is_apply) {
                    look = *shared_look;
                }
                if (is_animate) {
                    animation_table = *shared_table;
                }
                // restore: original look, all content slid in at once
                if (is_restore) {
                    look = BackgroundLook{};
                    animation_table = AnimationTable{};
                }
                // no taskbar found yet: new ones read the look and the table when added
                if (xaml_queue == nullptr) {
                    continue;
                }
                bool is_look_changed = is_apply || is_restore;
                if (is_look_changed) {
                    xaml_queue.TryEnqueue([next_look = look] {
                        apply_look_to_all(next_look);
                    });
                }
                bool is_table_changed = is_animate || is_restore;
                if (is_table_changed) {
                    xaml_queue.TryEnqueue([next_table = animation_table] {
                        apply_animation_table(next_table);
                    });
                }
            }
        }

        struct TreeWatcher : winrt::implements<TreeWatcher, IVisualTreeServiceCallback2> {
            winrt::com_ptr<IXamlDiagnostics> diagnostics;

            HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation, VisualElement element, VisualMutationType mutation) noexcept override {
                try {
                    // the strings are ours to free
                    take_bstr(element.SrcInfo.FileName);
                    take_bstr(element.SrcInfo.Hash);
                    std::wstring name = take_bstr(element.Name);
                    std::wstring type = take_bstr(element.Type);
                    if (mutation == Remove) {
                        remove_background(element.Handle);
                        remove_bar(element.Handle);
                        return S_OK;
                    }
                    if (type == L"Windows.UI.Xaml.Hosting.DesktopWindowXamlSource") {
                        source_handles.push_back(element.Handle);
                        return S_OK;
                    }

                    if (type == L"Taskbar.TaskbarFrame") {
                        winrt::Windows::Foundation::IInspectable frame;
                        winrt::check_hresult(diagnostics->GetIInspectableFromHandle(
                            element.Handle, reinterpret_cast<::IInspectable**>(winrt::put_abi(frame))));
                        add_bar(*diagnostics, element.Handle, frame.as<xaml::DependencyObject>());
                        return S_OK;
                    }

                    if (type != L"Windows.UI.Xaml.Shapes.Rectangle") {
                        return S_OK;
                    }
                    bool is_fill = name == L"BackgroundFill";
                    bool is_stroke = name == L"BackgroundStroke";
                    if (!is_fill && !is_stroke) {
                        return S_OK;
                    }

                    winrt::Windows::Foundation::IInspectable object;
                    winrt::check_hresult(diagnostics->GetIInspectableFromHandle(
                        element.Handle, reinterpret_cast<::IInspectable**>(winrt::put_abi(object))));
                    auto rectangle = object.as<xaml::Shapes::Rectangle>();
                    // same names exist outside the taskbar
                    if (!has_parent_named(rectangle, L"TaskbarFrame")) {
                        return S_OK;
                    }
                    add_background(element.Handle, rectangle, is_stroke);
                    return S_OK;
                } catch (...) {
                    append_log(LogLevel::error, L"explorer_tree_change_failed", {{L"hresult", format_hresult(winrt::to_hresult())}});
                    return winrt::to_hresult();
                }
            }

            HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle, VisualElementState, LPCWSTR) noexcept override {
                return S_OK;
            }
        };

        void watch_tree(winrt::com_ptr<TreeWatcher> watcher) {
            HRESULT result = watcher->diagnostics.as<IVisualTreeService3>()->AdviseVisualTreeChange(watcher.get());
            if (FAILED(result)) {
                append_log(LogLevel::error, L"explorer_watch_failed", {{L"hresult", format_hresult(result)}});
                return;
            }
            // never closed: the event lives as long as this Explorer, so the app sees a dll that is already loaded
            HANDLE ready_event = CreateEventW(nullptr, TRUE, FALSE, explorer_ready_event_name);
            SetEvent(ready_event);
            append_log(LogLevel::info, L"explorer_ready");
        }

        /**
         * COM object that XAML diagnostics creates inside Explorer and hands its site to.
         */
        struct ExplorerSite : winrt::implements<ExplorerSite, IObjectWithSite> {
            winrt::com_ptr<IUnknown> site;

            HRESULT STDMETHODCALLTYPE SetSite(IUnknown* unknown) noexcept override {
                try {
                    site.copy_from(unknown);
                    if (site == nullptr) {
                        return S_OK;
                    }
                    auto watcher = winrt::make_self<TreeWatcher>();
                    watcher->diagnostics = site.as<IXamlDiagnostics>();
                    // advise from another thread, as TranslucentTB does, to avoid hangs
                    std::thread(watch_tree, watcher).detach();
                    return S_OK;
                } catch (...) {
                    return winrt::to_hresult();
                }
            }

            HRESULT STDMETHODCALLTYPE GetSite(REFIID interface_id, void** result) noexcept override {
                *result = nullptr;
                if (site == nullptr) {
                    return E_FAIL;
                }
                return site->QueryInterface(interface_id, result);
            }
        };

        struct ExplorerClassFactory : winrt::implements<ExplorerClassFactory, IClassFactory> {
            HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID interface_id, void** result) noexcept override {
                try {
                    *result = nullptr;
                    if (outer != nullptr) {
                        return CLASS_E_NOAGGREGATION;
                    }
                    return winrt::make<ExplorerSite>().as<IUnknown>()->QueryInterface(interface_id, result);
                } catch (...) {
                    return winrt::to_hresult();
                }
            }

            HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override {
                return S_OK;
            }
        };

        using InitializeFunction = decltype(&InitializeXamlDiagnosticsEx);

        HRESULT initialize_once(InitializeFunction initialize, int attempt) {
            wchar_t dll_path[MAX_PATH] = {};
            GetModuleFileNameW(dll_module, dll_path, MAX_PATH);
            // other endpoint names fail with 0x80070490
            std::wstring connection = L"VisualDiagConnection" + std::to_wstring(attempt);
            DWORD explorer_id = GetCurrentProcessId();
            HRESULT result = E_FAIL;
            // one session per thread, so each attempt runs on a new thread
            std::thread([&result, initialize, &connection, explorer_id, &dll_path] {
                result = initialize(connection.c_str(), explorer_id, nullptr, dll_path, explorer_class_id, nullptr);
            }).join();
            return result;
        }

        DWORD WINAPI install(void*) {
            // stay loaded after the app removes the hook, so no thread runs in freed code
            HMODULE pinned = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&install), &pinned);
            set_log_role(L"explorer");
            std::thread(wait_for_signals).detach();

            HMODULE xaml_module = LoadLibraryExW(L"Windows.UI.Xaml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (xaml_module == nullptr) {
                append_log(LogLevel::error, L"explorer_load_failed", {{L"win32_error", std::to_wstring(GetLastError())}});
                return 1;
            }
            auto initialize = reinterpret_cast<InitializeFunction>(GetProcAddress(xaml_module, "InitializeXamlDiagnosticsEx"));
            if (initialize == nullptr) {
                append_log(LogLevel::error, L"explorer_load_failed", {{L"win32_error", std::to_wstring(GetLastError())}});
                return 1;
            }

            // XAML may not run yet right after explorer starts
            HRESULT result = E_FAIL;
            for (int attempt = 1; attempt <= max_initialize_attempts; ++attempt) {
                result = initialize_once(initialize, attempt);
                if (SUCCEEDED(result)) {
                    append_log(LogLevel::info, L"explorer_loaded", {{L"attempt", std::to_wstring(attempt)}});
                    return 0;
                }
                Sleep(initialize_retry_ms);
            }
            append_log(LogLevel::error, L"explorer_load_failed", {{L"hresult", format_hresult(result)}});
            return 1;
        }

        bool is_in_explorer() {
            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            const wchar_t* slash = std::wcsrchr(path, L'\\');
            if (slash == nullptr) {
                return false;
            }
            return _wcsicmp(slash + 1, L"explorer.exe") == 0;
        }
    } // namespace
} // namespace ninja

/// Hook procedure the app installs on the taskbar thread. It only loads this dll into explorer.
extern "C" LRESULT CALLBACK hook_procedure(int code, WPARAM wparam, LPARAM lparam) {
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

/// COM entry point that XAML diagnostics calls to create the explorer site.
STDAPI DllGetClassObject(REFCLSID class_id, REFIID interface_id, void** result) {
    try {
        *result = nullptr;
        if (class_id != ninja::explorer_class_id) {
            return CLASS_E_CLASSNOTAVAILABLE;
        }
        return winrt::make<ninja::ExplorerClassFactory>().as<IUnknown>()->QueryInterface(interface_id, result);
    } catch (...) {
        return winrt::to_hresult();
    }
}

/// COM entry point. The dll is pinned, so it never unloads.
STDAPI DllCanUnloadNow() {
    return S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }
    ninja::dll_module = instance;
    DisableThreadLibraryCalls(instance);
    // the app also loads this dll, only to find hook_procedure
    if (!ninja::is_in_explorer()) {
        return TRUE;
    }
    HANDLE thread = CreateThread(nullptr, 0, ninja::install, nullptr, 0, nullptr);
    if (thread != nullptr) {
        CloseHandle(thread);
    }
    return TRUE;
}
