# Dear ImGui

UI library for the settings window. Copied unchanged from upstream.

- Source: https://github.com/ocornut/imgui
- Tag: `v1.93.0`
- Commit: `c1aaac65e896a45f81f47a4d68f9959e0f86c859`

Files kept:

- Core: `imgui.cpp`, `imgui.h`, `imgui_internal.h`, `imconfig.h`, `imgui_draw.cpp`, `imgui_widgets.cpp`, `imgui_tables.cpp`, `imgui_demo.cpp`, `imstb_*.h`, `LICENSE.txt`
- Backends: `backends/imgui_impl_win32.*` (window and input), `backends/imgui_impl_dx11.*` (Direct3D 11 drawing)

`imgui_demo.cpp` is kept for the style editor during development. Release builds define `IMGUI_DISABLE_DEMO_WINDOWS`.

Update: copy the same files from a new tag, then change the tag and commit above. Do not edit the files here. Settings go in compile definitions in `CMakeLists.txt`.
