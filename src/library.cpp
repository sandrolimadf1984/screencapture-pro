#include "library.h"

#include <algorithm>
#include <condition_variable>

#include "log.h"
#include "paths.h"
#include "../third_party/nlohmann/json.hpp"

using json = nlohmann::json;

static uint64_t Ft64(const FILETIME& f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; }
static std::wstring CachePath() { return JoinPath(Paths().configDir, L"biblioteca_cache.json"); }

Library::Library() { thread_ = std::thread([this] { Worker(); }); }
Library::~Library() { Shutdown(); }

void Library::Shutdown() {
    stop_ = true;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lk(mtx_);
    if (cacheDirty_) SaveCache();
}

std::wstring Library::Dir() {
    std::lock_guard<std::mutex> lk(mtx_);
    return dir_;
}

void Library::LoadCache() {
    cacheLoaded_ = true;
    std::string text;
    if (!ReadFileUtf8(CachePath(), text)) return;
    try {
        json j = json::parse(text);
        for (auto& e : j["items"]) {
            CacheEntry c;
            c.size = e.value("size", (uint64_t)0);
            c.mtime = e.value("mtime", (uint64_t)0);
            c.info.ok = e.value("ok", false);
            c.info.duration = e.value("duration", 0.0);
            c.info.width = e.value("w", 0);
            c.info.height = e.value("h", 0);
            c.info.fps = e.value("fps", 0.0);
            c.info.vcodec = e.value("vcodec", "");
            c.info.acodec = e.value("acodec", "");
            c.info.container = e.value("container", "");
            c.info.audioTracks = e.value("tracks", 0);
            cache_.push_back({Widen(e.value("path", "")), c});
        }
    } catch (...) {
        cache_.clear();
    }
}

void Library::SaveCache() {
    json items = json::array();
    for (auto& [p, c] : cache_) {
        if (!FileExists(p)) continue;   // limpa itens de arquivos que não existem mais
        items.push_back({{"path", Narrow(p)}, {"size", c.size}, {"mtime", c.mtime}, {"ok", c.info.ok}, {"duration", c.info.duration},
                         {"w", c.info.width}, {"h", c.info.height}, {"fps", c.info.fps}, {"vcodec", c.info.vcodec},
                         {"acodec", c.info.acodec}, {"container", c.info.container}, {"tracks", c.info.audioTracks}});
    }
    WriteFileUtf8Atomic(CachePath(), json{{"items", items}}.dump(1));
    cacheDirty_ = false;
}

void Library::Refresh(const std::wstring& dir) {
    std::vector<RecordingItem> found;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(JoinPath(dir, L"*").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring n = fd.cFileName;
            if (!(EndsWithI(n, L".mp4") || EndsWithI(n, L".avi") || EndsWithI(n, L".mkv") || EndsWithI(n, L".mov"))) continue;
            RecordingItem it;
            it.path = JoinPath(dir, n);
            it.name = Narrow(n);
            it.modified = fd.ftLastWriteTime;
            it.size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            found.push_back(it);
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    std::sort(found.begin(), found.end(), [](const RecordingItem& a, const RecordingItem& b) { return Ft64(a.modified) > Ft64(b.modified); });

    std::lock_guard<std::mutex> lk(mtx_);
    if (!cacheLoaded_) LoadCache();
    dir_ = dir;
    queue_.clear();
    for (auto& it : found) {
        for (auto& [p, c] : cache_) {
            if (_wcsicmp(p.c_str(), it.path.c_str()) == 0 && c.size == it.size && c.mtime == Ft64(it.modified)) {
                it.info = c.info;
                it.infoReady = true;
                break;
            }
        }
        if (!it.infoReady) queue_.push_back(it.path);
    }
    items_ = std::move(found);
    pending_ = (int)queue_.size();
    cv_.notify_all();
}

std::vector<RecordingItem> Library::Items() {
    std::lock_guard<std::mutex> lk(mtx_);
    return items_;
}

void Library::Remove(const std::wstring& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const RecordingItem& i) { return i.path == path; }), items_.end());
}

void Library::Worker() {
    for (;;) {
        std::wstring path;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this] { return stop_.load() || !queue_.empty(); });
            if (stop_) return;
            path = queue_.front();
            queue_.erase(queue_.begin());
        }
        // lê as informações sem segurar o lock (o ffmpeg demora alguns ms)
        MediaInfo mi = ProbeMedia(path);
        uint64_t size = 0, mtime = 0;
        WIN32_FILE_ATTRIBUTE_DATA d;
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) {
            size = ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
            mtime = Ft64(d.ftLastWriteTime);
        }
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& it : items_) {
            if (it.path == path) {
                it.info = mi;
                it.infoReady = true;
            }
        }
        bool replaced = false;
        for (auto& [p, c] : cache_) {
            if (_wcsicmp(p.c_str(), path.c_str()) == 0) {
                c = {size, mtime, mi};
                replaced = true;
            }
        }
        if (!replaced) cache_.push_back({path, {size, mtime, mi}});
        cacheDirty_ = true;
        pending_ = std::max(0, pending_.load() - 1);
        if (queue_.empty()) SaveCache();
    }
}
