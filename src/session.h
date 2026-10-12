#pragma once
#include "common.h"
#include "ffmpeg.h"
#include "settings.h"

// ---------------------------------------------------------------------------
// Uma "sessão" é uma gravação em andamento. Ela vive numa pasta oculta
// (.screencapture_tmp\sessao_...) dentro da pasta de gravações, com:
//   session.json  -> tudo que é preciso para montar o vídeo final
//   seg_000.nut   -> vídeo de cada trecho (entre uma pausa e outra)
//   sys_000.pcm / mic_000.pcm -> áudio bruto de cada trecho (PCM 16 bits, 48 kHz, estéreo)
// Se o programa fechar de repente, na próxima abertura ele encontra a pasta e
// oferece a recuperação. Os caminhos no JSON são relativos (funciona em pendrive).
// ---------------------------------------------------------------------------

struct SegmentRecord {
    int index = 0;
    std::string video;            // nome do arquivo .nut
    std::string sysAudio;         // nome do .pcm (vazio = sem áudio do sistema)
    std::string micAudio;
    int64_t audioStartUs = 0;     // início do áudio, relativo a baseUs
    std::string encoder;          // nome do codificador ffmpeg usado neste trecho
    bool dda = true;
    bool closed = false;
};

struct Session {
    std::wstring dir;             // pasta da sessão (não vai para o JSON)
    int64_t baseUs = 0;
    std::string created;          // texto, só informativo
    std::string outputDir;        // UTF-8, pasta final
    std::string nameBase;         // nome final sem extensão (já resolvido)
    Container container = Container::MP4;
    VideoCodec codec = VideoCodec::H264;
    int fps = 30;
    int outW = 0, outH = 0;
    std::string mode;             // "Tela inteira" / "Gravação livre"
    bool hasSys = false, hasMic = false;
    int sysVolume = 100, micVolume = 100;
    AudioTracks tracks = AudioTracks::Mixed;
    int aacBitrate = 160;
    AviAudio aviAudio = AviAudio::MP3;
    int mp3Bitrate = 192;
    std::string audioFormat = "s16le";   // formato do áudio bruto ("f32le" nas versões de teste antigas)
    std::vector<SegmentRecord> segments;
    bool finalized = false;

    bool Save() const;
    static bool Load(const std::wstring& dir, Session& out, std::string* error);
    std::wstring File(const std::string& name) const { return JoinPath(dir, Widen(name)); }
};

// Pasta oculta de trabalho dentro da pasta de gravações
std::wstring TempRootFor(const std::wstring& outputDir);
// Sessões não finalizadas (para recuperação)
std::vector<std::wstring> FindPendingSessions(const std::wstring& outputDir);
bool DeleteDirectoryTree(const std::wstring& dir);
