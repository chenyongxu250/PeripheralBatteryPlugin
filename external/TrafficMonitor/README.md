# TrafficMonitor plugin interface

`PluginInterface.h` is taken unchanged from the official repository:

- Source: https://github.com/zhongyang219/TrafficMonitor/blob/V1.86/include/PluginInterface.h
- Tag: `V1.86` (commit `02a817a069bac6bf4d263b5209d9c1b07fe2f950`)
- Plugin API version: **7**
- Git blob: `f11f283302938d26bab668a3afdd07b1917814e3` (identical to the upstream blob)
- License: "Anti 996" License 1.0, see `LICENSE` in this folder

## Why V1.86 and not `master`

Tested with TrafficMonitor 1.8.6.0 (x64). `master` already contains API version 8
(`IPluginItem::DrawItemEx`, `IPluginItem::IsDoubleLineExclusive`, `IPluginDrawer`,
`ITrafficMonitor::GetMainWindowHwnd/GetTaskbarWindowHwnd`), which is not part of any release.
The plugin does not need these functions. A plugin built against API 7 also runs under future
hosts, because the host only calls newer functions after checking `GetAPIVersion()`.
