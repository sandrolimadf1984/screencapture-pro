#pragma once
#include <condition_variable>
#include <deque>

#include "audio.h"
#include "common.h"
#include "ffmpeg.h"
#include "monitors.h"
#include "process.h"
#include "session.h"
#include "settings.h"

// O que gravar
struct CaptureTarget {
    bool fullScreen = true;
    MonitorInfo monitor;          // monitor gravado (na gravação livre: o que contém a área)
    RECT region{};                // área em pixels físicos (coordenadas da área de trabalho virtual)
};

enum class RecState { Idle, Starting, Recording, Pausing, Paused, Resuming, Stopping, Finalizing };

struct RecorderEvent {
    enum class Type { Started, Paused, Resumed, Info, Warning, Error, Finalizing, Progress, Finished, Discarded, AudioNotice };
    Type type = Type::Warning;
    std::string text;
    std::wstring path;            // arquivo final (Finished)
    double duration = 0;
    uint64_t size = 0;
    float progress = 0;
    bool fatal = false;           // Error: a gravação parou
};

struct RecorderStats {
    std::string captureMethod;    // "DXGI (placa de vídeo)" / "GDI (compatibilidade)"
    std::string encoder;          // nome amigável
    int outW = 0, outH = 0, capW = 0, capH = 0, fps = 0;
    double currentFps = 0, speed = 0;
    int64_t dupFrames = 0;
    int segments = 0;
    bool upscaled = false;        // saída maior que a captura (ampliação, sem detalhe extra)
    std::wstring sessionDir;
};

// Motor de gravação. Os métodos públicos são chamados pela interface e não
// bloqueiam: o trabalho pesado (abrir/fechar ffmpeg, montar o vídeo) acontece
// numa thread "controladora". Os avisos voltam pelo callback 'sink' (chamado
// de outra thread — a interface repassa para a thread principal).
class Recorder {
public:
    using Sink = std::function<void(const RecorderEvent&)>;
    explicit Recorder(Sink sink);
    ~Recorder();

    bool Start(const CaptureTarget& target, const Settings& settings, std::string* error);
    void TogglePause();
    void Stop();
    void Discard();

    RecState State() const { return state_.load(); }
    bool IsBusy() const { return state_.load() != RecState::Idle; }
    double Elapsed() const;                // tempo efetivo gravado (sem pausas)
    float FinalizeProgress() const { return finalizeProgress_.load(); }
    RecorderStats Stats() const;
    float TakeSysPeak();
    float TakeMicPeak();
    bool HasSys() const { return sys_ != nullptr; }
    bool HasMic() const { return mic_ != nullptr; }
    // Microfone mudo/ativo durante a gravação (o vídeo continua normalmente)
    void SetMicMuted(bool muted);
    bool MicMuted() const { return mic_ && mic_->Muted(); }

private:
    enum class Cmd { Pause, Resume, Stop, Discard };
    void Controller();
    bool StartSegment(std::string* error);
    bool LaunchSegmentOnce(std::string* error, std::vector<std::string>& tail, bool& timedOut);
    void StopSegment(bool graceful);
    void CheckHealth();
    // Tela bloqueada / janela de permissão do Windows: pausa sozinho e volta depois
    void EnterDesktopWait();
    void CheckDesktopBack();
    void Finalize();
    void Cleanup(bool deleteSession);
    void Post(RecorderEvent::Type t, const std::string& text, bool fatal = false);
    void Push(Cmd c);

    Sink sink_;
    std::atomic<RecState> state_{RecState::Idle};
    std::thread thread_;
    std::mutex cmdMtx_;
    std::condition_variable cmdCv_;
    std::deque<Cmd> cmds_;

    // sessão atual (só a thread controladora mexe depois do Start)
    Settings settings_;
    CaptureTarget target_;
    Session session_;
    SegmentPlan plan_;
    bool ddaForced_ = false;
    std::unique_ptr<AudioCapture> sys_, mic_;
    std::unique_ptr<ChildProcess> proc_;
    bool stoppingSegment_ = false;

    // progresso do trecho atual (escrito pela thread de leitura do ffmpeg)
    mutable std::mutex statMtx_;
    FfProgress prog_;
    int64_t lastProgressQpc_ = 0;
    std::vector<std::string> errTail_;
    std::atomic<int64_t> curFrames_{0};
    double completedSec_ = 0;
    std::atomic<float> finalizeProgress_{0};
    RecorderStats stats_;
    std::wstring defaultOutputDir_;

    // saúde
    int64_t segStartQpc_ = 0;
    int slowReports_ = 0;
    bool perfWarned_ = false;
    bool lowDiskWarned_ = false;
    int64_t lastDiskCheckQpc_ = 0;
    std::vector<int64_t> crashTimes_;

    // pausa automática enquanto a tela do Windows estiver bloqueada
    bool waitingDesktop_ = false;
    int64_t desktopWaitStartQpc_ = 0;
    int64_t desktopOkSinceQpc_ = 0;
};

const char* RecStateText(RecState s);
