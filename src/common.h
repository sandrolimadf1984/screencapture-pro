#pragma once
// Cabeçalho comum: inclui o Windows do jeito certo e junta utilidades
// pequenas (conversão UTF-8 <-> UTF-16, formatação, relógios).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "branding.h"

// ---- strings --------------------------------------------------------------
// Todo texto interno do programa é UTF-8 (std::string). Só na hora de falar
// com a API do Windows é que convertemos para UTF-16 (std::wstring).
std::wstring Widen(const std::string& utf8);
std::string Narrow(const std::wstring& wide);
std::string Fmt(const char* fmt, ...);            // printf que devolve std::string
std::wstring WFmt(const wchar_t* fmt, ...);
std::string ToLower(std::string s);
bool StartsWith(const std::string& s, const std::string& prefix);
bool EndsWithI(const std::wstring& s, const std::wstring& suffix);
std::string Trim(const std::string& s);
void ReplaceAll(std::string& s, const std::string& from, const std::string& to);
std::vector<std::string> SplitLines(const std::string& s);

// ---- relógios ---------------------------------------------------------------
// WallClockUs: hora do sistema (UTC) em microssegundos desde 1970, precisa
// (GetSystemTimePreciseAsFileTime). É o MESMO relógio que o ffmpeg usa nos
// timestamps (av_gettime), por isso dá para alinhar áudio e vídeo.
int64_t WallClockUs();
// QpcNowUs: contador de alta resolução (QueryPerformanceCounter) em µs.
int64_t QpcNowUs();
int64_t QpcFrequency();
// Converte uma posição QPC em unidades de 100 ns (formato da WASAPI) para µs.
inline int64_t Qpc100nsToUs(uint64_t v) { return (int64_t)(v / 10); }

// Par (QPC, relógio) medido no mesmo instante — usado para converter os
// timestamps da placa de som (QPC) para o relógio do sistema.
struct ClockPair {
    int64_t qpcUs = 0;
    int64_t wallUs = 0;
    static ClockPair Measure();
    int64_t QpcToWall(int64_t qpcUs_) const { return wallUs + (qpcUs_ - qpcUs); }
};

// ---- formatação para a interface --------------------------------------------
std::string FormatBytes(uint64_t bytes);           // "1,5 GB"
std::string FormatDuration(double seconds, bool forceHours = true); // "00:02:35"
std::string FormatDateTime(const FILETIME& ft);    // "11/10/2026 15:30"
std::string NowStamp(const char* fmt);             // strftime com hora local

// ---- erros -----------------------------------------------------------------
std::string LastErrorText(DWORD err = GetLastError());
std::string HResultText(HRESULT hr);

// ---- pequenas ajudas de COM --------------------------------------------------
template <typename T>
inline void SafeRelease(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

struct ScopedCom {
    HRESULT hr;
    explicit ScopedCom(DWORD model = COINIT_MULTITHREADED) { hr = CoInitializeEx(nullptr, model); }
    ~ScopedCom() { if (SUCCEEDED(hr)) CoUninitialize(); }
};

// ---- caminhos ----------------------------------------------------------------
bool FileExists(const std::wstring& path);
bool DirExists(const std::wstring& path);
bool EnsureDir(const std::wstring& path);        // cria a pasta (e as de cima)
uint64_t FileSize(const std::wstring& path);
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
std::wstring ParentDir(const std::wstring& path);
std::wstring FileNameOf(const std::wstring& path);
std::wstring StripExtension(const std::wstring& name);
bool ReadFileUtf8(const std::wstring& path, std::string& out);
bool WriteFileUtf8Atomic(const std::wstring& path, const std::string& data, bool durable = false); // escreve em .tmp e troca
