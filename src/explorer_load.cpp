#include "explorer_load.h"

#include <windows.h>

#include <atomic>
#include <string>

#include "explorer_dll.h"
#include "log.h"

namespace ninja {
    namespace {
        // dll tries XAML for 30s, plus a margin
        constexpr DWORD max_ready_wait_ms = 35000;
        constexpr UINT taskbar_message_timeout_ms = 2000;

        // one load at a time
        std::atomic<bool> is_loading = false;
        // written only while it_loading is false, ready by the worker
        HWND ready_window = nullptr;
        UINT ready_message_id = 0;

        // just read GetLastError and log
        void log_load_rejected(const wchar_t* step) {
            append_log(LogLevel::error, L"explorer_load_rejected", {
                {L"step", step},
                {L"win32_error", std::to_wstring(GetLastError())}
            });
        }

        // empty when the exe path does not fit
        std::wstring make_explorer_path() {
            wchar_t exe_path[MAX_PATH] = {};
            DWORD length = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
            if (length == 0 || length == MAX_PATH) {
                return {};
            }
            std::wstring path = exe_path;
            std::wstring folder = path.substr(0, path.find_last_of(L'\\') + 1);
            return folder + explorer_file_name;
        }

        // true once the dll watches the taskbar XAML
        bool hook_taskbar_thread(HMODULE dll, HOOKPROC hook_procedure) {
            HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
            if (taskbar == nullptr) {
                log_load_rejected(L"taskbar");
                return false;
            }
            DWORD taskbar_thread_id = GetWindowThreadProcessId(taskbar, nullptr);

            HANDLE ready_event = CreateEventW(nullptr, TRUE, FALSE, explorer_ready_event_name);
            if (ready_event == nullptr) {
                log_load_rejected(L"ready_event");
                return false;
            }

            HHOOK hook = SetWindowsHookExW(WH_CALLWNDPROC, hook_procedure, dll, taskbar_thread_id);
            if (hook == nullptr) {
                log_load_rejected(L"hook");
                CloseHandle(ready_event);
                return false;
            }

            ULONGLONG started_at_ms = GetTickCount64();
            // a sent message makes the taskbar thread run the hook, which loads the dll
            DWORD_PTR ignored_result = 0;
            SendMessageTimeoutW(taskbar, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, taskbar_message_timeout_ms, &ignored_result);
            DWORD wait_result = WaitForSingleObject(ready_event, max_ready_wait_ms);
            ULONGLONG finished_at_ms = GetTickCount64();

            // the dll pins itself, so the unhook does not unload it
            UnhookWindowsHookEx(hook);
            CloseHandle(ready_event);

            bool is_ready = wait_result == WAIT_OBJECT_0;
            if (!is_ready) {
                append_log(LogLevel::error, L"explorer_load_rejected", {
                    {L"step", L"ready"},
                    {L"max_wait_ms", std::to_wstring(max_ready_wait_ms)}
                });
                return false;
            }
            append_log(LogLevel::info, L"explorer_load_done", {
                {L"elapsed_ms", std::to_wstring(finished_at_ms - started_at_ms)}
            });
            return true;
        }

        // true once the dll watches the taskbar XAML
        bool load_explorer_dll() {
            std::wstring dll_path = make_explorer_path();
            if (dll_path.empty()) {
                append_log(LogLevel::error, L"explorer_load_rejected", {
                    {L"step", L"path"},
                    {L"message", L"exe path must be shorter than MAX_PATH"}
                });
                return false;
            }

            // DllMain does nothing outside Explorer
            HMODULE dll = LoadLibraryW(dll_path.c_str());
            if (dll == nullptr) {
                log_load_rejected(L"library");
                return false;
            }
            auto hook_procedure = reinterpret_cast<HOOKPROC>(GetProcAddress(dll, explorer_hook_procedure_name));
            if (hook_procedure == nullptr) {
                log_load_rejected(L"hook_procedure");
                FreeLibrary(dll);
                return false;
            }

            bool is_ready = hook_taskbar_thread(dll, hook_procedure);
            FreeLibrary(dll);
            return is_ready;
        }

        DWORD WINAPI run_load_worker(void*) {
            bool is_ready = load_explorer_dll();
            // the app sends the look and the table, from its own state at that time
            if (is_ready) {
                PostMessageW(ready_window, ready_message_id, 0, 0);
            }
            is_loading = false;
            return 0;
        }
    } // namespace

    // the dll keeps the ready event open, so it exists only while Explorer has the dll
    bool is_explorer_ready() {
        HANDLE ready_event = OpenEventW(SYNCHRONIZE, FALSE, explorer_ready_event_name);
        if (ready_event == nullptr) {
            return false;
        }
        bool is_ready = WaitForSingleObject(ready_event, 0) == WAIT_OBJECT_0;
        CloseHandle(ready_event);
        return is_ready;
    }

    void load_explorer(HWND window, UINT ready_message) {
        // a ready dll would post at once, and a caller that sends on the message would loop
        if (is_explorer_ready()) {
            return;
        }
        bool was_loading = is_loading.exchange(true);
        if (was_loading) {
            return;
        }
        ready_window = window;
        ready_message_id = ready_message;
        append_log(LogLevel::info, L"explorer_load_started");
        // worker thread: the load waits up to 35 s, and the watchdog ends a UI thread that hangs 3 s
        HANDLE worker = CreateThread(nullptr, 0, run_load_worker, nullptr, 0, nullptr);
        if (worker == nullptr) {
            log_load_rejected(L"thread");
            is_loading = false;
            return;
        }
        CloseHandle(worker);
    }
} // namespace ninja