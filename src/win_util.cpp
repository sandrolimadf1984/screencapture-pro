#include "win_util.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include "log.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

bool PickFolderDialog(HWND owner, const std::wstring& initialDir, std::wstring& outDir) {
    IFileOpenDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (FAILED(hr)) {
        Log::Error("IFileOpenDialog indisponível: %s", HResultText(hr).c_str());
        return false;
    }
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(L"Escolha a pasta onde os vídeos serão salvos");
    dlg->SetOkButtonLabel(L"Usar esta pasta");
    if (!initialDir.empty() && DirExists(initialDir)) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initialDir.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            dlg->SetFolder(folder);
            folder->Release();
        }
    }
    bool ok = false;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                outDir = path;
                ok = true;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
}

void OpenFolderInExplorer(const std::wstring& dir) {
    ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"\"" + dir + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}

void RevealFileInExplorer(const std::wstring& file) {
    // SHOpenFolderAndSelectItems abre o Explorer já com o arquivo selecionado
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(file.c_str());
    if (pidl) {
        HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
        if (SUCCEEDED(hr)) return;
    }
    std::wstring args = L"/select,\"" + file + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

bool OpenWithDefaultApp(const std::wstring& file) {
    HINSTANCE r = ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        Log::Warn("ShellExecute falhou (%d) para %s", (int)(INT_PTR)r, Narrow(file).c_str());
        // sem programa associado: mostra o "Abrir com..."
        std::wstring args = L"shell32.dll,OpenAs_RunDLL " + file;
        ShellExecuteW(nullptr, L"open", L"rundll32.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        return false;
    }
    return true;
}

bool CopyTextToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (mem) {
        void* p = GlobalLock(mem);
        memcpy(p, text.c_str(), bytes);
        GlobalUnlock(mem);
        ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}

bool MoveToRecycleBin(const std::wstring& file, std::string* error) {
    // SHFileOperation precisa de lista terminada em DOIS zeros
    std::wstring from = file;
    from.push_back(L'\0');
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    int r = SHFileOperationW(&op);
    if (r != 0 || op.fAnyOperationsAborted) {
        // última tentativa: excluir direto
        if (DeleteFileW(file.c_str())) return true;
        if (error) *error = Fmt("Não foi possível excluir o arquivo (código %d). Ele pode estar aberto em outro programa.", r);
        return false;
    }
    return !FileExists(file);
}

bool GetDiskSpace(const std::wstring& path, uint64_t& freeBytes, uint64_t& totalBytes) {
    std::wstring p = path;
    // sobe até achar uma pasta que exista (a de gravações pode ainda não existir)
    while (!p.empty() && !DirExists(p)) p = ParentDir(p);
    if (p.empty()) return false;
    if (p.back() != L'\\') p += L'\\';
    ULARGE_INTEGER freeAvail, total, totalFree;
    if (!GetDiskFreeSpaceExW(p.c_str(), &freeAvail, &total, &totalFree)) return false;
    freeBytes = freeAvail.QuadPart;
    totalBytes = total.QuadPart;
    return true;
}

uint64_t DirectoryBytes(const std::wstring& dir) {
    uint64_t total = 0;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(JoinPath(dir, L"*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        // o tamanho da listagem pode estar atrasado para arquivos abertos: abre e pergunta
        std::wstring p = JoinPath(dir, fd.cFileName);
        HANDLE h = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        LARGE_INTEGER sz{};
        if (h != INVALID_HANDLE_VALUE && GetFileSizeEx(h, &sz)) total += (uint64_t)sz.QuadPart;
        else total += ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return total;
}

bool IsInputDesktopAvailable() {
    // Teste automatizado (o Wine não tem tela bloqueada): com SCP_TEST_LOCKFILE=caminho,
    // a tela "fica bloqueada" enquanto esse arquivo existir.
    static const std::wstring testLock = [] {
        wchar_t b[MAX_PATH] = {};
        DWORD n = GetEnvironmentVariableW(L"SCP_TEST_LOCKFILE", b, MAX_PATH);
        return n > 0 && n < MAX_PATH ? std::wstring(b) : std::wstring();
    }();
    if (!testLock.empty() && FileExists(testLock)) return false;
    // Na área de trabalho segura (Winlogon) o programa não tem acesso: a abertura falha.
    HDESK d = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!d) return false;
    wchar_t name[64] = {};
    DWORD need = 0;
    bool ok = GetUserObjectInformationW(d, UOI_NAME, name, sizeof(name), &need) != 0;
    CloseDesktop(d);
    return ok && _wcsicmp(name, L"Default") == 0;
}

std::wstring DriveLabel(const std::wstring& path) {
    if (path.size() >= 2 && path[1] == L':') return path.substr(0, 2);
    if (path.size() > 2 && path[0] == L'\\' && path[1] == L'\\') {
        size_t s = path.find(L'\\', 2);
        size_t s2 = s == std::wstring::npos ? s : path.find(L'\\', s + 1);
        return path.substr(0, s2);
    }
    return path;
}

std::wstring FileSystemName(const std::wstring& path) {
    std::wstring p = path;
    while (!p.empty() && !DirExists(p)) p = ParentDir(p);
    if (p.empty()) return L"";
    wchar_t root[MAX_PATH] = {0};
    if (!GetVolumePathNameW(p.c_str(), root, MAX_PATH)) return L"";
    wchar_t fs[64] = {0};
    if (!GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, nullptr, fs, 63)) return L"";
    return fs;
}

void SetDarkTitleBar(HWND hwnd, bool dark) {
    BOOL v = dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &v, sizeof(v)))) {
        DwmSetWindowAttribute(hwnd, 19, &v, sizeof(v));   // builds antigos do Win10 usavam 19
    }
}

void SetWindowCorners(HWND hwnd, int preference) {
    DWORD v = (DWORD)preference;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &v, sizeof(v));   // só tem efeito no Win11
}

WinVersion GetWindowsVersion() {
    static WinVersion cached = [] {
        WinVersion v;
        using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        auto fn = (RtlGetVersionFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
        if (fn) {
            RTL_OSVERSIONINFOW info = {};
            info.dwOSVersionInfoSize = sizeof(info);
            if (fn(&info) == 0) {
                v.major = info.dwMajorVersion;
                v.minor = info.dwMinorVersion;
                v.build = info.dwBuildNumber;
            }
        }
        return v;
    }();
    return cached;
}

std::string WindowsVersionText() {
    WinVersion v = GetWindowsVersion();
    const char* name = "Windows";
    if (v.major == 10 && v.build >= 22000) name = "Windows 11";
    else if (v.major == 10) name = "Windows 10";
    else if (v.major == 6 && v.minor == 3) name = "Windows 8.1";
    else if (v.major == 6 && v.minor == 1) name = "Windows 7";
    return Fmt("%s (build %lu)", name, (unsigned long)v.build);
}

bool IsCaptureExclusionSupported() {
    WinVersion v = GetWindowsVersion();
    return v.major > 10 || (v.major == 10 && v.build >= 19041);
}

bool ExcludeWindowFromCapture(HWND hwnd) {
    if (!SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)) {
        Log::Warn("SetWindowDisplayAffinity(EXCLUDEFROMCAPTURE) falhou: %s", LastErrorText().c_str());
        return false;
    }
    DWORD aff = 0;
    if (GetWindowDisplayAffinity(hwnd, &aff) && aff != WDA_EXCLUDEFROMCAPTURE) {
        // Windows mais antigo aceita a chamada mas vira WDA_MONITOR (tarja preta).
        Log::Warn("Afinidade efetiva = 0x%lx (exclusão não suportada de verdade)", (unsigned long)aff);
        SetWindowDisplayAffinity(hwnd, WDA_NONE);
        return false;
    }
    return true;
}

void IncludeWindowInCapture(HWND hwnd) {
    if (hwnd) SetWindowDisplayAffinity(hwnd, WDA_NONE);
}

UINT DpiForWindow(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (fn && hwnd) {
        UINT d = fn(hwnd);
        if (d) return d;
    }
    HDC dc = GetDC(nullptr);
    UINT d = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    return d ? d : 96;
}

UINT DpiForMonitor(HMONITOR mon) {
    using Fn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static Fn fn = [] {
        HMODULE m = LoadLibraryW(L"shcore.dll");
        return m ? (Fn)GetProcAddress(m, "GetDpiForMonitor") : (Fn) nullptr;
    }();
    UINT x = 96, y = 96;
    if (fn && SUCCEEDED(fn(mon, 0 /*MDT_EFFECTIVE_DPI*/, &x, &y)) && x) return x;
    return DpiForWindow(nullptr);
}

void FatalMessage(const std::string& title, const std::string& text) {
    MessageBoxW(nullptr, Widen(text).c_str(), Widen(title).c_str(), MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}
