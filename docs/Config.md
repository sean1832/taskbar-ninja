# Config

The app writes `%APPDATA%\taskbar-ninja\config.ini` on first start. The settings window edits this file for you. You can also edit it manually. A manual edit applies when you open the settings window or click back on it, and when the app starts.

```ini
[default]
auto_hide = true
cover_taskbar_area = true
show_delay_ms = 0
hide_delay_ms = 300
hot_zone_pixels = 2
show_animation_ms = 300
hide_animation_ms = 300
background = original
background_color = 40202020
background_blur_pixels = 10
background_feather = true
background_feather_percent = 50
```

| Key | Values | Description |
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

To give one monitor its own settings manually, add a section named after its model, for example `[DELL U2720Q]`. The settings window makes these sections for you. Keys you leave out come from `[default]`. You can find the model names on the `monitor` lines in `%APPDATA%\taskbar-ninja\log.txt`.

The `background_*` keys apply to every taskbar and only work in `[default]`.

A missing `show_animation_ms` or `hide_animation_ms` means 0, so a config from an older version keeps its instant show and hide.

`acrylic` and `blur` need **Settings > Personalization > Colors > Transparency effects** turned on.

If a value is out of range, the app keeps the last valid settings and writes the reason to `log.txt`. The settings window shows the reason in red.