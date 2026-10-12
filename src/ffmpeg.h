#pragma once
#include "common.h"
#include "settings.h"

// ---------------------------------------------------------------------------
// Tudo que conversa com o ffmpeg.exe: descobrir codificadores de hardware,
// montar a linha de comando de cada trecho gravado e ler informações de vídeo.
// ---------------------------------------------------------------------------

enum class EncoderKind { X264, X265, NvencH264, NvencHevc, QsvH264, QsvHevc, AmfH264, AmfHevc, Mpeg4, Mjpeg };

struct EncoderCaps {
    bool probed = false;
    bool nvencH264 = false, nvencHevc = false;
    bool qsvH264 = false, qsvHevc = false;
    bool amfH264 = false, amfHevc = false;
    std::string ffmpegVersion;   // "7.1-essentials..." (vazio = ffmpeg não encontrado)
    bool ffmpegOk = false;
};

// Estado global das capacidades (preenchido em segundo plano no início)
EncoderCaps GetEncoderCaps();
void ProbeEncodersBlocking();                 // roda os testes (alguns segundos)

const char* EncoderDisplayName(EncoderKind k);          // "NVIDIA NVENC (H.264)"
const char* EncoderFfmpegName(EncoderKind k);           // "h264_nvenc"
bool EncoderIsHardware(EncoderKind k);
// Escolhe o codificador real a partir do codec + preferência + o que existe na máquina.
EncoderKind ResolveEncoder(VideoCodec codec, EncoderPref pref, const EncoderCaps& caps, std::string* note);
// Próximo codificador para tentar se o atual falhar (hardware -> software)
bool FallbackEncoder(EncoderKind current, EncoderKind& next);

// Plano de UM trecho (segmento) de gravação.
struct SegmentPlan {
    bool useDDA = true;            // true = ddagrab (DXGI), false = gdigrab (GDI)
    int ddaAdapter = 0, ddaOutput = 0;
    int capX = 0, capY = 0;        // DDA: relativo ao monitor; GDI: coordenada da área de trabalho virtual
    int capW = 0, capH = 0;        // sempre pares
    bool fullMonitor = false;      // DDA: captura o monitor inteiro (sem offset)
    int fps = 30;
    bool drawMouse = true;
    int64_t baseUs = 0;            // relógio base da sessão (µs desde 1970)
    int outW = 0, outH = 0;        // tamanho final do vídeo (pares)
    AspectMode aspect = AspectMode::Letterbox;
    EncoderKind encoder = EncoderKind::X264;
    VideoCodec codec = VideoCodec::H264;
    QualityPreset quality = QualityPreset::Recommended;
    int bitrateKbps = 8000;
    bool forAvi = false;           // AVI: sem B-frames (o AVI não guarda a ordem de exibição)
    std::wstring outFile;          // trecho .nut
    double testSeconds = 0;        // > 0 = teste de desempenho (saída descartada)
};

std::wstring BuildSegmentArgs(const SegmentPlan& p);
// Tamanho de saída final para um tamanho de captura e as configurações
void ComputeOutputSize(const Settings& s, int capW, int capH, int& outW, int& outH);

// Informações de um vídeo (lidas pelo próprio ffmpeg)
struct MediaInfo {
    bool ok = false;
    double duration = 0;
    int width = 0, height = 0;
    double fps = 0;
    std::string vcodec, acodec, container;
    int audioTracks = 0;
};
MediaInfo ProbeMedia(const std::wstring& file);

// Lê um trecho .nut: primeiro timestamp (s) e número de quadros
struct SegmentProbe {
    bool ok = false;
    double firstPts = 0;
    double lastPts = 0;           // maior timestamp (com B-frames, um trecho cortado à força pode ter "buracos")
    int64_t frames = 0;
    std::string error;
};
// Lê o trecho inteiro (sem decodificar). Não tem tempo máximo: um trecho de
// muitas horas pode levar alguns minutos; onProgress recebe de 0 a 1.
SegmentProbe ProbeSegment(const std::wstring& nutFile, const std::function<void(double)>& onProgress = nullptr);

// Linha "-progress" do ffmpeg (chave=valor)
struct FfProgress {
    int64_t frame = 0;
    double fps = 0;
    int64_t dupFrames = 0, dropFrames = 0;
    double speed = 0;
    int64_t outTimeUs = 0;
    bool ended = false;
};
// Atualiza 'p' com uma linha; devolve true quando um bloco completo chegou
bool ParseProgressLine(const std::string& line, FfProgress& p);

// Mensagem amigável a partir das últimas linhas de erro do ffmpeg
std::string ExplainFfmpegError(const std::vector<std::string>& stderrTail, bool* ddaProblem, bool* encoderProblem,
                               bool* diskFull, bool* permission);
