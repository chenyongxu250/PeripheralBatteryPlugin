// BatteryProbe – diagnostics for the battery sources, independent of TrafficMonitor.
// Uses the same queries as the plugin (src/bluetooth, src/hidpp), so it tests exactly that code.

#include <windows.h>
#include <objbase.h>
#include <tlhelp32.h>

#include <fcntl.h>
#include <io.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "bluetooth/BluetoothScanner.h"
#include "hidpp/LogitechScanner.h"

namespace
{
    constexpr wchar_t kVersion[] = L"0.4";

    struct Options
    {
        bool bluetooth = false;
        bool logitech = false;
        bool json = false;
        bool verbose = false;
        bool diag = false;
        bool includeSensitive = false;
    };

    std::wstring RegistryString(const wchar_t* key, const wchar_t* value)
    {
        wchar_t buffer[256] = {};
        DWORD size = sizeof(buffer);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS)
            return {};
        return buffer;
    }

    DWORD RegistryDword(const wchar_t* key, const wchar_t* value)
    {
        DWORD data = 0, size = sizeof(data);
        RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_DWORD, nullptr, &data, &size);
        return data;
    }

    // Product version from a file's VERSIONINFO, empty if there is none.
    std::wstring FileVersion(const std::wstring& path)
    {
        DWORD handle = 0;
        const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
        if (size == 0)
            return {};
        std::vector<BYTE> data(size);
        if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
            return {};
        VS_FIXEDFILEINFO* info = nullptr;
        UINT length = 0;
        if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || info == nullptr)
            return {};
        wchar_t text[64];
        swprintf_s(text, L"%u.%u.%u.%u", HIWORD(info->dwProductVersionMS), LOWORD(info->dwProductVersionMS),
            HIWORD(info->dwProductVersionLS), LOWORD(info->dwProductVersionLS));
        return text;
    }

    // Path of the running TrafficMonitor.exe (also for an elevated process, via "limited information").
    std::wstring RunningTrafficMonitor(DWORD& pid)
    {
        pid = 0;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return {};
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        std::wstring path;
        for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
        {
            if (_wcsicmp(entry.szExeFile, L"TrafficMonitor.exe") != 0)
                continue;
            pid = entry.th32ProcessID;
            if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
            {
                wchar_t buffer[MAX_PATH] = {};
                DWORD length = MAX_PATH;
                if (QueryFullProcessImageNameW(process, 0, buffer, &length))
                    path.assign(buffer, length);
                CloseHandle(process);
            }
            break;
        }
        CloseHandle(snapshot);
        return path;
    }

    std::wstring ReadUtf8Tail(const std::wstring& path, size_t lines)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return {};
        std::string data((std::istreambuf_iterator<char>(in)), {});
        const int size = MultiByteToWideChar(CP_UTF8, 0, data.data(), static_cast<int>(data.size()), nullptr, 0);
        std::wstring text(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, data.data(), static_cast<int>(data.size()), text.data(), size);
        size_t pos = text.size(), found = 0;
        while (pos > 0 && found <= lines)
        {
            pos = text.find_last_of(L'\n', pos - 1);
            if (pos == std::wstring::npos) { pos = 0; break; }
            ++found;
        }
        return text.substr(pos == 0 ? 0 : pos + 1);
    }

    void PrintDiagnostics()
    {
        const wchar_t* ntKey = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
        std::wstring product = RegistryString(ntKey, L"ProductName");
        const std::wstring build = RegistryString(ntKey, L"CurrentBuild");
        // ProductName reads "Windows 10 …" even on Windows 11; Windows 11 starts with build 22000.
        if (_wtoi(build.c_str()) >= 22000 && product.rfind(L"Windows 10", 0) == 0)
            product.replace(0, 10, L"Windows 11");
        wprintf(L"[System]\nWindows:   %s %s (Build %s.%lu)\n", product.c_str(),
            RegistryString(ntKey, L"DisplayVersion").c_str(), build.c_str(), RegistryDword(ntKey, L"UBR"));

        DWORD pid = 0;
        std::wstring exe = RunningTrafficMonitor(pid);
        const bool running = pid != 0;
        if (exe.empty())
            exe = L"C:\\Program Files\\TrafficMonitor\\TrafficMonitor.exe";
        const std::wstring dir = exe.substr(0, exe.find_last_of(L'\\') + 1);
        const std::wstring plugins = dir + L"plugins\\";

        wprintf(L"\n[TrafficMonitor]\n");
        wprintf(L"Running:   %s\n", running ? (L"yes (PID " + std::to_wstring(pid) + L")").c_str() : L"no");
        wprintf(L"Path:      %s%s\n", exe.c_str(), GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES ? L" (not found)" : L"");
        wprintf(L"Version:   %s\n", FileVersion(exe).c_str());
        const std::wstring dll = plugins + L"PeripheralBatteryPlugin.dll";
        const std::wstring dllVersion = FileVersion(dll);
        wprintf(L"Plugin:    %s\n", dllVersion.empty() ? L"not found" : (dllVersion + L" (" + dll + L")").c_str());

        const std::wstring ini = plugins + L"PeripheralBattery.ini";
        const bool iniExists = GetFileAttributesW(ini.c_str()) != INVALID_FILE_ATTRIBUTES;
        wprintf(L"\n[PeripheralBattery.ini]%s\n", iniExists ? L"" : L" not present");
        if (iniExists)
        {
            // UTF-16 LE: read via GetPrivateProfileSection, which detects the encoding itself.
            for (const wchar_t* section : { L"General", L"Headphones", L"Mouse" })
            {
                wchar_t buffer[2048] = {};
                const DWORD n = GetPrivateProfileSectionW(section, buffer, static_cast<DWORD>(std::size(buffer)), ini.c_str());
                wprintf(L"[%s]\n", section);
                for (const wchar_t* p = buffer; p < buffer + n && *p; p += wcslen(p) + 1)
                    wprintf(L"  %s\n", p);
            }
        }

        const std::wstring log = plugins + L"PeripheralBatteryPlugin.log";
        const std::wstring tail = ReadUtf8Tail(log, 15);
        wprintf(L"\n[PeripheralBatteryPlugin.log – last lines]%s\n%s", tail.empty() ? L" not present" : L"",
            tail.c_str());
        wprintf(L"\n");
    }

    std::wstring JsonEscape(std::wstring_view text)
    {
        std::wstring out;
        for (wchar_t c : text)
        {
            switch (c)
            {
            case L'"': out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:
                if (c < 0x20)
                {
                    wchar_t buffer[8];
                    swprintf_s(buffer, L"\\u%04x", static_cast<unsigned>(c));
                    out += buffer;
                }
                else
                {
                    out.push_back(c);
                }
            }
        }
        return out;
    }

    std::wstring Hex4(unsigned value)
    {
        wchar_t buffer[8];
        swprintf_s(buffer, L"%04X", value);
        return buffer;
    }

    std::wstring SlotText(uint8_t index)
    {
        return index == 0xFF ? L"Cable (0xFF)" : L"Slot " + std::to_wstring(index);
    }

    void PrintHex(bool tx, const uint8_t* data, size_t length)
    {
        wprintf(L"    %s", tx ? L"TX" : L"RX");
        for (size_t i = 0; i < length; ++i)
            wprintf(L" %02X", data[i]);
        wprintf(L"\n");
    }

    void PrintBluetoothText(const BluetoothScanResult& scan, const Options& options)
    {
        if (!scan.error.empty())
        {
            wprintf(L"[Bluetooth] Error: %s\n", scan.error.c_str());
            return;
        }

        wprintf(L"[Bluetooth] %zu paired devices\n", scan.devices.size());
        for (const auto& r : scan.devices)
        {
            wprintf(L"\nName:      %s\n", r.name.c_str());
            wprintf(L"Transport: %s\n", r.transport.c_str());
            wprintf(L"Connected: %s\n", !r.connected ? L"unknown" : (*r.connected ? L"yes" : L"no"));
            if (options.includeSensitive)
                wprintf(L"Address:   %s\n", r.address.c_str());
            if (r.battery)
                wprintf(L"Battery:   %d%%  (node: %s)%s\n", *r.battery, r.batterySource.c_str(),
                    r.connected.value_or(false) ? L"" : L"  [disconnected: last known value]");
            else
                wprintf(L"Battery:   -\n");
            if (!r.error.empty())
                wprintf(L"Error:     %s\n", r.error.c_str());

            if (options.verbose)
            {
                wprintf(L"Container: %s\n", r.containerId.c_str());
                for (const auto& n : r.nodes)
                {
                    std::wstring battery = n.battery ? std::to_wstring(*n.battery) + L"%"
                        : (n.batteryType.empty() ? L"-" : L"(" + n.batteryType + L")");
                    wprintf(L"  Node:    %-45s Battery property: %s\n", n.name.c_str(), battery.c_str());
                    if (options.includeSensitive)
                        wprintf(L"           %s\n", n.id.c_str());
                }
            }
        }
    }

    void PrintLogitechText(const LogitechScanResult& scan)
    {
        wprintf(L"[Logitech] %zu HID++ channels, %zu devices\n", scan.channels, scan.devices.size());
        for (const auto& note : scan.notes)
            wprintf(L"Note:      %s\n", note.c_str());
        for (const auto& r : scan.devices)
        {
            wprintf(L"\nName:      %s\n", r.name.empty() ? L"(unknown)" : r.name.c_str());
            wprintf(L"Channel:   %s (PID %s), %s\n", r.product.c_str(), Hex4(r.productId).c_str(),
                SlotText(r.deviceIndex).c_str());
            wprintf(L"Protocol:  HID++ %d.%d\n", r.protocolMajor, r.protocolMinor);
            if (r.battery && r.battery->millivolts)
                wprintf(L"Battery:   %d%%  (estimated from %d mV, Feature 0x%s, Flags 0x%02X)\n",
                    r.battery->percentage.value_or(-1), *r.battery->millivolts,
                    Hex4(r.battery->feature).c_str(), r.battery->rawStatus);
            else if (r.battery && r.battery->percentage)
                wprintf(L"Battery:   %d%%  (Feature 0x%s, Status 0x%02X)\n", *r.battery->percentage,
                    Hex4(r.battery->feature).c_str(), r.battery->rawStatus);
            else
                wprintf(L"Battery:   -\n");
            if (r.battery && r.battery->charge != hidpp::ChargeState::Unknown)
            {
                const wchar_t* charge = r.battery->charge == hidpp::ChargeState::Charging ? L"charging"
                    : r.battery->charge == hidpp::ChargeState::Full ? L"full" : L"discharging";
                wprintf(L"Charge:    %s\n", charge);
            }
            if (!r.error.empty())
                wprintf(L"Error:     %s\n", r.error.c_str());
        }
    }

    void PrintJson(const BluetoothScanResult& bt, const LogitechScanResult& lg, const Options& options)
    {
        wprintf(L"{\n");
        if (!bt.error.empty())
            wprintf(L"  \"bluetoothError\": \"%s\",\n", JsonEscape(bt.error).c_str());
        wprintf(L"  \"devices\": [");
        bool first = true;
        auto open = [&] { wprintf(L"%s\n    {\n", first ? L"" : L","); first = false; };

        for (const auto& r : bt.devices)
        {
            open();
            wprintf(L"      \"name\": \"%s\",\n", JsonEscape(r.name).c_str());
            wprintf(L"      \"type\": \"bluetooth-%s\",\n", r.transport.c_str());
            if (r.connected)
                wprintf(L"      \"connected\": %s,\n", *r.connected ? L"true" : L"false");
            else
                wprintf(L"      \"connected\": null,\n");
            if (options.includeSensitive)
                wprintf(L"      \"address\": \"%s\",\n", JsonEscape(r.address).c_str());
            if (!r.error.empty())
                wprintf(L"      \"error\": \"%s\",\n", JsonEscape(r.error).c_str());
            if (r.battery)
                wprintf(L"      \"battery\": %d\n", *r.battery);
            else
                wprintf(L"      \"battery\": null\n");
            wprintf(L"    }");
        }

        for (const auto& r : lg.devices)
        {
            open();
            wprintf(L"      \"name\": \"%s\",\n", JsonEscape(r.name).c_str());
            wprintf(L"      \"type\": \"logitech-hidpp\",\n");
            wprintf(L"      \"productId\": \"%s\",\n", Hex4(r.productId).c_str());
            wprintf(L"      \"deviceIndex\": %d,\n", r.deviceIndex);
            wprintf(L"      \"protocol\": \"%d.%d\",\n", r.protocolMajor, r.protocolMinor);
            if (!r.error.empty())
                wprintf(L"      \"error\": \"%s\",\n", JsonEscape(r.error).c_str());
            if (r.battery)
            {
                wprintf(L"      \"batteryFeature\": \"0x%s\",\n", Hex4(r.battery->feature).c_str());
                wprintf(L"      \"batteryStatus\": %d,\n", r.battery->rawStatus);
                if (r.battery->millivolts)
                    wprintf(L"      \"millivolts\": %d,\n", *r.battery->millivolts);
            }
            if (r.battery && r.battery->percentage)
                wprintf(L"      \"battery\": %d\n", *r.battery->percentage);
            else
                wprintf(L"      \"battery\": null\n");
            wprintf(L"    }");
        }
        wprintf(L"\n  ]\n}\n");
    }

    void PrintUsage()
    {
        wprintf(L"Usage: BatteryProbe.exe [--all | --bluetooth | --logitech] [--json] [--verbose] [--diag] [--include-sensitive]\n"
                L"  --diag     system, TrafficMonitor, plugin version, INI, last log lines and all devices\n"
                L"  --verbose  also device nodes and HID++ trace\n");
    }
}

int wmain(int argc, wchar_t** argv)
{
    // UTF-8 in files/pipes, Unicode in the console.
    (void)_setmode(_fileno(stdout), _O_U8TEXT);
    (void)_setmode(_fileno(stderr), _O_U8TEXT);

    Options options;
    for (int i = 1; i < argc; ++i)
    {
        std::wstring_view arg = argv[i];
        if (arg == L"--all")
            options.bluetooth = options.logitech = true;
        else if (arg == L"--bluetooth")
            options.bluetooth = true;
        else if (arg == L"--logitech")
            options.logitech = true;
        else if (arg == L"--json")
            options.json = true;
        else if (arg == L"--verbose")
            options.verbose = true;
        else if (arg == L"--diag")
            options.diag = true;
        else if (arg == L"--include-sensitive")
            options.includeSensitive = true;
        else if (arg == L"--help" || arg == L"-h")
        {
            PrintUsage();
            return 0;
        }
        else
        {
            fwprintf(stderr, L"Unknown parameter: %s\n", argv[i]);
            PrintUsage();
            return 2;
        }
    }
    if (!options.bluetooth && !options.logitech)
        options.bluetooth = options.logitech = true;
    if (options.diag)
        options.json = false;   // diagnostics are a text report

    // Like the worker in the plugin: MTA.
    (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    if (!options.json)
    {
        wprintf(L"Peripheral Battery Probe %s\n\n", kVersion);
        if (options.diag)
            PrintDiagnostics();
    }

    BluetoothScanResult bt;
    if (options.bluetooth)
        bt = ScanBluetoothDevices();

    LogitechScanResult lg;
    if (options.logitech)
    {
        HidppTrace trace;
        if (options.verbose && !options.json)
            trace = PrintHex;
        lg = LogitechScanner(false).Scan(trace);   // no cache: every run reads everything fresh
    }

    if (options.json)
    {
        PrintJson(bt, lg, options);
    }
    else
    {
        if (options.bluetooth)
        {
            Options btOptions = options;
            btOptions.verbose = options.verbose || options.diag;    // device nodes in diagnostics too
            PrintBluetoothText(bt, btOptions);
        }
        if (options.bluetooth && options.logitech)
            wprintf(L"\n");
        if (options.logitech)
            PrintLogitechText(lg);
    }
    return bt.error.empty() ? 0 : 1;
}
