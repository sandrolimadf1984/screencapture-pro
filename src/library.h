#pragma once
#include <condition_variable>
#include "common.h"
#include "ffmpeg.h"

// Lista de gravações da pasta de destino, com duração/resolução lidas em
// segundo plano pelo ffmpeg (e guardadas num cache para abrir rápido depois).
struct RecordingItem {
    std::wstring path;
    std::string name;            // nome do arquivo (UTF-8)
    FILETIME modified{};
    uint64_t size = 0;
    MediaInfo info;
    bool infoReady = false;
};

class Library {
public:
    Library();
    ~Library();
    void Refresh(const std::wstring& dir);           // relê a pasta (rápido) e agenda a leitura dos detalhes
    std::vector<RecordingItem> Items();
    bool Busy() const { return pending_.load() > 0; }
    void Remove(const std::wstring& path);           // tira da lista (depois de excluir)
    std::wstring Dir();
    void Shutdown();

private:
    void Worker();
    void LoadCache();
    void SaveCache();
    std::mutex mtx_;
    std::condition_variable_any cv_;
    std::vector<RecordingItem> items_;
    std::vector<std::wstring> queue_;
    std::wstring dir_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<int> pending_{0};
    bool cacheLoaded_ = false;
    bool cacheDirty_ = false;
    struct CacheEntry { uint64_t size; uint64_t mtime; MediaInfo info; };
    std::vector<std::pair<std::wstring, CacheEntry>> cache_;
};
