<h1 align="center">
  <img src="/resources/icon.svg" width="200px"/><br/>
  Taskbar Ninja
</h1>

<p align="center">
  Auto-hide and restyle the Windows 11 taskbar, with separate settings for each monitor.<br/>
  Protect OLED screens from taskbar burn-in, with more control than the Windows auto-hide.
</p>

<p align="center">
  <a href="https://github.com/sean1832/taskbar-ninja/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/sean1832/taskbar-ninja"></a>
  <img alt="Windows 11" src="https://img.shields.io/badge/Windows-11-0078D4">
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/github/license/sean1832/taskbar-ninja"></a>
</p>

<p align="center">
  <a href="https://github.com/sean1832/taskbar-ninja/releases/latest"><b>Download</b></a> |
  <a href="#features">Features</a> |
  <a href="#settings-window">Settings</a> |
  <a href="docs/Config.md">Config</a> |
  <a href="#emergency-restore">Emergency restore</a>
</p>

<p align="center">
  <img alt="Taskbar hiding and showing" src="/docs/imgs/taskbar_demo_01.gif" width="800">
</p>

> Requires Windows 11 22H2 (build 22621) or later. Tested on 24H2 (build 26100).
> Windows 10 may work, but it is not tested or supported.

## Features
- **Per-monitor settings.** Each monitor hides and shows on its own. The app keeps a monitor's settings while it is disconnected.
- **Adjustable timing.** Set the show delay, hide delay, hot zone size, and slide speed.
- **Context-aware visibility.** The taskbar stays visible while Start, a menu, an app preview, or quick settings is open.
- **Full work area.** Windows can fill the area the hidden taskbar leaves.
- **Background styles.** Original, clear, tint, acrylic, or blur. Blur can fade in from the inner edge.
- **Any screen edge.** Works with the taskbar on the top, bottom, left, or right.
- **Live settings window.** Changes apply immediately, with no restart.
- **Crash recovery.** A watchdog restores the taskbar if the app crashes or freezes.

## Installation
1. Download the latest release from [releases](https://github.com/sean1832/taskbar-ninja/releases/latest).
2. Keep `taskbar-ninja.exe` and `taskbar-ninja-explorer.dll` in the same folder.
3. Run `taskbar-ninja.exe`. The app adds an icon to the tray.

## Usage
### Settings window
Right-click the tray icon and choose **Settings**. A change applies when you release the control.

<p align="center">
  <img alt="Settings window" src="/docs/imgs/taskbar_settings_01.png" width="300">
</p>

| Section | Function |
|---|---|
| Monitor | Selects the target of the controls below. **All monitors** is the base for every monitor. Then each connected monitor by model name, the primary first. Monitors that have a section in `config.ini` but are not connected come last. Hidden when you have one monitor and no monitor sections. |
| Hiding, Animation | Hide and show triggers, and slide speed. See [Config](docs/Config.md) for each value. |
| Background | Appearance of every taskbar. The taskbar shows the change while you drag. Only for **All monitors**. |
| Configuration | **Start with Windows** starts the app when you sign in. **Open config file** opens `config.ini`. **Restore Default** asks for confirmation, then moves `config.ini` to `config.ini.bak` and writes the default file. This removes every monitor section too. |

With a monitor picked:
- A grey value comes from **All monitors**. Change it to give that monitor its own value.
- Click **x** next to a value to use the **All monitors** value again.

With **All monitors** picked, `(set on N)` after a control means N monitors keep their own value for it. Hover it to see which ones.

A red line at the top means `config.ini` has a value the app rejected, for example from a manual edit. The app keeps the last valid settings. Set the control again to write a valid value.

### Tray menu
| Item | Action |
|---|---|
| Pause | Shows every taskbar and restores the original background. Click again to resume. |
| Settings | Opens the settings window. |
| Exit | Restores the taskbar and background, then closes the app. |

### Config file
See [Config](docs/Config.md) for `config.ini` and its keys.

### Command line
| Command | Action |
|---|---|
| `taskbar-ninja.exe --restore` | Restores the taskbar and closes a running app. Use it if the taskbar stays hidden. |
| `taskbar-ninja.exe --configs` | Opens `%APPDATA%\taskbar-ninja` in Explorer. |

### The Explorer dll
To change the background, the app loads `taskbar-ninja-explorer.dll` into `explorer.exe`. Explorer keeps the dll loaded until Explorer restarts. Before you update or delete the dll, restart Explorer (`Task Manager > Windows Explorer > Restart`).

### Emergency restore
If the taskbar stays hidden or the app stops responding, try these in order:

1. Press <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>Shift</kbd> + <kbd>T</kbd>. The watchdog closes the app and restores the taskbar and background. This works while the app runs, even when it is frozen.
2. Press <kbd>Win</kbd> + <kbd>R</kbd> and run `"C:\path\to\taskbar-ninja.exe" --restore`. Use this if the hotkey does nothing, for example after the app closed.
3. Restart Explorer: press <kbd>Ctrl</kbd> + <kbd>Shift</kbd> + <kbd>Esc</kbd>, select **Windows Explorer**, then **Restart**. Explorer makes a new taskbar with the original look.

## Building from source
<details>
<summary>Build steps</summary>

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

</details>

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
