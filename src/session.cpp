#include "session.h"

#include "log.h"
#include "../third_party/nlohmann/json.hpp"

using json = nlohmann::json;

std::wstring TempRootFor(const std::wstring& outputDir) { return JoinPath(outputDir, L".screencapture_tmp"); }

bool Session::Save() const {
    json j;
    j["version"] = 1;
    j["app"] = APP_NAME_U8;
    j["base_us"] = baseUs;
    j["created"] = created;
    j["output_dir"] = outputDir;
    j["name_base"] = nameBase;
    j["container"] = (int)container;
    j["codec"] = (int)codec;
    j["fps"] = fps;
    j["out_w"] = outW;
    j["out_h"] = outH;
    j["mode"] = mode;
    j["audio"] = {{"sys", hasSys}, {"mic", hasMic}, {"sys_volume", sysVolume}, {"mic_volume", micVolume},
                  {"tracks", (int)tracks}, {"aac_bitrate", aacBitrate}, {"avi_audio", (int)aviAudio}, {"mp3_bitrate", mp3Bitrate}};
    json segs = json::array();
    for (const auto& s : segments)
        segs.push_back({{"index", s.index}, {"video", s.video}, {"sys", s.sysAudio}, {"mic", s.micAudio},
                        {"audio_start_us", s.audioStartUs}, {"encoder", s.encoder}, {"dda", s.dda}, {"closed", s.closed}});
    j["segments"] = segs;
    j["audio_format"] = audioFormat;
    j["finalized"] = finalized;
    bool ok = WriteFileUtf8Atomic(JoinPath(dir, L"session.json"), j.dump(2));
    if (!ok) Log::Error("Não foi possível gravar session.json: %s", LastErrorText().c_str());
    return ok;
}

bool Session::Load(const std::wstring& dir, Session& s, std::string* error) {
    std::string text;
    if (!ReadFileUtf8(JoinPath(dir, L"session.json"), text)) {
        if (error) *error = "session.json não encontrado";
        return false;
    }
    try {
        json j = json::parse(text);
        s = Session();
        s.dir = dir;
        s.baseUs = j.value("base_us", (int64_t)0);
        s.created = j.value("created", "");
        s.outputDir = j.value("output_dir", "");
        s.nameBase = j.value("name_base", "Gravacao_recuperada");
        s.container = (Container)j.value("container", 0);
        s.codec = (VideoCodec)j.value("codec", 0);
        s.fps = j.value("fps", 30);
        s.outW = j.value("out_w", 0);
        s.outH = j.value("out_h", 0);
        s.mode = j.value("mode", "");
        if (j.contains("audio")) {
            const json& a = j["audio"];
            s.hasSys = a.value("sys", false);
            s.hasMic = a.value("mic", false);
            s.sysVolume = a.value("sys_volume", 100);
            s.micVolume = a.value("mic_volume", 100);
            s.tracks = (AudioTracks)a.value("tracks", 0);
            s.aacBitrate = a.value("aac_bitrate", 160);
            s.aviAudio = (AviAudio)a.value("avi_audio", 0);
            s.mp3Bitrate = a.value("mp3_bitrate", 192);
        }
        if (j.contains("segments")) {
            for (const auto& x : j["segments"]) {
                SegmentRecord r;
                r.index = x.value("index", 0);
                r.video = x.value("video", "");
                r.sysAudio = x.value("sys", "");
                r.micAudio = x.value("mic", "");
                r.audioStartUs = x.value("audio_start_us", (int64_t)0);
                r.encoder = x.value("encoder", "");
                r.dda = x.value("dda", true);
                r.closed = x.value("closed", false);
                s.segments.push_back(r);
            }
        }
        s.audioFormat = j.value("audio_format", "f32le");
        if (s.audioFormat != "s16le" && s.audioFormat != "f32le") s.audioFormat = "s16le";
        s.finalized = j.value("finalized", false);
        if (s.fps <= 0) s.fps = 30;
    } catch (const std::exception& e) {
        if (error) *error = std::string("session.json inválido: ") + e.what();
        return false;
    }
    return true;
}

static bool HasSegmentFiles(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(JoinPath(dir, L"seg_*.nut").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return false;
    FindClose(f);
    return true;
}

std::vector<std::wstring> FindPendingSessions(const std::wstring& outputDir) {
    std::vector<std::wstring> out, garbage;
    std::wstring root = TempRootFor(outputDir);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(JoinPath(root, L"sessao_*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        std::wstring dir = JoinPath(root, fd.cFileName);
        Session s;
        bool loaded = Session::Load(dir, s, nullptr);
        if (loaded && !s.finalized && !s.segments.empty() && HasSegmentFiles(dir)) {
            out.push_back(dir);
        } else if ((loaded && s.finalized) || !HasSegmentFiles(dir)) {
            // Sobras sem nada para recuperar: o vídeo já foi salvo (o programa
            // fechou no meio da limpeza) ou não chegou a gravar nenhum quadro.
            garbage.push_back(dir);
        }
        // sem session.json mas com trechos de vídeo: fica lá (não apago vídeo do usuário às cegas)
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    for (auto& d : garbage) {
        Log::Info("Removendo pasta temporária sem nada a recuperar: %s", Narrow(d).c_str());
        DeleteDirectoryTree(d);
    }
    RemoveDirectoryW(root.c_str());   // só some se estiver vazia
    return out;
}

bool DeleteDirectoryTree(const std::wstring& dir) {
    if (dir.size() < 4) return false;   // proteção contra apagar raiz por engano
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(JoinPath(dir, L"*").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            std::wstring p = JoinPath(dir, n);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) DeleteDirectoryTree(p);
            else {
                SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(p.c_str());
            }
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    return RemoveDirectoryW(dir.c_str()) != 0;
}
