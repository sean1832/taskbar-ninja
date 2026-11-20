// explorer dll. app loads it into explorer.exe with a windows hook.
// inside explorer, it starts XAML diagnostics on its own process, finds the taskbar background and set a clear
// or the original brush when the app sets the clear or restore event.
// Referenced TranslucentTB ExplorerTAp, see https://github.com/TranslucentTB/TranslucentTB/tree/d4636e439865df0a1a1419db408e055740ce5c74/ExplorerTAP

#include "explorer_dll.h"

#include "log.h"

#include <windows.h>
#undef GetCurrentTime // clashes with winrt
#include <unknwn.h> // classic COM support
#include <ocidl.h>
#include <xamlOM.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>

#include <atomic>
#include <cwchar>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <format>

namespace ninja {
    namespace {
        namespace xaml = winrt::Windows::UI::Xaml;

        // {6B0F6E7C-3D1A-4C55-9C2E-6A5E2B7D9F11}
        constexpr CLSID explorer_class_id = {0x6b0f6e7c, 0x3d1a, 0x4c55, {0x9c, 0x2e, 0x6a, 0x5e, 0x2b, 0x7d, 0x9f, 0x11}};
        constexpr int max_initialize_attempts = 60;
        constexpr DWORD initialize_retry_ms = 500;

        /**
         * Brush the taskbar background shows.
         */
        enum class BackgroundMode {
            original, ///< Explorer's own brush
            clear,    ///< fully transparent
        };

        /**
         * One taskbar background shape and the brush Explorer gave it.
         */
        struct Background {
            InstanceHandle handle;              ///< XAML diagnostics handle, the only field valid on remove
            xaml::Shapes::Rectangle rectangle;  ///< BackgroundFill or BackgroundStroke
            xaml::Media::Brush original_fill;   ///< fill before the first change
        };

        HMODULE dll_module = nullptr;
        std::atomic<BackgroundMode> mode = BackgroundMode::original; // thread safe single item access

        // XAML thread only
        std::vector<Background> backgrounds;

        // written on the XAML thread, read on the signal thread
        std::mutex queue_mutex;
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

        // apply the brush
        void apply_mode(const Background& background, BackgroundMode next_mode) {
            if (next_mode == BackgroundMode::original) {
                background.rectangle.Fill(background.original_fill); // restore
                return;
            }
            xaml::Media::SolidColorBrush clear_brush;
            clear_brush.Color(winrt::Windows::UI::Color{0,0,0,0}); // ARGB
            background.rectangle.Fill(clear_brush);
        }

        std::wstring_view mode_name(BackgroundMode value) {
            switch (value) {
            case BackgroundMode::original:
                return L"original";
            case BackgroundMode::clear:
                return L"clear";
            }
            return L"unknown";
        }

        void apply_mode_to_all() {
            BackgroundMode next_mode = mode.load();
            for (const Background& background : backgrounds) {
                apply_mode(background, next_mode);
            }
            append_log(LogLevel::info, L"background_applied", {
                {L"mode", std::wstring(mode_name(next_mode))},
                {L"count", std::to_wstring(backgrounds.size())}
            });
        }

        void add_background(InstanceHandle handle, const xaml::Shapes::Rectangle& rectangle) {
            // mutex lock
            {
                std::lock_guard lock(queue_mutex);
                if (xaml_queue == nullptr) {
                    xaml_queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
                }
            }
            Background background{
                handle, rectangle, rectangle.Fill()
            };
            apply_mode(background, mode.load());
            backgrounds.push_back(background);
        }

        void remove_background(InstanceHandle handle) {
            std::erase_if(backgrounds, [handle](const Background& background) {
                return background.handle == handle;
            });
        }

        void wait_for_signals() {
            HANDLE clear_event = CreateEventW(nullptr, FALSE, FALSE, explorer_clear_event_name);
            HANDLE restore_event = CreateEventW(nullptr, false, false, explorer_restore_event_name);
            if (clear_event == nullptr || restore_event == nullptr) {
                append_log(LogLevel::error, L"explorer_signal_failed", {{L"win32_error", std::to_wstring(GetLastError())}});
                return;
            }
            HANDLE events[] = {clear_event, restore_event};
            while (true) {
                DWORD result = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                if (result == WAIT_OBJECT_0) {
                    mode = BackgroundMode::clear;
                } else if (result == WAIT_OBJECT_0 + 1) {
                    mode = BackgroundMode::original;
                } else {
                    append_log(LogLevel::error, L"explorer_signal_failed", {{L"win32_error", std::to_wstring(GetLastError())}});
                    return;
                }

                std::lock_guard lock(queue_mutex);
                // no taskbar found yet. New ones read the mode when added
                if (xaml_queue != nullptr) {
                    xaml_queue.TryEnqueue(apply_mode_to_all);
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
                        return S_OK;
                    }
                    if (type != L"Windows.UI.Xaml.Shapes.Rectangle") {
                        return S_OK;
                    }
                    if (name != L"BackgroundFill" && name != L"BackgroundStroke") {
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
                    add_background(element.Handle, rectangle);
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
