#include "common.h"

#include <shlobj.h>
#include <algorithm>
#include <cstdarg>
#include <ctime>

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::string Fmt(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    std::string out;
    if (n > 0) {
        out.resize((size_t)n + 1);
        vsnprintf(out.data(), out.size(), fmt, ap2);
        out.resize((size_t)n);
    }
    va_end(ap2);
    return out;
}

std::wstring WFmt(const wchar_t* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    wchar_t buf[2048];
    int n = _vsnwprintf(buf, 2047, fmt, ap);
    va_end(ap);
    if (n < 0) n = 2047;
    buf[n] = 0;
    return buf;
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
    return s;
}

bool StartsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

bool EndsWithI(const std::wstring& s, const std::wstring& suf) {
    if (s.size() < suf.size()) return false;
    return _wcsicmp(s.c_str() + s.size() - suf.size(), suf.c_str()) == 0;
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::vector<std::string> SplitLines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); i++) {
        if (i == s.size() || s[i] == '\n' || s[i] == '\r') {
            if (i > start) out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Relógios
// ---------------------------------------------------------------------------
int64_t WallClockUs() {
    // GetSystemTimePreciseAsFileTime existe do Windows 8 em diante. Carrego
    // dinamicamente só por garantia; se faltar, uso a versão comum (15 ms).
    using Fn = VOID(WINAPI*)(LPFILETIME);
    static Fn precise = (Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetSystemTimePreciseAsFileTime");
    FILETIME ft;
    if (precise) precise(&ft); else GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    // FILETIME conta intervalos de 100 ns desde 1601. Converto para µs desde 1970
    // (exatamente a mesma conta que o ffmpeg faz em av_gettime()).
    return (int64_t)(t / 10) - INT64_C(11644473600000000);
}

int64_t QpcFrequency() {
    static int64_t f = [] {
        LARGE_INTEGER li;
        QueryPerformanceFrequency(&li);
        return (int64_t)li.QuadPart;
    }();
    return f;
}

int64_t QpcNowUs() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    int64_t f = QpcFrequency();
    // divide em duas partes para não estourar o int64
    return (int64_t)(li.QuadPart / f) * 1000000 + (int64_t)((li.QuadPart % f) * 1000000 / f);
}

ClockPair ClockPair::Measure() {
    // Mede 5 vezes e fica com a medição mais "apertada" (menor intervalo entre
    // as duas leituras de QPC). Isso dá precisão de poucos microssegundos.
    ClockPair best;
    int64_t bestSpan = INT64_MAX;
    for (int i = 0; i < 5; i++) {
        int64_t q1 = QpcNowUs();
        int64_t w = WallClockUs();
        int64_t q2 = QpcNowUs();
        if (q2 - q1 < bestSpan) {
            bestSpan = q2 - q1;
            best.qpcUs = (q1 + q2) / 2;
            best.wallUs = w;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Formatação
// ---------------------------------------------------------------------------
std::string FormatBytes(uint64_t b) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = (double)b;
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; u++; }
    std::string s = (u == 0) ? Fmt("%.0f %s", v, units[u]) : Fmt(v < 10 ? "%.2f %s" : (v < 100 ? "%.1f %s" : "%.0f %s"), v, units[u]);
    // padrão brasileiro: vírgula decimal
    for (auto& c : s) if (c == '.') c = ',';
    return s;
}

std::string FormatDuration(double sec, bool forceHours) {
    if (sec < 0) sec = 0;
    int64_t t = (int64_t)sec;
    int h = (int)(t / 3600), m = (int)((t / 60) % 60), s = (int)(t % 60);
    if (forceHours || h > 0) return Fmt("%02d:%02d:%02d", h, m, s);
    return Fmt("%02d:%02d", m, s);
}

std::string FormatDateTime(const FILETIME& ft) {
    FILETIME local;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(&ft, &local);
    FileTimeToSystemTime(&local, &st);
    return Fmt("%02d/%02d/%04d %02d:%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
}

std::string NowStamp(const char* fmt) {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_s(&lt, &t);
    char buf[128];
    strftime(buf, sizeof(buf), fmt, &lt);
    return buf;
}

std::string LastErrorText(DWORD err) {
    wchar_t* msg = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, err, 0, (LPWSTR)&msg, 0, nullptr);
    std::string s = n ? Trim(Narrow(msg)) : Fmt("erro %lu", (unsigned long)err);
    if (msg) LocalFree(msg);
    return s + Fmt(" (código %lu)", (unsigned long)err);
}

std::string HResultText(HRESULT hr) {
    wchar_t* msg = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                             (DWORD)hr, 0, (LPWSTR)&msg, 0, nullptr);
    std::string s = n ? Trim(Narrow(msg)) + " " : std::string();
    if (msg) LocalFree(msg);
    return s + Fmt("[0x%08lX]", (unsigned long)hr);
}

// ---------------------------------------------------------------------------
// Arquivos e pastas
// ---------------------------------------------------------------------------
bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool EnsureDir(const std::wstring& p) {
    if (p.empty()) return false;
    if (DirExists(p)) return true;
    int r = SHCreateDirectoryExW(nullptr, p.c_str(), nullptr);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS || DirExists(p);
}

uint64_t FileSize(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

std::wstring ParentDir(const std::wstring& p) {
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L"";
    return p.substr(0, pos);
}

std::wstring FileNameOf(const std::wstring& p) {
    size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? p : p.substr(pos + 1);
}

std::wstring StripExtension(const std::wstring& n) {
    size_t pos = n.find_last_of(L'.');
    size_t slash = n.find_last_of(L"\\/");
    if (pos == std::wstring::npos || (slash != std::wstring::npos && pos < slash)) return n;
    return n.substr(0, pos);
}

bool ReadFileUtf8(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > 64 * 1024 * 1024) { CloseHandle(h); return false; }
    out.resize((size_t)sz.QuadPart);
    DWORD rd = 0;
    bool ok = out.empty() || (ReadFile(h, out.data(), (DWORD)out.size(), &rd, nullptr) && rd == out.size());
    CloseHandle(h);
    // remove BOM, se houver
    if (ok && out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    return ok;
}

bool WriteFileUtf8Atomic(const std::wstring& path, const std::string& data, bool durable) {
    // Escreve primeiro num arquivo temporário e só depois substitui o original.
    // Assim, se faltar luz no meio, o arquivo antigo continua inteiro.
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    bool ok = data.empty() || (WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr) && wr == data.size());
    // FlushFileBuffers força a gravação física (lento em pendrive); só quando pedido
    if (durable) FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | (durable ? MOVEFILE_WRITE_THROUGH : 0))) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}
