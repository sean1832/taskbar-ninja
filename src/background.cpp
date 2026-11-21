#include "background.h"

#include <windows.h>

#include "explorer_dll.h"

namespace ninja {
    namespace {
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
    } // namespace

    bool set_background(const BackgroundLook& look) {
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

    void restore_background() {
        // no event: no Explorer has the dll, so every background is original
        set_explorer_event(explorer_restore_event_name);
    }
} // namespace ninja
