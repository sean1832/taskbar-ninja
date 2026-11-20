#include "background.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>

#include "explorer_dll.h"
#include "log.h"

namespace ninja {
    namespace {
        // dll tries XAML for 30 s, plus a margin
        constexpr DWORD max_ready_wait_ms = 35000;
        constexpr UINT taskbar_message_timeout_ms = 2000;

        // false after a restore, so a slow load cannot apply a look again
        std::atomic<bool> is_look_wanted = false;
        // last look from set_background, for the load worker
        std::mutex wanted_look_mutex;
        BackgroundLook wanted_look;
        // one load at a time
        std::atomic<bool> is_loading = false;

        // false when the dll has not made the event, so no Explorer has the dll
        bool set_explorer_event(const wchar_t* event_name) {
            HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, event_name);
            if (event == nullptr) {
                return false;
            }
            SetEvent(event);
            CloseHandle(event);
            return true;
        }

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

        // reads GetLastError, so call it right after the rejected call
        void log_load_rejected(const wchar_t* step) {
            DWORD error = GetLastError();
            append_log(LogLevel::error, L"background_load_rejected", {
                {L"step", step},
                {L"win32_error", std::to_wstring(error)}
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
                append_log(LogLevel::error, L"background_load_rejected", {
                    {L"step", L"ready"},
                    {L"max_wait_ms", std::to_wstring(max_ready_wait_ms)}
                });
                return false;
            }
            append_log(LogLevel::info, L"background_loaded", {
                {L"elapsed_ms", std::to_wstring(finished_at_ms - started_at_ms)}
            });
            return true;
        }

        // true once the dll watches the taskbar XAML
        bool load_explorer_dll() {
            std::wstring dll_path = make_explorer_path();
            if (dll_path.empty()) {
                append_log(LogLevel::error, L"background_load_rejected", {
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

        // false when the dll has not made the memory, so no Explorer has the dll
        bool send_look(const BackgroundLook& look) {
            HANDLE look_memory = OpenFileMappingW(FILE_MAP_WRITE, FALSE, explorer_look_memory_name);
            if (look_memory == nullptr) {
                return false;
            }
            auto* shared_look = static_cast<BackgroundLook*>(
                MapViewOfFile(look_memory, FILE_MAP_WRITE, 0, 0, sizeof(BackgroundLook)));
            if (shared_look == nullptr) {
                CloseHandle(look_memory);
                return false;
            }
            *shared_look = look;
            UnmapViewOfFile(shared_look);
            CloseHandle(look_memory);
            // look first, then the event, so the dll reads the new look
            return set_explorer_event(explorer_apply_event_name);
        }

        DWORD WINAPI run_load_worker(void*) {
            bool is_ready = load_explorer_dll();
            // a restore during the load wins
            if (is_ready && is_look_wanted) {
                BackgroundLook look;
                {
                    std::lock_guard lock(wanted_look_mutex);
                    look = wanted_look;
                }
                send_look(look);
            }
            is_loading = false;
            return 0;
        }
    } // namespace

    void set_background(const BackgroundLook& look) {
        {
            std::lock_guard lock(wanted_look_mutex);
            wanted_look = look;
        }
        is_look_wanted = true;
        if (is_explorer_ready()) {
            send_look(look);
            return;
        }

        // a running load applies the wanted look when it is done
        bool was_loading = is_loading.exchange(true);
        if (was_loading) {
            return;
        }
        append_log(LogLevel::info, L"background_load_started");
        // worker thread: the load waits up to 35 s, and the watchdog ends a UI thread that hangs 3 s
        HANDLE worker = CreateThread(nullptr, 0, run_load_worker, nullptr, 0, nullptr);
        if (worker == nullptr) {
            log_load_rejected(L"thread");
            is_loading = false;
            return;
        }
        CloseHandle(worker);
    }

    void restore_background() {
        is_look_wanted = false;
        // no event: no Explorer has the dll, so every background is original
        set_explorer_event(explorer_restore_event_name);
    }
} // namespace ninja