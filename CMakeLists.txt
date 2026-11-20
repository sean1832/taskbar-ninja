cmake_minimum_required(VERSION 3.20)
project(taskbar_ninja LANGUAGES CXX RC) # RC = resource compiler for the tray icon

if(NOT MSVC)
    message(FATAL_ERROR "compiler must be MSVC (CLion: Visual Studio toolchain)")
endif()

set(CMAKE_CXX_STANDARD 20) # C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# static runtime: exe needs no runtime dlls
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# app version; CI sets it from the git tag, for example -DTASKBAR_NINJA_VERSION=1.2.0
set(TASKBAR_NINJA_VERSION "dev" CACHE STRING "version shown in the tray tooltip")

# WIN32 = no console window
add_executable(taskbar-ninja WIN32
        src/main.cpp
        src/exit_code.h
        src/log.h
        src/log.cpp
        src/taskbar.cpp
        src/taskbar.h
        src/tray.cpp
        src/tray.h
        src/app.cpp
        src/app.h
        src/watchdog.cpp
        src/watchdog.h
        src/monitor.cpp
        src/monitor.h
        src/config.cpp
        src/config.h
        src/visibility.cpp
        src/visibility.h
        src/edge.cpp
        src/edge.h
        src/workarea.cpp
        src/workarea.h
        src/hold.cpp
        src/hold.h
        src/panel.cpp
        src/panel.h
        src/explorer_dll.h
        src/background.cpp
        src/background.h
        src/files.cpp
        src/files.h
        resources/app.rc # app icon
)

target_compile_definitions(taskbar-ninja PRIVATE
        UNICODE # W version of Win32 calls
        _UNICODE
        WIN32_LEAN_AND_MEAN # exclude rarely-used stuff from Windows headers (smaller windows.h)
        NOMINMAX # prevent windows.h from defining min/max macros (conflicts with std::min/max)
        _WIN32_WINNT=0x0A00 # Windows 10+ API
        TASKBAR_NINJA_VERSION=L"${TASKBAR_NINJA_VERSION}" # version wstring
)

target_link_libraries(taskbar-ninja PRIVATE user32 shell32 dwmapi)

target_compile_options(taskbar-ninja PRIVATE /W4 /WX /permissive- /utf-8)

# ==== EXPLORER DLL ====
# Loaded into explorer.exe, sets the taskbar background brush
add_library(taskbar-ninja-explorer SHARED
        src/explorer_dll.cpp
        src/explorer_dll.h
        src/explorer_dll.def # exports: DllGetClassObject needs PRIVATE, which dllexport cannot give
        src/log.cpp
        src/log.h
        src/files.cpp
        src/files.h
)

target_compile_definitions(taskbar-ninja-explorer PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
)

# windowsapp: c++/WinRT, oleaut32: BSTR, dxguid: Direct2D effect ids
target_link_libraries(taskbar-ninja-explorer PRIVATE user32 oleaut32 windowsapp dxguid)

# bigobj: winrt headers make too many sections for the default obj format
target_compile_options(taskbar-ninja-explorer PRIVATE /W4 /WX /permissive- /utf-8 /bigobj)

# the exe loads the dll from its own folder
add_dependencies(taskbar-ninja taskbar-ninja-explorer)