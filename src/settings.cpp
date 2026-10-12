#include "settings.h"

#include <algorithm>

#include "log.h"
#include "paths.h"
#include "../third_party/nlohmann/json.hpp"

using json = nlohmann::json;

void Settings::Sanitize() {
    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    if (fps != 15 && fps != 24 && fps != 30 && fps != 60) fps = 30;
    customW = clampi(customW, 64, 7680) & ~1;     // codecs exigem números pares
    customH = clampi(customH, 64, 4320) & ~1;
    customBitrateKbps = clampi(customBitrateKbps, 300, 200000);
    sysVolume = clampi(sysVolume, 0, 300);
    micVolume = clampi(micVolume, 0, 300);
    if (aacBitrate != 96 && aacBitrate != 128 && aacBitrate != 160 && aacBitrate != 192 && aacBitrate != 256 && aacBitrate != 320) aacBitrate = 160;
    if (mp3Bitrate != 128 && mp3Bitrate != 160 && mp3Bitrate != 192 && mp3Bitrate != 256 && mp3Bitrate != 320) mp3Bitrate = 192;
    if (countdown != 0 && countdown != 3 && countdown != 5 && countdown != 10) countdown = 3;
    // codec compatível com o contêiner (não basta trocar a extensão!)
    if (codecMp4 != VideoCodec::H264 && codecMp4 != VideoCodec::HEVC) codecMp4 = VideoCodec::H264;
    if (codecAvi != VideoCodec::MPEG4 && codecAvi != VideoCodec::MJPEG && codecAvi != VideoCodec::H264) codecAvi = VideoCodec::MPEG4;
    if (Trim(namePattern).empty()) namePattern = APP_FILE_PREFIX "_{data}_{hora}";
}

const char* ContainerName(Container c) { return c == Container::MP4 ? "MP4" : "AVI"; }

const char* CodecName(VideoCodec c) {
    switch (c) {
        case VideoCodec::H264: return "H.264";
        case VideoCodec::HEVC: return "H.265/HEVC";
        case VideoCodec::MPEG4: return "MPEG-4 (Xvid)";
        case VideoCodec::MJPEG: return "MJPEG";
    }
    return "?";
}

const char* CodecLongName(VideoCodec c) {
    switch (c) {
        case VideoCodec::H264: return "H.264 / AVC (recomendado)";
        case VideoCodec::HEVC: return "H.265 / HEVC (arquivos menores)";
        case VideoCodec::MPEG4: return "MPEG-4 Parte 2 (compatível com Xvid)";
        case VideoCodec::MJPEG: return "Motion JPEG (máxima compatibilidade)";
    }
    return "?";
}

const char* QualityName(QualityPreset q) {
    switch (q) {
        case QualityPreset::Recommended: return "Recomendada";
        case QualityPreset::Small: return "Arquivo menor";
        case QualityPreset::High: return "Qualidade superior";
        case QualityPreset::CustomBitrate: return "Taxa de bits fixa";
    }
    return "?";
}

const char* ResolutionName(ResolutionPreset r) {
    switch (r) {
        case ResolutionPreset::Original: return "Original (resolução da captura)";
        case ResolutionPreset::P360: return "360p — 640 × 360";
        case ResolutionPreset::P480: return "480p — 854 × 480";
        case ResolutionPreset::P720: return "720p HD — 1280 × 720";
        case ResolutionPreset::P1080: return "1080p Full HD — 1920 × 1080";
        case ResolutionPreset::Custom2060: return "2060p — personalizada";
        case ResolutionPreset::UHD4K: return "4K UHD — 3840 × 2160";
    }
    return "?";
}

bool ResolutionSize(const Settings& s, int& w, int& h) {
    switch (s.resolution) {
        case ResolutionPreset::Original: return false;
        case ResolutionPreset::P360: w = 640; h = 360; return true;
        case ResolutionPreset::P480: w = 854; h = 480; return true;
        case ResolutionPreset::P720: w = 1280; h = 720; return true;
        case ResolutionPreset::P1080: w = 1920; h = 1080; return true;
        case ResolutionPreset::Custom2060: w = s.customW; h = s.customH; return true;
        case ResolutionPreset::UHD4K: w = 3840; h = 2160; return true;
    }
    return false;
}

std::string ResolutionLabel(const Settings& s) {
    switch (s.resolution) {
        case ResolutionPreset::Original: return "Original";
        case ResolutionPreset::P360: return "360p";
        case ResolutionPreset::P480: return "480p";
        case ResolutionPreset::P720: return "720p";
        case ResolutionPreset::P1080: return "1080p";
        case ResolutionPreset::Custom2060: return Fmt("%d×%d", s.customW, s.customH);
        case ResolutionPreset::UHD4K: return "4K";
    }
    return "?";
}

const char* AfterFinishName(AfterFinish a) {
    switch (a) {
        case AfterFinish::Notify: return "Só mostrar uma notificação";
        case AfterFinish::OpenFolder: return "Abrir a pasta da gravação";
        case AfterFinish::OpenVideo: return "Abrir o vídeo no player";
        case AfterFinish::ShowLibrary: return "Mostrar a lista de gravações";
        case AfterFinish::Nothing: return "Não fazer nada";
    }
    return "?";
}

std::wstring ContainerExtension(Container c) { return c == Container::MP4 ? L".mp4" : L".avi"; }

std::string HotkeyText(const Hotkey& h) {
    if (!h.enabled || !h.vk) return "Desativado";
    std::string s;
    if (h.mods & MOD_CONTROL) s += "Ctrl + ";
    if (h.mods & MOD_ALT) s += "Alt + ";
    if (h.mods & MOD_SHIFT) s += "Shift + ";
    if (h.mods & MOD_WIN) s += "Win + ";
    UINT vk = h.vk;
    if (vk >= VK_F1 && vk <= VK_F24) return s + Fmt("F%u", vk - VK_F1 + 1);
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return s + std::string(1, (char)vk);
    switch (vk) {
        case VK_PAUSE: return s + "Pause";
        case VK_SCROLL: return s + "Scroll Lock";
        case VK_INSERT: return s + "Insert";
        case VK_HOME: return s + "Home";
        case VK_END: return s + "End";
        case VK_PRIOR: return s + "Page Up";
        case VK_NEXT: return s + "Page Down";
        case VK_SPACE: return s + "Espaço";
        case VK_SNAPSHOT: return s + "Print Screen";
    }
    // nome que o próprio Windows dá para a tecla
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    wchar_t name[64] = {0};
    if (GetKeyNameTextW((LONG)(sc << 16), name, 63) > 0) return s + Narrow(name);
    return s + Fmt("Tecla %u", vk);
}

std::wstring SettingsFilePath() { return JoinPath(Paths().configDir, L"settings.json"); }

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------
static json HotkeyToJson(const Hotkey& h) { return json{{"enabled", h.enabled}, {"mods", h.mods}, {"vk", h.vk}}; }
static void HotkeyFromJson(const json& j, Hotkey& h) {
    if (!j.is_object()) return;
    h.enabled = j.value("enabled", h.enabled);
    h.mods = j.value("mods", h.mods);
    h.vk = j.value("vk", h.vk);
}

bool SaveSettings(const Settings& s, std::string* error) {
    json j;
    j["version"] = 1;
    j["video"] = {
        {"container", (int)s.container}, {"codecMp4", (int)s.codecMp4}, {"codecAvi", (int)s.codecAvi},
        {"encoder", (int)s.encoder}, {"quality", (int)s.quality}, {"customBitrateKbps", s.customBitrateKbps},
        {"resolution", (int)s.resolution}, {"customW", s.customW}, {"customH", s.customH},
        {"aspect", (int)s.aspect}, {"fps", s.fps}, {"drawMouse", s.drawMouse}, {"captureMethod", (int)s.captureMethod}};
    j["audio"] = {
        {"system", s.sysAudio}, {"systemDeviceId", s.sysDeviceId}, {"systemDeviceName", s.sysDeviceName}, {"systemVolume", s.sysVolume},
        {"mic", s.mic}, {"micDeviceId", s.micDeviceId}, {"micDeviceName", s.micDeviceName}, {"micVolume", s.micVolume},
        {"tracks", (int)s.tracks}, {"aacBitrate", s.aacBitrate}, {"aviAudio", (int)s.aviAudio}, {"mp3Bitrate", s.mp3Bitrate}};
    j["recordings"] = {
        {"outputDir", s.outputDir}, {"namePattern", s.namePattern}, {"afterFinish", (int)s.afterFinish},
        {"countdown", s.countdown}, {"minimizeOnRecord", s.minimizeOnRecord}, {"showRegionBorder", s.showRegionBorder},
        {"lastMonitorDevice", s.lastMonitorDevice}};
    j["hotkeys"] = {{"start", HotkeyToJson(s.hkStart)}, {"pause", HotkeyToJson(s.hkPause)}, {"stop", HotkeyToJson(s.hkStop)},
                    {"mic", HotkeyToJson(s.hkMic)}, {"startAction", (int)s.startAction}};
    std::string text;
    try {
        text = j.dump(2);
    } catch (const std::exception& e) {
        if (error) *error = std::string("Erro ao gerar o JSON: ") + e.what();
        return false;
    }
    EnsureDir(Paths().configDir);
    if (!WriteFileUtf8Atomic(SettingsFilePath(), text)) {
        if (error) *error = "Não foi possível gravar " + Narrow(SettingsFilePath()) + ": " + LastErrorText();
        Log::Error("Falha ao salvar configurações: %s", LastErrorText().c_str());
        return false;
    }
    return true;
}

bool LoadSettings(Settings& s, std::string* warning) {
    s = Settings();
    std::string text;
    if (!ReadFileUtf8(SettingsFilePath(), text)) {
        Log::Info("Sem settings.json ainda — usando valores padrão.");
        s.Sanitize();
        return false;
    }
    try {
        json j = json::parse(text);
        if (j.contains("video")) {
            const json& v = j["video"];
            s.container = (Container)v.value("container", (int)s.container);
            s.codecMp4 = (VideoCodec)v.value("codecMp4", (int)s.codecMp4);
            s.codecAvi = (VideoCodec)v.value("codecAvi", (int)s.codecAvi);
            s.encoder = (EncoderPref)std::clamp(v.value("encoder", (int)s.encoder), 0, 4);
            s.quality = (QualityPreset)std::clamp(v.value("quality", (int)s.quality), 0, 3);
            s.customBitrateKbps = v.value("customBitrateKbps", s.customBitrateKbps);
            s.resolution = (ResolutionPreset)std::clamp(v.value("resolution", (int)s.resolution), 0, 6);
            s.customW = v.value("customW", s.customW);
            s.customH = v.value("customH", s.customH);
            s.aspect = (AspectMode)std::clamp(v.value("aspect", (int)s.aspect), 0, 2);
            s.fps = v.value("fps", s.fps);
            s.drawMouse = v.value("drawMouse", s.drawMouse);
            s.captureMethod = (CaptureMethod)std::clamp(v.value("captureMethod", (int)s.captureMethod), 0, 2);
            if ((int)s.container < 0 || (int)s.container > 1) s.container = Container::MP4;
        }
        if (j.contains("audio")) {
            const json& a = j["audio"];
            s.sysAudio = a.value("system", s.sysAudio);
            s.sysDeviceId = a.value("systemDeviceId", s.sysDeviceId);
            s.sysDeviceName = a.value("systemDeviceName", s.sysDeviceName);
            s.sysVolume = a.value("systemVolume", s.sysVolume);
            s.mic = a.value("mic", s.mic);
            s.micDeviceId = a.value("micDeviceId", s.micDeviceId);
            s.micDeviceName = a.value("micDeviceName", s.micDeviceName);
            s.micVolume = a.value("micVolume", s.micVolume);
            s.tracks = (AudioTracks)std::clamp(a.value("tracks", (int)s.tracks), 0, 1);
            s.aacBitrate = a.value("aacBitrate", s.aacBitrate);
            s.aviAudio = (AviAudio)std::clamp(a.value("aviAudio", (int)s.aviAudio), 0, 1);
            s.mp3Bitrate = a.value("mp3Bitrate", s.mp3Bitrate);
        }
        if (j.contains("recordings")) {
            const json& r = j["recordings"];
            s.outputDir = r.value("outputDir", s.outputDir);
            s.namePattern = r.value("namePattern", s.namePattern);
            s.afterFinish = (AfterFinish)std::clamp(r.value("afterFinish", (int)s.afterFinish), 0, 4);
            s.countdown = r.value("countdown", s.countdown);
            s.minimizeOnRecord = r.value("minimizeOnRecord", s.minimizeOnRecord);
            s.showRegionBorder = r.value("showRegionBorder", s.showRegionBorder);
            s.lastMonitorDevice = r.value("lastMonitorDevice", s.lastMonitorDevice);
        }
        if (j.contains("hotkeys")) {
            const json& h = j["hotkeys"];
            if (h.contains("start")) HotkeyFromJson(h["start"], s.hkStart);
            if (h.contains("pause")) HotkeyFromJson(h["pause"], s.hkPause);
            if (h.contains("stop")) HotkeyFromJson(h["stop"], s.hkStop);
            if (h.contains("mic")) HotkeyFromJson(h["mic"], s.hkMic);
            s.startAction = (StartAction)std::clamp(h.value("startAction", (int)s.startAction), 0, 2);
        }
    } catch (const std::exception& e) {
        // Arquivo corrompido: guardo uma cópia para análise e sigo com o padrão.
        std::wstring bad = SettingsFilePath() + L".corrompido";
        CopyFileW(SettingsFilePath().c_str(), bad.c_str(), FALSE);
        Log::Error("settings.json inválido (%s). Copiado para %s; usando padrões.", e.what(), Narrow(bad).c_str());
        if (warning) *warning = "O arquivo de configurações estava danificado e foi substituído pelos valores padrão. "
                                "Uma cópia foi guardada como settings.json.corrompido.";
        s = Settings();
    }
    s.Sanitize();
    return true;
}
