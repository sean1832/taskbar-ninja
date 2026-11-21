<h1 align="center">
  <img src="/resources/icon.svg" width="200px"/><br/>
  Taskbar Ninja
</h1>
<p align="center">
    Auto-hide and restyle the Windows 11 taskbar, with separate settings for each monitor.<br/>
    Protect OLED screens from taskbar burn-in, with more control than the Windows auto-hide.
</p>

> Requires Windows 11 22H2 (build 22621) or later. Tested on 24H2 (build 26100). 
> Windows 10 may work but not tested or supported.

## Features
- Auto-hide per monitor. The taskbar shows when the mouse touches the screen edge, and hides after a delay.
- The taskbar stays shown while Start, a menu, an app preview, or quick settings is open.
- Windows can fill the strip the hidden taskbar leaves (`cover_taskbar_area`).
- Background styles: `original`, `clear`, `tint`, `acrylic`, `blur`. Blur can fade in from the inner edge.
- Works with the taskbar on any screen edge.
- Applies config changes when you save the file. No restart.
- A watchdog process restores the taskbar if the app crashes or freezes.

## Installation
1. Download the latest release from [releases](https://github.com/sean1832/taskbar-ninja/releases/latest).
2. Keep `taskbar-ninja.exe` and `taskbar-ninja-explorer.dll` in the same folder.
3. Run `taskbar-ninja.exe`. The app adds an icon to the tray.

## Usage
### Tray menu
| Item | Action |
|---|---|
| Pause | Shows every taskbar and restores the original background. Click again to resume. |
| Config | Opens `config.ini`. |
| Start With Windows | Starts the app when you sign in. Click again to stop. |
| Exit | Restores the taskbar and background, then closes the app. |

### Config
The app writes `%APPDATA%\taskbar-ninja\config.ini` on first start.

```ini
[default]
auto_hide = true
cover_taskbar_area = true
show_delay_ms = 0
hide_delay_ms = 300
hot_zone_pixels = 2
show_animation_ms = 300
hide_animation_ms = 300
background = blur
background_color = 40202020
background_blur_pixels = 10
background_feather = true
background_feather_percent = 50
```

| Key | Values | Meaning |
|---|---|---|
| `auto_hide` | `true`, `false` | Hide the taskbar until the mouse touches the hot zone. |
| `cover_taskbar_area` | `true`, `false` | Let windows fill the taskbar strip. Needs `auto_hide = true`. |
| `show_delay_ms` | 0 to 10000 | Time in the hot zone before the taskbar shows. |
| `hide_delay_ms` | 0 to 10000 | Time away from the taskbar before it hides. |
| `hot_zone_pixels` | 1 to 50 | Thickness of the strip at the screen edge that shows the taskbar. |
| `show_animation_ms` | 0 to 2000 | Time the taskbar takes to slide in. 0 = shows at once. |
| `hide_animation_ms` | 0 to 2000 | Time the taskbar takes to slide out. Starts after `hide_delay_ms`. 0 = hides at once. |
| `background` | `original`, `clear`, `tint`, `acrylic`, `blur` | Background of every taskbar. |
| `background_color` | `AARRGGBB` hex | Colour for `tint`, `acrylic` and `blur`. `AA` is the opacity. |
| `background_blur_pixels` | 1 to 50 | Blur strength. |
| `background_feather` | `true`, `false` | Fade the blur in from the inner edge. |
| `background_feather_percent` | 1 to 100 | Part of the bar thickness the fade covers. |

To give one monitor its own settings, add a section named after its model, for example `[DELL U2720Q]`. Keys you leave out come from `[default]`. You can find the model names on the `monitor` lines in `%APPDATA%\taskbar-ninja\log.txt`.

The `background_*` keys apply to every taskbar and only work in `[default]`.

A missing `show_animation_ms` or `hide_animation_ms` means 0, so a config from an older version keeps its instant show and hide.

`acrylic` and `blur` need **Settings > Personalization > Colors > Transparency effects** turned on.

If a value is out of range, the app keeps the last good settings and writes the reason to `log.txt`.

### Command line
| Command | Action |
|---|---|
| `taskbar-ninja.exe --restore` | Restores the taskbar and closes a running app. Use it if the taskbar stays hidden. |
| `taskbar-ninja.exe --configs` | Opens `%APPDATA%\taskbar-ninja` in Explorer. |

### The Explorer dll
To change the background, the app loads `taskbar-ninja-explorer.dll` into `explorer.exe`. Explorer keeps the dll loaded until Explorer restarts. Before you update or delete the dll, restart Explorer (`Task Manager > Windows Explorer > Restart`).

### Emergency restore
If the taskbar stays hidden or the app stops responding, try these in order:

1. Press `Ctrl + Alt + Shift + T`. The watchdog closes the app and restores the taskbar and background. This works while the app runs, even when it is frozen.
2. Press `Win + R` and run `"C:\path\to\taskbar-ninja.exe" --restore`. Use this if the hotkey does nothing, for example after the app closed.
3. Restart Explorer: press `Ctrl + Shift + Esc`, select **Windows Explorer**, then **Restart**. Explorer makes a new taskbar with the original look.

## Building from source
### Prerequisites
- Windows 11 22H2 or later
- CMake 3.20 or later
- Visual Studio 2022 or later, with the "Desktop development with C++" workload

### Compile
```bash
git clone https://github.com/sean1832/taskbar-ninja.git
cd taskbar-ninja
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The build writes `taskbar-ninja.exe` and `taskbar-ninja-explorer.dll` to `build/Release`.

The tray tooltip shows the version. It is `dev` unless you set it when you configure:

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 -DTASKBAR_NINJA_VERSION=1.2.0
```

CMake keeps this value in `build`. To change it, run the command again with a new value, or delete `build`.

If the link step fails because `taskbar-ninja-explorer.dll` is in use, restart Explorer and build again.

## Acknowledgements
- [Per-Monitor Taskbar](https://github.com/nikoleinonen/per-monitor-taskbar) by Leiksa, MIT License. Hide and show of each taskbar, the work area, and crash recovery.
- [TranslucentTB](https://github.com/TranslucentTB/TranslucentTB), GPL-3.0. Loading the dll into Explorer, the taskbar XAML element names, the blur brush, and Start and search detection.

## License
```
Taskbar Ninja - auto-hide and restyle the Windows 11 taskbar
Copyright (C) 2026 Zeke Zhang

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published
by the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.
```

See [LICENSE](LICENSE) for the full text.
