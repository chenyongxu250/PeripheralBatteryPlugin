# PeripheralBatteryPlugin

[![Build](https://github.com/chenyongxu250/PeripheralBatteryPlugin/actions/workflows/build.yml/badge.svg)](https://github.com/chenyongxu250/PeripheralBatteryPlugin/actions/workflows/build.yml)
[![CodeQL](https://github.com/chenyongxu250/PeripheralBatteryPlugin/actions/workflows/codeql.yml/badge.svg)](https://github.com/chenyongxu250/PeripheralBatteryPlugin/actions/workflows/codeql.yml)

Plugin for [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) (Windows, x64) that shows the battery
level of peripheral devices as separate items in the taskbar:

| Item | Source | Tested with |
|---|---|---|
| **Headphones Battery** | Bluetooth battery level as known to Windows – no vendor app needed | Sennheiser MOMENTUM 4 |
| **Mouse Battery** | Logitech mouse queried directly via HID++ through the LIGHTSPEED / Unifying / Bolt receiver – no G Hub needed | Logitech G502 LIGHTSPEED |

```
M4 100%   Mouse 92%
```

## Display

| Value | Meaning |
|---|---|
| `82%` | battery level |
| `⚡` | charging (mice that only report their voltage show no percentage while charging) |
| `⚡100%` | full and on the cable |
| `—` | not connected, switched off or asleep |
| `?` | no value yet, or the device does not respond |

This build uses TrafficMonitor's native text rendering: values are not colored by the plugin. Set item labels and
text colors in TrafficMonitor's **Display Settings**. Hovering over the display shows a tooltip with the full device
name, the value and the time of the reading.

For mice that only report their voltage (e.g. G502 LIGHTSPEED) the percentage is an estimate. Windows reports the
MOMENTUM 4's battery level in 10% steps.

## Installation

Requirements: Windows 10/11 (64-bit), TrafficMonitor 1.82 or later, 64-bit version (tested with 1.86).

1. Open the [Build workflow](https://github.com/chenyongxu250/PeripheralBatteryPlugin/actions/workflows/build.yml) in this repository, open a successful run for the commit you want to test, and download its `PeripheralBatteryPlugin-<commit SHA>` artifact. Do not use the upstream Releases archive to test changes from this fork.
2. Exit TrafficMonitor (right-click → Exit).
3. Copy `PeripheralBatteryPlugin.dll` **directly** into the `plugins` folder next to `TrafficMonitor.exe`,
   e.g. `C:\Program Files\TrafficMonitor\plugins\` (no subfolder – TrafficMonitor only loads DLLs directly inside
   `plugins`; under `C:\Program Files` this needs administrator rights).
4. Start TrafficMonitor.
5. Right-click the TrafficMonitor display → **Display Settings** → tick "Headphones Battery" and "Mouse Battery"
   (at the very bottom of the list).

The plugin manager (right-click → "Plugin-in Manage…") shows whether the plugin was loaded.

## Settings

Plugin manager → "Peripheral Battery" → **Options…** (or right-click one of the items → **Plugin Options**).
Changes take effect immediately.

The settings are stored in `PeripheralBattery.ini` in the plugin folder. The plugin creates the file on first
start; it explains every setting and can also be edited directly:

| Section | Key | Default | Meaning |
|---|---|---|---|
| `[General]` | `UpdateInterval` | `60` | poll interval in seconds (15–3600) |
| | `ShowDisconnected` | `1` | `—` for a disconnected device (`0` = empty) |
| | `ShowPercentSign` | `1` | `82%` (`0` = `82`) |
| | `ShowCharging` | `1` | `⚡` while charging |
| | `LowBatteryWarning` | `15` | notification at or below this level (`0` = off) |
| | `VerboseLogging` | `0` | `1` = write every poll cycle to the log |
| `[Headphones]`, `[Mouse]` | `Enabled` | `1` | query the device |
| | `Provider` | `WindowsBluetooth` / `LogitechHidpp` | data source |
| | `NameContains` | `MOMENTUM 4` / empty | part of the device name. Empty: the first Logitech device with a battery, or the first connected Bluetooth device with a battery level |
Legacy `Colors` and `Label` entries in an existing INI are not used by this build; the obsolete `Colors` key is
deleted the next time settings are saved. Configure labels and text colors in TrafficMonitor's **Display Settings**.

For other Bluetooth headphones, enter part of their name under "Device" / `NameContains` – provided Windows
knows their battery level (Settings → Bluetooth & devices).

## Behavior

- Queries run in the background, every 60 seconds by default; TrafficMonitor is never blocked.
- Instant updates: mouse on/off, charging cable, receiver plugged/unplugged and Bluetooth connection changes
  show up after about 1–3 seconds.
- Short dropouts keep showing the last value; `—` appears only after three failures spanning at least two
  intervals. If Windows or the Logitech receiver reports a device as disconnected, that applies immediately –
  also when a mouse falls asleep after a few minutes of inactivity.
- When a battery drops to the warning threshold, TrafficMonitor shows a notification once; again only after the
  battery has risen 5 percentage points above it or has been charged.
- Log: `PeripheralBatteryPlugin.log` next to the INI (start, errors and state changes).

## Troubleshooting

`BatteryProbe.exe` (included in the release) shows what the plugin sees, without TrafficMonitor:

```
BatteryProbe.exe --diag                 report: system, TrafficMonitor, plugin version, INI, log, devices
BatteryProbe.exe                        all devices
BatteryProbe.exe --logitech --verbose   with HID++ trace
BatteryProbe.exe --json                 machine-readable
```

Addresses and device IDs are only shown with `--include-sensitive`.

## Building

Visual Studio 2022 or the Build Tools 2022 with the C++ workload (v143) and a Windows 10/11 SDK:

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" `
    PeripheralBatteryPlugin.sln /p:Configuration=Release /p:Platform=x64 /m
```

Output: `bin\x64\Release\PeripheralBatteryPlugin.dll` and `bin\x64\Release\tools\BatteryProbe.exe`.

## Contributing

Issues and pull requests are welcome – bug reports, fixes, support for more devices, translations.

- **Bug reports:** please describe what the taskbar shows and attach the output of `BatteryProbe.exe --diag`.
  It contains no addresses or device IDs (those only appear with `--include-sensitive`).
- **Pull requests:** every pull request is built automatically (Debug and Release, warnings are errors) and
  analyzed with CodeQL.
- **New devices:** please don't guess protocol commands or Windows properties – new HID++ features or device
  properties should come with a source (documentation or a capture of the device's traffic).

## License and sources

MIT, see [LICENSE](LICENSE).

- `external/TrafficMonitor/PluginInterface.h` is taken unchanged from
  [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) (V1.86) and is covered by its license
  (`external/TrafficMonitor/LICENSE`).
- HID++ protocol details based on Logitech's [cpg-docs](https://github.com/Logitech/cpg-docs) and
  [logitray](https://github.com/Ithilias/logitray) (MIT); the data points of the voltage curve come from
  [Solaar](https://github.com/pwr-Solaar/Solaar).
- The battery level of Bluetooth devices comes from an undocumented device property set by Windows (also used
  by e.g. [WmiPnp](https://github.com/nikvoronin/WmiPnp)).
