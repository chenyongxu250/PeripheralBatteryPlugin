#include "items/BatteryItem.h"

#include <algorithm>
#include <ctime>

#include "battery/BatteryManager.h"

namespace
{
    // Widest value FormatValue can return; determines the reserved width.
    const wchar_t* SampleText(const DisplayOptions& options)
    {
        return options.showCharging ? L"⚡100%" : L"100%";
    }

    // Normalize punctuation from IMEs (full-width colon) and trim edge spaces, so the displayed
    // label is consistently "MB:" rather than "MB：" or a label with an accidental trailing blank.
    std::wstring NormalizeLabel(std::wstring label)
    {
        for (wchar_t& ch : label)
        {
            if (ch == L'\uFF1A')
                ch = L':';
        }
        const auto notSpace = [](wchar_t ch) { return ch != L' ' && ch != L'\t'; };
        label.erase(label.begin(), std::find_if(label.begin(), label.end(), notSpace));
        label.erase(std::find_if(label.rbegin(), label.rend(), notSpace).base(), label.end());
        return label;
    }

    // Compact but visible gap between the label and value.
    int GapWidth(HDC dc)
    {
        SIZE size{};
        if (!GetTextExtentPoint32W(dc, L" ", 1, &size))
            return 1;
        const int spaceWidth = static_cast<int>(size.cx);
        return spaceWidth > 1 ? (spaceWidth + 1) / 2 : 1;
    }
}

BatteryItem::BatteryItem(const BatteryManager& manager, const DrawContext& drawContext)
    : manager_(manager)
    , drawContext_(drawContext)
{
}

const wchar_t* BatteryItem::GetItemValueText() const
{
    try
    {
        valueText_ = FormatValue(ReadStatus(), manager_.GetDisplayOptions());
        return valueText_.c_str();
    }
    catch (...)
    {
        return L"?";
    }
}

const wchar_t* BatteryItem::GetItemValueSampleText() const
{
    try
    {
        return SampleText(manager_.GetDisplayOptions());
    }
    catch (...)
    {
        return L"⚡100%";
    }
}

bool BatteryItem::IsCustomDraw() const
{
    // This build always lets TrafficMonitor draw the item using its own label and text color.
    return false;
}

int BatteryItem::GetItemWidth() const
{
    // Fallback only (at 96 DPI) in case GetItemWidthEx fails; otherwise the host uses GetItemWidthEx.
    return 80;
}

int BatteryItem::GetItemWidthEx(void* hDC) const
{
    // UI thread, while the taskbar window is being built; the host has selected its font.
    try
    {
        const HDC dc = static_cast<HDC>(hDC);
        if (dc == nullptr)
            return 0;
        const DisplayOptions options = manager_.GetDisplayOptions();
        // TrafficMonitor renders the label itself; reserve width only for the battery value.
        return MeasureWidth(dc, L"", SampleText(options));
    }
    catch (...)
    {
        return 0;
    }
}

void BatteryItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    // UI thread, on every repaint: read the cache only, let nothing escape into TrafficMonitor.
    try
    {
        const HDC dc = static_cast<HDC>(hDC);
        if (dc == nullptr || w <= 0 || h <= 0)
            return;
        const DisplayOptions options = manager_.GetDisplayOptions();
        const BatteryStatus status = ReadStatus();
        const COLORREF normal = drawContext_.hasValueColor ? drawContext_.valueColor : GetTextColor(dc);
        // The main window has only one text color (EI_LABEL_TEXT_COLOR is not sent there).
        const COLORREF labelColor = drawContext_.taskbar && drawContext_.hasLabelColor ? drawContext_.labelColor : normal;
        const COLORREF valueColor = ToneColor(ToneFor(status, manager_.GetLowBatteryWarning()), normal, dark_mode);
        const RECT rect{ x, y, x + w, y + h };
        DrawLabelValue(dc, rect, NormalizeLabel(ReadLabel(options)), labelColor, FormatValue(status, options), valueColor,
            drawContext_.taskbar && drawContext_.valueRightAlign);
    }
    catch (...)
    {
    }
}

BatteryItem::Tone BatteryItem::ToneFor(const BatteryStatus& status, int lowThreshold)
{
    if (!status.available)
        return Tone::Normal;
    if (status.state == BatteryState::Charging || status.state == BatteryState::Full)
        return Tone::Charging;
    // Like the battery warning: only measured values while discharging.
    if (status.state == BatteryState::Discharging && lowThreshold > 0 && status.percentage >= 0
        && status.percentage <= lowThreshold)
    {
        return Tone::Low;
    }
    return Tone::Normal;
}

COLORREF BatteryItem::ToneColor(Tone tone, COLORREF normal, bool darkMode)
{
    // Light on a dark background, strong on a light one, so both stay readable.
    switch (tone)
    {
    case Tone::Low:
        return darkMode ? RGB(255, 96, 96) : RGB(200, 0, 0);
    case Tone::Charging:
        return darkMode ? RGB(96, 224, 96) : RGB(0, 140, 0);
    default:
        return normal;
    }
}

int BatteryItem::MeasureWidth(HDC dc, const std::wstring& label, const std::wstring& value)
{
    auto textWidth = [dc](const std::wstring& text) {
        if (text.empty())
            return 0;
        // DT_CALCRECT measures like DrawTextW, i.e. with font fallback (e.g. for "⚡").
        RECT r{};
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        return static_cast<int>(r.right - r.left);
    };
    return (label.empty() ? 0 : textWidth(label) + GapWidth(dc)) + textWidth(value);
}

void BatteryItem::DrawLabelValue(HDC dc, const RECT& rect, const std::wstring& label, COLORREF labelColor,
    const std::wstring& value, COLORREF valueColor, bool valueRightAlign)
{
    constexpr UINT kFlags = DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    const int saved = SaveDC(dc);
    SetBkMode(dc, TRANSPARENT);

    RECT valueRect = rect;
    if (!label.empty())
    {
        RECT labelRect = rect;
        SetTextColor(dc, labelColor);
        DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &labelRect, DT_LEFT | kFlags);
        // Use the same DrawTextW measurement as MeasureWidth to avoid an extra-looking gap
        // caused by mixing GDI text metrics with DrawTextW's actual layout.
        RECT labelBounds{};
        DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &labelBounds,
            DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        const int labelWidth = static_cast<int>(labelBounds.right - labelBounds.left);
        valueRect.left = std::min(rect.right, rect.left + labelWidth + GapWidth(dc));
    }
    if (!value.empty())
    {
        SetTextColor(dc, valueColor);
        DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &valueRect,
            (valueRightAlign ? DT_RIGHT : DT_LEFT) | kFlags);
    }
    if (saved != 0)
        RestoreDC(dc, saved);
}

std::wstring BatteryItem::FormatValue(const BatteryStatus& status, const DisplayOptions& options)
{
    switch (status.state)
    {
    case BatteryState::Disconnected:
        return options.showDisconnected ? L"—" : L"";
    case BatteryState::Unknown:
    case BatteryState::Error:
        return L"?";
    default:
        break;
    }
    if (!status.available)
        return L"?";

    const wchar_t* percentSign = options.showPercentSign ? L"%" : L"";
    int percentage = status.percentage;
    if (status.state == BatteryState::Full && percentage < 0)
        percentage = 100;
    std::wstring text = percentage >= 0 ? std::to_wstring(percentage) + percentSign : L"";

    // "⚡" checked with GDI font fallback (Segoe UI 9 pt, as TrafficMonitor draws).
    const bool onPower = status.state == BatteryState::Charging || status.state == BatteryState::Full;
    if (onPower && options.showCharging)
        return L"⚡" + text;       // while charging without a percentage only "⚡"
    return text.empty() ? L"?" : text;
}

std::wstring BatteryItem::FormatTooltipLine(const wchar_t* label, const BatteryStatus& status)
{
    std::wstring line = std::wstring(label) + L": ";
    if (!status.deviceName.empty())
        line += status.deviceName + L" – ";

    if (status.state == BatteryState::Disconnected)
        return line + L"not connected";
    if (!status.available)
        return line + L"no value";

    const std::wstring percent = status.percentage >= 0 ? std::to_wstring(status.percentage) + L"%" : L"";
    switch (status.state)
    {
    case BatteryState::Charging:
        line += percent.empty() ? L"charging" : L"charging, " + percent;
        break;
    case BatteryState::Full:
        line += L"full";
        break;
    default:
        line += percent.empty() ? L"?" : percent;
        break;
    }
    if (status.millivolts > 0)
        line += L" (" + std::to_wstring(status.millivolts) + L" mV)";

    if (status.lastUpdate.time_since_epoch().count() != 0)
    {
        const std::time_t t = std::chrono::system_clock::to_time_t(status.lastUpdate);
        std::tm local{};
        if (localtime_s(&local, &t) == 0)
        {
            wchar_t stamp[16];
            swprintf_s(stamp, L"%02d:%02d", local.tm_hour, local.tm_min);
            line += std::wstring(L", as of ") + stamp;
        }
    }
    // The dropout filter shows the last value even though the current query failed.
    if (!status.error.empty())
        line += L", currently unreachable";
    return line;
}
