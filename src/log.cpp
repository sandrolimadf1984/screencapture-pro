#include "log.h"

#include <cstdarg>
#include <deque>

namespace {
std::mutex g_mutex;
HANDLE g_file = INVALID_HANDLE_VALUE;
std::wstring g_dir, g_path;
std::deque<std::string> g_recent;

void CleanupOld(const std::wstring& dir) {
    // mantém só os 15 logs mais novos (os nomes têm a data, então a ordem
    // alfabética é a ordem cronológica)
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(JoinPath(dir, APP_ID_W L"_*.log").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do { files.push_back(fd.cFileName); } while (FindNextFileW(f, &fd));
    FindClose(f);
    std::sort(files.begin(), files.end());
    while (files.size() > 15) {
        DeleteFileW(JoinPath(dir, files.front()).c_str());
        files.erase(files.begin());
    }
}

void Write(const char* level, const std::string& msg) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::string line = Fmt("%04d-%02d-%02d %02d:%02d:%02d.%03d [%s] [t%lu] ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                           st.wSecond, st.wMilliseconds, level, (unsigned long)GetCurrentThreadId()) + msg;
    std::lock_guard<std::mutex> lk(g_mutex);
    g_recent.push_back(line);
    while (g_recent.size() > 400) g_recent.pop_front();
    if (g_file != INVALID_HANDLE_VALUE) {
        std::string l = line + "\r\n";
        DWORD wr;
        WriteFile(g_file, l.data(), (DWORD)l.size(), &wr, nullptr);
    }
#ifdef SCP_DEBUG_CONSOLE
    fprintf(stderr, "%s\n", line.c_str());
#endif
}

std::string VFmt(const char* fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(nullptr, 0, fmt, ap);
    std::string s;
    if (n > 0) {
        s.resize((size_t)n + 1);
        vsnprintf(s.data(), s.size(), fmt, ap2);
        s.resize((size_t)n);
    }
    va_end(ap2);
    return s;
}
}  // namespace

namespace Log {
void Init(const std::wstring& dir) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_file != INVALID_HANDLE_VALUE) { CloseHandle(g_file); g_file = INVALID_HANDLE_VALUE; }
    g_dir = dir;
    if (!EnsureDir(dir)) return;
    g_path = JoinPath(dir, APP_ID_W L"_" + Widen(NowStamp("%Y-%m-%d")) + L".log");
    g_file = CreateFileW(g_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    CleanupOld(dir);
}

void Info(const char* fmt, ...) { va_list ap; va_start(ap, fmt); std::string s = VFmt(fmt, ap); va_end(ap); Write("INFO", s); }
void Warn(const char* fmt, ...) { va_list ap; va_start(ap, fmt); std::string s = VFmt(fmt, ap); va_end(ap); Write("AVISO", s); }
void Error(const char* fmt, ...) { va_list ap; va_start(ap, fmt); std::string s = VFmt(fmt, ap); va_end(ap); Write("ERRO", s); }
void Raw(const std::string& line) { Write("ffmpeg", line); }

std::vector<std::string> Recent(size_t maxLines) {
    std::lock_guard<std::mutex> lk(g_mutex);
    std::vector<std::string> v;
    size_t start = g_recent.size() > maxLines ? g_recent.size() - maxLines : 0;
    for (size_t i = start; i < g_recent.size(); i++) v.push_back(g_recent[i]);
    return v;
}

std::wstring CurrentFile() { std::lock_guard<std::mutex> lk(g_mutex); return g_path; }
std::wstring Dir() { std::lock_guard<std::mutex> lk(g_mutex); return g_dir; }
}  // namespace Log
