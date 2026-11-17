cmake_minimum_required(VERSION 3.20)
project(taskbar_ninja LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20) # C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# static runtime: exe needs no runtime dlls
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# WIN32 = no console window
add_executable(taskbar-ninja WIN32
        src/main.cpp
        src/exit_code.h
)

target_compile_definitions(taskbar-ninja PRIVATE
        UNICODE # W version of Win32 calls
        _UNICODE
        WIN32_LEAN_AND_MEAN # exclude rarely-used stuff from Windows headers (smaller windows.h)
        NOMINMAX # prevent windows.h from defining min/max macros (conflicts with std::min/max)
        _WIN32_WINNT=0x0A00 # Windows 10+ API
)

target_link_libraries(taskbar-ninja PRIVATE user32 shell32)

if (MSVC)
    target_compile_options(taskbar-ninja PRIVATE /W4 /WX /permissive- /utf-8)
else()
    # -municode: lets MinGW find wWinMain
    target_compile_options(taskbar-ninja PRIVATE -Wall -Wextra -Werror -municode)
    target_link_options(taskbar-ninja PRIVATE -municode -static)
endif()