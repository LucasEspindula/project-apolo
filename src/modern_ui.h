#pragma once
#include <windows.h>
#include <string>

namespace apolo {
enum class UiAction {
    None, Refresh, Clean, OpenCsv, SaveSettings, LoadHistory
};
struct UiSnapshot {
    bool ramValid = false;
    double ramPercent = 0, ramUsedGiB = 0, ramTotalGiB = 0;
    double ramAvailableGiB = 0, commitUsedGiB = 0, commitLimitGiB = 0;
    bool cpuValid = false;
    double cpuPercent = 0;
    bool diskValid = false;
    double diskReadMiB = 0, diskWriteMiB = 0;
    std::wstring diagnosis;
};
struct UiSettings {
    bool automatic = false, startHidden = false, locked = false;
    int threshold = 85, cooldown = 60;
};
bool modernCreate(HWND window);
void modernDestroy();
void modernResize(unsigned width, unsigned height);
void modernPaint(HWND window);
void modernSetSnapshot(const UiSnapshot& snapshot);
void modernSetSettings(const UiSettings& settings);
UiSettings modernGetSettings();
void modernSetStatus(const std::wstring& status);
void modernLoadHistoryCsv(const std::wstring& path);
UiAction modernClick(HWND window, int x, int y);
void modernWheel(HWND window, int delta);
bool modernOverControl(int x, int y);
} // namespace apolo
