// Apolo 0.6.0 - monitoramento de desempenho e limpeza conservadora para Windows 11.
// Implementacao independente, utilizando APIs documentadas do Windows.

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>

#include "performance.h"
#include "analysis.h"
#include "modern_ui.h"
#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"ApoloMainWindow";
constexpr UINT_PTR kRefreshTimer = 1;
constexpr int kRefreshButton = 101;
constexpr int kCleanButton = 102;
constexpr UINT kRefreshIntervalMs = 2500;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayId = 1;
constexpr int kOpenMenu = 301, kCleanMenu = 302, kHistoryMenu = 303, kExitMenu = 304;
constexpr std::array<int, 6> kLimits = {70, 75, 80, 85, 90, 95};
constexpr std::array<int, 5> kIntervals = {15, 30, 60, 120, 240};
struct Options {
    bool automatic = false;
    int threshold = 85;
    int cooldown = 60;
    bool startHidden = false;
};
Options gOptions;
HWND gWindow = nullptr;
apolo::UiSnapshot gUiSnapshot;
unsigned gPerformanceTicks = 0;
bool gLocked = false, gTray = false, gExiting = false;
ULONGLONG gLastAttempt = 0;
UINT gTaskbarCreated = 0;
std::wstring gConfig, gLog, gPolicyFile;



struct MemorySnapshot {
    ULONGLONG total = 0;
    ULONGLONG available = 0;
    ULONGLONG commitUsed = 0;
    ULONGLONG commitLimit = 0;
    DWORD loadPercent = 0;
    bool valid = false;
};

MemorySnapshot gLastSnapshot;

struct CleanupResult {
    unsigned eligible = 0;
    unsigned cleaned = 0;
    unsigned skipped = 0;
    MemorySnapshot before;
    MemorySnapshot after;
};

bool exists(const std::wstring& path) {
    DWORD attrs = path.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}
std::wstring knownPath(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) return L"";
    std::wstring result(p);
    CoTaskMemFree(p);
    return result;
}
template<size_t N> int closest(int value, const std::array<int,N>& a) {
    int result = a[0];
    for (int option : a)
        if (std::abs(option-value) < std::abs(result-value)) result = option;
    return result;
}
void loadConfig() {
    std::wstring user = knownPath(FOLDERID_LocalAppData);
    if (!user.empty()) {
        std::wstring dir = user + L"\\Apolo";
        CreateDirectoryW(dir.c_str(), nullptr);
        gConfig = dir + L"\\settings.ini";
        gLog = dir + L"\\history.csv";
    }
    std::wstring shared = knownPath(FOLDERID_ProgramData);
    if (!shared.empty()) {
        gPolicyFile = shared + L"\\Apolo\\policy.ini";
        gLocked = exists(gPolicyFile);
    }
    const auto read = [](const std::wstring& path) {
        if (!exists(path)) return;
        gOptions.automatic = GetPrivateProfileIntW(L"Apolo", L"AutoCleanEnabled",
            gOptions.automatic ? 1 : 0, path.c_str()) != 0;
        gOptions.threshold = GetPrivateProfileIntW(L"Apolo", L"AutoCleanThreshold",
            gOptions.threshold, path.c_str());
        gOptions.cooldown = GetPrivateProfileIntW(L"Apolo", L"AutoCleanCooldownMinutes",
            gOptions.cooldown, path.c_str());
        gOptions.startHidden = GetPrivateProfileIntW(L"Apolo", L"StartMinimized",
            gOptions.startHidden ? 1 : 0, path.c_str()) != 0;
    };
    read(gConfig);
    if (gLocked) read(gPolicyFile);
    gOptions.threshold = closest(gOptions.threshold, kLimits);
    gOptions.cooldown = closest(gOptions.cooldown, kIntervals);
}
bool writeConfig() {
    if (gLocked || gConfig.empty()) return false;
    const auto write = [](const wchar_t* key, int value) {
        std::wstring text = std::to_wstring(value);
        return WritePrivateProfileStringW(L"Apolo", key, text.c_str(),
                                           gConfig.c_str()) != FALSE;
    };
    return write(L"AutoCleanEnabled", gOptions.automatic ? 1 : 0) &&
           write(L"AutoCleanThreshold", gOptions.threshold) &&
           write(L"AutoCleanCooldownMinutes", gOptions.cooldown) &&
           write(L"StartMinimized", gOptions.startHidden ? 1 : 0);
}
double toGiB(ULONGLONG bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
}

MemorySnapshot readMemory() {
    MEMORYSTATUSEX status = {};
    status.dwLength = sizeof(status);

    MemorySnapshot result;
    if (!GlobalMemoryStatusEx(&status)) {
        return result;
    }

    result.total = status.ullTotalPhys;
    result.available = status.ullAvailPhys;
    result.loadPercent = status.dwMemoryLoad;
    result.valid = true;

    PERFORMANCE_INFORMATION perf = {};
    perf.cb = sizeof(perf);

    if (GetPerformanceInfo(&perf, sizeof(perf))) {
        result.commitUsed =
            static_cast<ULONGLONG>(perf.CommitTotal) * perf.PageSize;
        result.commitLimit =
            static_cast<ULONGLONG>(perf.CommitLimit) * perf.PageSize;
    }

    return result;
}

void refreshDisplay() {
    gLastSnapshot=readMemory();
    const auto& m=gLastSnapshot;
    gUiSnapshot.ramValid=m.valid;
    if(m.valid && m.total>0) {
        gUiSnapshot.ramPercent=static_cast<double>(m.loadPercent);
        gUiSnapshot.ramTotalGiB=toGiB(m.total);
        gUiSnapshot.ramUsedGiB=toGiB(m.total-m.available);
        gUiSnapshot.ramAvailableGiB=toGiB(m.available);
    }
    gUiSnapshot.commitUsedGiB=toGiB(m.commitUsed);
    gUiSnapshot.commitLimitGiB=toGiB(m.commitLimit);
    apolo::modernSetSnapshot(gUiSnapshot);
}

void refreshPerformanceDisplay(bool updateProcessList) {
    const auto performance=apolo::samplePerformance(updateProcessList);
    gUiSnapshot.cpuValid=performance.cpuValid;
    gUiSnapshot.cpuPercent=performance.cpuPercent;
    gUiSnapshot.diskValid=performance.diskValid;
    gUiSnapshot.diskReadMiB=performance.diskReadBytesPerSecond/1048576.0;
    gUiSnapshot.diskWriteMiB=performance.diskWriteBytesPerSecond/1048576.0;

    const auto& m=gLastSnapshot;
    const bool ramValid=m.valid && m.total>0;
    const bool commitValid=m.commitLimit>0;
    const double available=ramValid?
        static_cast<double>(m.available)*100.0/static_cast<double>(m.total):0.0;
    const double commit=commitValid?
        static_cast<double>(m.commitUsed)*100.0/static_cast<double>(m.commitLimit):0.0;
    apolo::recordTelemetry(performance,ramValid,
                           static_cast<double>(m.loadPercent),
                           available,commitValid,commit);
    gUiSnapshot.diagnosis=apolo::diagnosticSummary();
    apolo::modernSetSnapshot(gUiSnapshot);
    if(gWindow && IsWindowVisible(gWindow))InvalidateRect(gWindow,nullptr,FALSE);
}

bool readTokenUser(HANDLE token, std::vector<BYTE>& output, PSID& sid) {
    DWORD needed = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);

    if (needed < sizeof(TOKEN_USER) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return false;
    }

    output.resize(needed);
    if (!GetTokenInformation(token, TokenUser, output.data(), needed, &needed)) {
        return false;
    }

    sid = reinterpret_cast<TOKEN_USER*>(output.data())->User.Sid;
    return sid != nullptr && IsValidSid(sid);
}

bool isProtectedShellProcess(const wchar_t* filename) {
    // Evita perturbar processos centrais do Windows e a propria interface.
    constexpr std::array<const wchar_t*, 16> excluded = {
        L"Apolo.exe", L"explorer.exe", L"dwm.exe", L"csrss.exe",
        L"winlogon.exe", L"wininit.exe", L"services.exe", L"lsass.exe",
        L"svchost.exe", L"sihost.exe", L"ShellExperienceHost.exe",
        L"StartMenuExperienceHost.exe", L"SearchHost.exe",
        L"RuntimeBroker.exe", L"TextInputHost.exe", L"ApplicationFrameHost.exe"
    };

    for (const wchar_t* name : excluded) {
        if (_wcsicmp(filename, name) == 0) {
            return true;
        }
    }
    return false;
}

CleanupResult cleanCurrentUserProcesses() {
    CleanupResult result;
    result.before = readMemory();

    HANDLE currentToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &currentToken)) {
        result.after = readMemory();
        return result;
    }

    std::vector<BYTE> currentTokenBuffer;
    PSID currentSid = nullptr;
    if (!readTokenUser(currentToken, currentTokenBuffer, currentSid)) {
        CloseHandle(currentToken);
        result.after = readMemory();
        return result;
    }

    DWORD currentSession = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSession)) {
        CloseHandle(currentToken);
        result.after = readMemory();
        return result;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        CloseHandle(currentToken);
        result.after = readMemory();
        return result;
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            const DWORD pid = entry.th32ProcessID;
            if (pid == 0 || pid == GetCurrentProcessId() ||
                isProtectedShellProcess(entry.szExeFile)) {
                continue;
            }

            DWORD session = 0;
            if (!ProcessIdToSessionId(pid, &session) || session != currentSession) {
                continue;
            }

            // Primeiro solicita somente leitura; privilegios de alteracao
            // sao solicitados apenas apos confirmar usuario e sessao.
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                         FALSE, pid);

            if (!process) {
                ++result.skipped;
                continue;
            }

            HANDLE token = nullptr;
            if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
                ++result.skipped;
                CloseHandle(process);
                continue;
            }

            std::vector<BYTE> candidateTokenBuffer;
            PSID candidateSid = nullptr;
            const bool sameUser =
                readTokenUser(token, candidateTokenBuffer, candidateSid) &&
                EqualSid(currentSid, candidateSid);
            CloseHandle(token);

            if (sameUser) {
                ++result.eligible;
                HANDLE writable = OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_QUOTA,
                    FALSE, pid);
                if (writable && EmptyWorkingSet(writable)) {
                    ++result.cleaned;
                } else {
                    ++result.skipped;
                }
                if (writable) {
                    CloseHandle(writable);
                }
            }

            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    CloseHandle(currentToken);
    result.after = readMemory();
    return result;
}


bool history(bool autoMode, const CleanupResult& r) {
    if (gLog.empty() || !r.before.valid || !r.after.valid) return false;
    HANDLE h = CreateFileW(gLog.c_str(), FILE_APPEND_DATA | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length = {};
    bool ok = GetFileSizeEx(h, &length) != FALSE;
    if (ok && length.QuadPart == 0) {
        constexpr char header[] = "data_hora;origem;tratados;elegiveis;ignorados;disponivel_antes_bytes;disponivel_depois_bytes;variacao_bytes\r\n";
        DWORD written = 0;
        ok = WriteFile(h, header, DWORD(sizeof(header)-1), &written, nullptr) != FALSE &&
             written == sizeof(header)-1;
    }
    SYSTEMTIME t = {};
    GetLocalTime(&t);
    char line[512] = {};
    LONGLONG delta = LONGLONG(r.after.available)-LONGLONG(r.before.available);
    int n = std::snprintf(line,sizeof(line),
        "%04u-%02u-%02u %02u:%02u:%02u;%s;%u;%u;%u;%llu;%llu;%lld\r\n",
        unsigned(t.wYear),unsigned(t.wMonth),unsigned(t.wDay),
        unsigned(t.wHour),unsigned(t.wMinute),unsigned(t.wSecond),
        autoMode ? "automatica" : "manual",r.cleaned,r.eligible,r.skipped,
        static_cast<unsigned long long>(r.before.available),
        static_cast<unsigned long long>(r.after.available),
        static_cast<long long>(delta));
    if (ok && n>0 && size_t(n)<sizeof(line)) {
        DWORD written = 0;
        ok = WriteFile(h,line,DWORD(n),&written,nullptr)!=FALSE &&
             written == DWORD(n);
    } else ok=false;
    CloseHandle(h);
    return ok;
}
void viewHistory(HWND window) {
    if (!exists(gLog)) {
        MessageBoxW(window,L"Nenhuma limpeza registrada.",L"Apolo",MB_OK | MB_ICONINFORMATION);
        return;
    }
    wchar_t path[MAX_PATH] = {};
    if (!GetSystemDirectoryW(path,MAX_PATH)) return;
    std::wstring notepad = std::wstring(path) + L"\\notepad.exe";
    std::wstring args = L"\"" + gLog + L"\"";
    ShellExecuteW(window,L"open",notepad.c_str(),args.c_str(),nullptr,SW_SHOWNORMAL);
}
void cleanNow(HWND window,bool autoMode) {
    if (!autoMode && MessageBoxW(window,
        L"Reduzir o working set de aplicativos pode causar lentidão temporária "
        L"e não garante aumento de desempenho.\n\nDeseja continuar?",
        L"Apolo - Confirmação",MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2)!=IDYES) return;
    gLastAttempt = GetTickCount64();
    apolo::modernSetStatus(autoMode ? L"Limpeza automática em execução..." : L"Limpeza manual em execução...");
    CleanupResult r = cleanCurrentUserProcesses();
    refreshDisplay();
    bool saved = history(autoMode,r);
    wchar_t text[420] = {};
    double delta = r.before.valid && r.after.valid ?
        toGiB(r.after.available)-toGiB(r.before.available) : 0.0;
    std::swprintf(text,std::size(text),
        L"%s: %u/%u processos (%u ignorados). Variação observada: %+.2f GiB.",
        autoMode ? L"Automática" : L"Manual",r.cleaned,r.eligible,r.skipped,delta);
    std::wstring message(text);
    if(!saved) message += L" Histórico não salvo.";
    apolo::modernSetStatus(message);
    if(gWindow && IsWindowVisible(gWindow))InvalidateRect(gWindow,nullptr,FALSE);
}
void openWindow(HWND h) { ShowWindow(h,SW_SHOWNORMAL); SetForegroundWindow(h); }
bool iconAdd(HWND h) {
    NOTIFYICONDATAW n = {};
    n.cbSize=sizeof(n);n.hWnd=h;n.uID=kTrayId;
    n.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;n.uCallbackMessage=kTrayMessage;
    n.hIcon=LoadIconW(nullptr,IDI_INFORMATION);
    wcscpy_s(n.szTip,L"Apolo - Monitor de memória");
    gTray = Shell_NotifyIconW(NIM_ADD,&n)!=FALSE;
    if(gTray) {n.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&n);}
    return gTray;
}
void iconRemove(HWND h) {
    if(!gTray)return;
    NOTIFYICONDATAW n={};n.cbSize=sizeof(n);n.hWnd=h;n.uID=kTrayId;
    Shell_NotifyIconW(NIM_DELETE,&n);gTray=false;
}
void trayMenu(HWND h) {
    HMENU menu=CreatePopupMenu();if(!menu)return;
    AppendMenuW(menu,MF_STRING,kOpenMenu,L"Abrir");
    AppendMenuW(menu,MF_STRING,kCleanMenu,L"Limpar agora...");
    AppendMenuW(menu,MF_STRING,kHistoryMenu,L"Ver histórico");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,kExitMenu,L"Sair do Apolo");
    POINT p={};GetCursorPos(&p);SetForegroundWindow(h);
    TrackPopupMenu(menu,TPM_RIGHTBUTTON|TPM_BOTTOMALIGN|TPM_LEFTALIGN,
                   p.x,p.y,0,h,nullptr);
    PostMessageW(h,WM_NULL,0,0);DestroyMenu(menu);
}
void saveUi(HWND window) {
    if(gLocked)return;
    const apolo::UiSettings candidate=apolo::modernGetSettings();
    const apolo::UiSettings previousUi={
        gOptions.automatic,gOptions.startHidden,gLocked,
        gOptions.threshold,gOptions.cooldown
    };
    if(candidate.automatic && !gOptions.automatic &&
       MessageBoxW(window,L"A limpeza automática pode causar lentidão. "
           L"Teste primeiro em uma única máquina.\n\nDeseja ativar?",
           L"Apolo - Confirmação",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES) {
        apolo::modernSetSettings(previousUi);
        InvalidateRect(window,nullptr,FALSE);
        return;
    }
    const Options previous=gOptions;
    gOptions.automatic=candidate.automatic;
    gOptions.startHidden=candidate.startHidden;
    gOptions.threshold=closest(candidate.threshold,kLimits);
    gOptions.cooldown=closest(candidate.cooldown,kIntervals);
    if(!writeConfig()) {
        gOptions=previous;
        apolo::modernSetSettings(previousUi);
        MessageBoxW(window,L"Falha ao salvar as configurações.",L"Apolo",MB_ICONERROR);
        return;
    }
    gLastAttempt=GetTickCount64();
    apolo::modernSetStatus(L"Configurações salvas. Intervalo de segurança reiniciado.");
    InvalidateRect(window,nullptr,FALSE);
}

LRESULT CALLBACK mainWindowProc(HWND window,UINT message,
                                WPARAM wParam,LPARAM lParam) {
    if(gTaskbarCreated && message==gTaskbarCreated) {
        iconAdd(window);return 0;
    }
    switch(message) {
    case WM_CREATE: {
        gWindow=window;
        // Titlebar escura no Windows 11, sem compositor de terceiros.
        const BOOL dark=TRUE;
        DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
        if(!apolo::modernCreate(window)) {
            MessageBoxW(window,L"Não foi possível inicializar o Direct2D/DirectWrite.",
                        L"Apolo",MB_OK|MB_ICONERROR);
            return -1;
        }
        apolo::modernSetSettings({
            gOptions.automatic,gOptions.startHidden,gLocked,
            gOptions.threshold,gOptions.cooldown
        });
        apolo::initializePerformance();
        iconAdd(window);
        gLastAttempt=GetTickCount64();
        SetTimer(window,kRefreshTimer,kRefreshIntervalMs,nullptr);
        refreshDisplay();
        refreshPerformanceDisplay(true);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* limits=reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize.x=950;
        limits->ptMinTrackSize.y=765;
        return 0;
    }
    case WM_SIZE:
        if(wParam!=SIZE_MINIMIZED)
            apolo::modernResize(LOWORD(lParam),HIWORD(lParam));
        return 0;
    case WM_PAINT:
        apolo::modernPaint(window);
        return 0;
    case WM_ERASEBKGND:
        return 1; // Direct2D pinta toda a superfície; evita flash branco.
    case WM_LBUTTONUP: {
        const auto action=apolo::modernClick(
            window,GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));
        switch(action) {
        case apolo::UiAction::Refresh:
            refreshDisplay();
            refreshPerformanceDisplay(true);
            break;
        case apolo::UiAction::Clean:
            cleanNow(window,false);
            break;
        case apolo::UiAction::OpenCsv:
            viewHistory(window);
            break;
        case apolo::UiAction::SaveSettings:
            saveUi(window);
            break;
        case apolo::UiAction::LoadHistory:
            apolo::modernLoadHistoryCsv(gLog);
            InvalidateRect(window,nullptr,FALSE);
            break;
        default: break;
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
        apolo::modernWheel(window,GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    case WM_MOUSEMOVE:
        SetCursor(LoadCursorW(nullptr,apolo::modernOverControl(
            GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam))?IDC_HAND:IDC_ARROW));
        return 0;
    case WM_TIMER:
        if(wParam==kRefreshTimer) {
            refreshDisplay();
            refreshPerformanceDisplay((++gPerformanceTicks%2)==1);
            const ULONGLONG elapsed=GetTickCount64()-gLastAttempt;
            const ULONGLONG interval=
                static_cast<ULONGLONG>(gOptions.cooldown)*60000ULL;
            if(gOptions.automatic && gLastSnapshot.valid &&
               gLastSnapshot.loadPercent>=static_cast<DWORD>(gOptions.threshold) &&
               elapsed>=interval)cleanNow(window,true);
        }
        return 0;
    case WM_COMMAND:
        switch(LOWORD(wParam)) {
        case kCleanMenu:cleanNow(window,false);break;
        case kHistoryMenu:viewHistory(window);break;
        case kOpenMenu:openWindow(window);break;
        case kExitMenu:gExiting=true;DestroyWindow(window);break;
        }
        return 0;
    case kTrayMessage: {
        const UINT e=LOWORD(lParam);
        if(e==WM_LBUTTONDBLCLK||e==NIN_SELECT||e==NIN_KEYSELECT)
            openWindow(window);
        else if(e==WM_RBUTTONUP||e==WM_CONTEXTMENU)trayMenu(window);
        return 0;
    }
    case WM_CLOSE:
        if(!gExiting && gTray) {ShowWindow(window,SW_HIDE);return 0;}
        DestroyWindow(window);return 0;
    case WM_DESTROY:
        KillTimer(window,kRefreshTimer);
        apolo::shutdownPerformance();
        iconRemove(window);
        apolo::modernDestroy();
        gWindow=nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDPIAware();
    loadConfig();
    gTaskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    HWND running=FindWindowW(kWindowClass,nullptr);
    if(running){openWindow(running);return 0;}

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = mainWindowProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&wc)) {
        return 1;
    }

    HWND window = CreateWindowExW(
        0, kWindowClass, L"Apolo v0.6 - Desempenho",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1140, 830,
        nullptr, nullptr, instance, nullptr);

    if (!window) {
        return 2;
    }

    if(!gOptions.startHidden || !gTray) {
        ShowWindow(window,showCommand);
        UpdateWindow(window);
    }

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
