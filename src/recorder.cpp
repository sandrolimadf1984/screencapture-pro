// ---------------------------------------------------------------------------
// Motor de gravação.
//
// COMO A SINCRONIA FUNCIONA (a parte mais importante do programa):
//  * Cada trecho (do início até uma pausa) é um ffmpeg separado gravando um
//    arquivo .nut. Os timestamps do vídeo são a HORA DO SISTEMA em que cada
//    quadro foi capturado (menos um "base" fixo da sessão):
//      - gdigrab já marca os quadros com av_gettime() (relógio do sistema);
//      - no ddagrab o 1º quadro recebe RTCTIME (relógio do sistema) e os demais
//        seguem os intervalos reais da captura (expressão setpts em ffmpeg.cpp).
//  * O áudio é gravado por nós (WASAPI) também amarrado ao relógio do sistema:
//    a amostra n do arquivo = início do trecho + n/48000 s.
//  * No final, para cada trecho, lemos o horário do 1º quadro e o número de
//    quadros, e copiamos do áudio exatamente o mesmo intervalo. Junta tudo e
//    pronto: áudio e vídeo batem, mesmo com várias pausas.
// ---------------------------------------------------------------------------
#include "recorder.h"

#include <algorithm>
#include <cmath>

#include "finalize.h"
#include "log.h"
#include "paths.h"
#include "win_util.h"

// Erro "especial" de StartSegment: a tela do Windows está bloqueada (ou com uma
// janela de permissão do UAC). Não é falha: a gravação espera a tela voltar.
static const char* kDesktopUnavailable = "tela do Windows indisponível";

// Mantém o PC acordado e a tela ligada enquanto a sessão de gravação existir
// (gravando, pausado ou montando o vídeo). Sem isso, numa gravação longa sem
// mexer no mouse ou no teclado, o Windows suspenderia o PC e a gravação acabaria.
// O pedido vale para a thread que o fez: some sozinho quando ela termina.
struct KeepSystemAwake {
    KeepSystemAwake() {
        if (SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED))
            Log::Info("Suspensão automática e desligamento da tela bloqueados durante a gravação.");
    }
    ~KeepSystemAwake() { SetThreadExecutionState(ES_CONTINUOUS); }
};

const char* RecStateText(RecState s) {
    switch (s) {
        case RecState::Idle: return "Pronto para gravar";
        case RecState::Starting: return "Iniciando a gravação…";
        case RecState::Recording: return "Gravando";
        case RecState::Pausing: return "Pausando…";
        case RecState::Paused: return "Pausado";
        case RecState::Resuming: return "Retomando…";
        case RecState::Stopping: return "Finalizando a captura…";
        case RecState::Finalizing: return "Processando o vídeo…";
    }
    return "";
}

Recorder::Recorder(Sink sink) : sink_(std::move(sink)) {}

Recorder::~Recorder() {
    if (thread_.joinable()) {
        Push(Cmd::Stop);
        thread_.join();
    }
}

void Recorder::Post(RecorderEvent::Type t, const std::string& text, bool fatal) {
    RecorderEvent e;
    e.type = t;
    e.text = text;
    e.fatal = fatal;
    if (t == RecorderEvent::Type::Warning) Log::Warn("%s", text.c_str());
    if (t == RecorderEvent::Type::Error) Log::Error("%s", text.c_str());
    if (sink_) sink_(e);
}

void Recorder::Push(Cmd c) {
    {
        std::lock_guard<std::mutex> lk(cmdMtx_);
        cmds_.push_back(c);
    }
    cmdCv_.notify_all();
}

void Recorder::TogglePause() {
    RecState s = state_.load();
    if (s == RecState::Recording) Push(Cmd::Pause);
    else if (s == RecState::Paused) Push(Cmd::Resume);
}

void Recorder::Stop() {
    RecState s = state_.load();
    if (s == RecState::Recording || s == RecState::Paused || s == RecState::Starting || s == RecState::Resuming || s == RecState::Pausing)
        Push(Cmd::Stop);
}

void Recorder::Discard() {
    if (state_.load() != RecState::Idle && state_.load() != RecState::Finalizing) Push(Cmd::Discard);
}

float Recorder::TakeSysPeak() { return sys_ ? sys_->TakePeak() : 0.0f; }

void Recorder::SetMicMuted(bool muted) {
    if (!mic_ || mic_->Muted() == muted) return;
    RecState s = state_.load();
    if (s == RecState::Idle || s == RecState::Finalizing) return;
    mic_->SetMuted(muted);
    Log::Info("Microfone %s (tempo gravado: %s).", muted ? "silenciado" : "reativado", FormatDuration(Elapsed(), true).c_str());
}
float Recorder::TakeMicPeak() { return mic_ ? mic_->TakePeak() : 0.0f; }

double Recorder::Elapsed() const {
    std::lock_guard<std::mutex> lk(statMtx_);
    double t = completedSec_;
    if (state_.load() == RecState::Recording && settings_.fps > 0 && prog_.frame > 0) {
        double since = (double)(QpcNowUs() - lastProgressQpc_) / 1e6;
        t += (double)prog_.frame / settings_.fps + std::clamp(since, 0.0, 0.6);
    }
    return t;
}

RecorderStats Recorder::Stats() const {
    std::lock_guard<std::mutex> lk(statMtx_);
    RecorderStats s = stats_;
    s.currentFps = prog_.fps;
    s.speed = prog_.speed;
    s.dupFrames = prog_.dupFrames;
    return s;
}

// ---------------------------------------------------------------------------
// Início
// ---------------------------------------------------------------------------
bool Recorder::Start(const CaptureTarget& target, const Settings& st, std::string* error) {
    if (state_.load() != RecState::Idle) {
        if (error) *error = "Já existe uma gravação em andamento.";
        return false;
    }
    if (thread_.joinable()) thread_.join();   // sessão anterior já terminou

    EncoderCaps caps = GetEncoderCaps();
    if (!FileExists(Paths().ffmpegExe)) {
        if (error) *error = "O componente de vídeo (ffmpeg\\ffmpeg.exe) não foi encontrado na pasta do programa. "
                            "Extraia o pacote completo novamente.";
        return false;
    }
    settings_ = st;
    target_ = target;

    // ---- pasta de saída
    defaultOutputDir_ = Paths().defaultRecordingsDir;
    std::wstring outDir = st.outputDir.empty() ? defaultOutputDir_ : Widen(st.outputDir);
    if (!EnsureDir(outDir) || !IsDirWritable(outDir)) {
        if (error) *error = "A pasta de gravações não está disponível ou não permite gravação:\n" + Narrow(outDir) +
                            "\n\nEscolha outra pasta em Configurações > Gravações.";
        return false;
    }
    uint64_t freeB = 0, totalB = 0;
    if (GetDiskSpace(outDir, freeB, totalB) && freeB < 500ull * 1024 * 1024) {
        if (error) *error = "Há menos de 500 MB livres no disco de destino (" + FormatBytes(freeB) +
                            "). Libere espaço ou escolha outra pasta antes de gravar.";
        return false;
    }

    // ---- área de captura (sempre com largura/altura pares — exigência dos codecs)
    RECT r = target.fullScreen ? target.monitor.rect : target.region;
    int w = (r.right - r.left) & ~1, h = (r.bottom - r.top) & ~1;
    if (w < 64 || h < 64) {
        if (error) *error = "A área selecionada é pequena demais (mínimo 64 × 64 pixels).";
        return false;
    }
    plan_ = SegmentPlan();
    plan_.fps = st.fps;
    plan_.drawMouse = st.drawMouse;
    plan_.capW = w;
    plan_.capH = h;
    const MonitorInfo& mon = target.monitor;
    bool insideMonitor = r.left >= mon.rect.left && r.top >= mon.rect.top && r.left + w <= mon.rect.right && r.top + h <= mon.rect.bottom;
    bool ddaPossible = mon.dxgiAdapter >= 0 && !mon.rotated && insideMonitor;
    plan_.useDDA = st.captureMethod != CaptureMethod::GDI && ddaPossible;
    ddaForced_ = st.captureMethod == CaptureMethod::DDA;
    if (plan_.useDDA) {
        plan_.ddaAdapter = mon.dxgiAdapter;
        plan_.ddaOutput = mon.dxgiOutput;
        plan_.capX = r.left - mon.rect.left;
        plan_.capY = r.top - mon.rect.top;
        plan_.fullMonitor = target.fullScreen && w == mon.Width() && h == mon.Height();
    } else {
        plan_.capX = r.left;
        plan_.capY = r.top;
    }
    ComputeOutputSize(st, w, h, plan_.outW, plan_.outH);
    plan_.aspect = st.aspect;
    plan_.codec = st.CurrentCodec();
    std::string note;
    plan_.encoder = ResolveEncoder(plan_.codec, st.encoder, caps, &note);
    plan_.quality = st.quality;
    plan_.bitrateKbps = st.customBitrateKbps;
    plan_.forAvi = st.container == Container::AVI;

    // ---- sessão
    session_ = Session();
    session_.baseUs = WallClockUs() - 2000000;   // 2 s antes de agora: os timestamps ficam sempre positivos
    session_.created = NowStamp("%Y-%m-%d %H:%M:%S");
    session_.outputDir = Narrow(outDir);
    session_.mode = target.fullScreen ? "TelaInteira" : "Area";
    session_.nameBase = ExpandNamePattern(st.namePattern, session_.mode, mon.number);
    session_.container = st.container;
    session_.codec = plan_.codec;
    session_.fps = st.fps;
    session_.outW = plan_.outW;
    session_.outH = plan_.outH;
    session_.sysVolume = st.sysVolume;
    session_.micVolume = st.micVolume;
    session_.tracks = st.tracks;
    session_.aacBitrate = st.aacBitrate;
    session_.aviAudio = st.aviAudio;
    session_.mp3Bitrate = st.mp3Bitrate;
    session_.audioFormat = "s16le";   // o que a AudioCapture grava (ver kTempBytesPerFrame)
    std::wstring tmpRoot = TempRootFor(outDir);
    if (!EnsureDir(tmpRoot)) {
        if (error) *error = "Não foi possível criar a pasta temporária em " + Narrow(tmpRoot);
        return false;
    }
    SetFileAttributesW(tmpRoot.c_str(), FILE_ATTRIBUTE_HIDDEN);
    session_.dir = JoinPath(tmpRoot, Widen("sessao_" + NowStamp("%Y%m%d_%H%M%S")));
    for (int i = 2; DirExists(session_.dir); i++) session_.dir = JoinPath(tmpRoot, Widen("sessao_" + NowStamp("%Y%m%d_%H%M%S") + Fmt("_%d", i)));
    if (!EnsureDir(session_.dir)) {
        if (error) *error = "Não foi possível criar a pasta da sessão: " + LastErrorText();
        return false;
    }

    // ---- áudio
    sys_.reset();
    mic_.reset();
    auto forward = [this](const std::string& msg) { Post(RecorderEvent::Type::AudioNotice, msg); };
    char envBuf[8] = {0};
    bool testTone = GetEnvironmentVariableA("SCP_TEST_TONE", envBuf, sizeof(envBuf)) > 0 && envBuf[0] == '1';
    if (st.sysAudio || testTone) {
        sys_ = std::make_unique<AudioCapture>(testTone ? AudioKind::TestTone : AudioKind::SystemLoopback, st.sysDeviceId);
        sys_->onEvent = forward;
        sys_->Start();
    }
    if (st.mic) {
        mic_ = std::make_unique<AudioCapture>(AudioKind::Microphone, st.micDeviceId);
        mic_->onEvent = forward;
        mic_->Start();
    }
    session_.hasSys = sys_ != nullptr;
    session_.hasMic = mic_ != nullptr;
    session_.Save();

    {
        std::lock_guard<std::mutex> lk(statMtx_);
        prog_ = FfProgress();
        completedSec_ = 0;
        stats_ = RecorderStats();
        stats_.capW = w;
        stats_.capH = h;
        stats_.outW = plan_.outW;
        stats_.outH = plan_.outH;
        stats_.fps = st.fps;
        stats_.upscaled = (int64_t)plan_.outW * plan_.outH > (int64_t)w * h;
        stats_.sessionDir = session_.dir;
    }
    finalizeProgress_ = 0;
    perfWarned_ = lowDiskWarned_ = false;
    crashTimes_.clear();
    waitingDesktop_ = false;
    desktopWaitStartQpc_ = desktopOkSinceQpc_ = 0;
    {
        std::lock_guard<std::mutex> lk(cmdMtx_);
        cmds_.clear();
    }
    Log::Info("=== Nova gravação: %s | captura %dx%d em (%d,%d) %s | saída %dx%d @ %d fps | %s | %s ===",
              target.fullScreen ? "tela inteira" : "gravação livre", w, h, (int)r.left, (int)r.top,
              plan_.useDDA ? "DXGI" : "GDI", plan_.outW, plan_.outH, st.fps, EncoderDisplayName(plan_.encoder), ContainerName(st.container));
    if (!note.empty()) Post(RecorderEvent::Type::Warning, note);
    if (!ddaPossible && st.captureMethod == CaptureMethod::DDA)
        Post(RecorderEvent::Type::Warning, "A captura DXGI não está disponível para esta área/monitor; usando GDI (compatibilidade).");
    state_ = RecState::Starting;
    thread_ = std::thread([this] { Controller(); });
    return true;
}

// ---------------------------------------------------------------------------
// Trechos
// ---------------------------------------------------------------------------
bool Recorder::LaunchSegmentOnce(std::string* error, std::vector<std::string>& tail, bool& timedOut) {
    timedOut = false;
    int idx = (int)session_.segments.size();
    SegmentRecord rec;
    rec.index = idx;
    rec.video = Fmt("seg_%03d.nut", idx);
    if (sys_) rec.sysAudio = Fmt("sys_%03d.pcm", idx);
    if (mic_) rec.micAudio = Fmt("mic_%03d.pcm", idx);
    rec.encoder = EncoderFfmpegName(plan_.encoder);
    rec.dda = plan_.useDDA;

    // o áudio começa a ser gravado AGORA; o vídeo começa quando o ffmpeg entregar
    // o 1º quadro — a diferença é corrigida na finalização (alinhamento por relógio)
    int64_t segStart = WallClockUs();
    rec.audioStartUs = segStart - session_.baseUs;
    std::string aerr;
    if (sys_ && !sys_->BeginSegment(session_.File(rec.sysAudio), segStart, &aerr)) Log::Error("%s", aerr.c_str());
    if (mic_ && !mic_->BeginSegment(session_.File(rec.micAudio), segStart, &aerr)) Log::Error("%s", aerr.c_str());

    plan_.baseUs = session_.baseUs;
    plan_.outFile = session_.File(rec.video);
    session_.segments.push_back(rec);
    session_.Save();

    std::wstring args = BuildSegmentArgs(plan_);
    Log::Info("Trecho %d: ffmpeg %s", idx, Narrow(args).c_str());
    {
        std::lock_guard<std::mutex> lk(statMtx_);
        prog_ = FfProgress();
        errTail_.clear();
        lastProgressQpc_ = QpcNowUs();
        stats_.captureMethod = plan_.useDDA ? "DXGI (placa de vídeo)" : "GDI (compatibilidade)";
        stats_.encoder = EncoderDisplayName(plan_.encoder);
        stats_.segments = idx + 1;
    }
    proc_ = std::make_unique<ChildProcess>();
    proc_->OnStdout([this](const std::string& line) {
        std::lock_guard<std::mutex> lk(statMtx_);
        if (ParseProgressLine(line, prog_)) {
            lastProgressQpc_ = QpcNowUs();
            curFrames_ = prog_.frame;
        }
    });
    proc_->OnStderr([this](const std::string& line) {
        Log::Raw(line);
        std::lock_guard<std::mutex> lk(statMtx_);
        errTail_.push_back(line);
        if (errTail_.size() > 40) errTail_.erase(errTail_.begin());
    });
    std::string err;
    if (!proc_->Start(Paths().ffmpegExe, args, session_.dir, &err)) {
        if (error) *error = err;
        return false;
    }
    segStartQpc_ = QpcNowUs();
    // espera o primeiro quadro (o ffmpeg informa a contagem pelo -progress a cada 0,5 s)
    for (int i = 0; i < 120; i++) {   // até 12 s (codificadores de GPU às vezes demoram a iniciar)
        if (curFrames_.load() >= 1) return true;
        if (proc_->Wait(100)) break;
        {
            std::lock_guard<std::mutex> lk(cmdMtx_);
            if (!cmds_.empty() && (cmds_.front() == Cmd::Discard || cmds_.front() == Cmd::Stop)) break;
        }
    }
    if (curFrames_.load() >= 1 && proc_->Running()) return true;
    if (proc_->Running()) {
        timedOut = true;
        proc_->Kill();
    }
    proc_->JoinReaders();
    {
        std::lock_guard<std::mutex> lk(statMtx_);
        tail = errTail_;
    }
    if (error) *error = timedOut ? "O ffmpeg não entregou nenhum quadro (tempo esgotado)." : Fmt("O ffmpeg saiu com código %lu.", (unsigned long)proc_->ExitCode());
    // desfaz o trecho que falhou
    if (sys_) sys_->EndSegment();
    if (mic_) mic_->EndSegment();
    DeleteFileW(session_.File(rec.video).c_str());
    if (sys_) DeleteFileW(session_.File(rec.sysAudio).c_str());
    if (mic_) DeleteFileW(session_.File(rec.micAudio).c_str());
    session_.segments.pop_back();
    session_.Save();
    proc_.reset();
    return false;
}

bool Recorder::StartSegment(std::string* error) {
    curFrames_ = 0;
    for (int attempt = 0; attempt < 4; attempt++) {
        // tela bloqueada/UAC: não adianta tentar (nem trocar para GDI) — quem chamou espera
        if (!IsInputDesktopAvailable()) {
            if (error) *error = kDesktopUnavailable;
            return false;
        }
        std::vector<std::string> tail;
        bool timedOut = false;
        std::string err;
        if (LaunchSegmentOnce(&err, tail, timedOut)) return true;
        {
            std::lock_guard<std::mutex> lk(cmdMtx_);
            if (!cmds_.empty() && (cmds_.front() == Cmd::Discard || cmds_.front() == Cmd::Stop)) {
                if (error) *error = "cancelado";
                return false;
            }
        }
        // a tela pode ter sido bloqueada justamente durante a tentativa
        if (!IsInputDesktopAvailable()) {
            Log::Info("Tentativa %d falhou com a tela do Windows bloqueada; aguardando a tela voltar.", attempt + 1);
            if (error) *error = kDesktopUnavailable;
            return false;
        }
        bool ddaProb = false, encProb = false, disk = false, perm = false;
        std::string why = ExplainFfmpegError(tail, &ddaProb, &encProb, &disk, &perm);
        Log::Warn("Tentativa %d falhou: %s | %s", attempt + 1, err.c_str(), why.c_str());
        if (disk || perm) {
            if (error) *error = why;
            return false;
        }
        EncoderKind next;
        if (encProb && EncoderIsHardware(plan_.encoder) && FallbackEncoder(plan_.encoder, next)) {
            Post(RecorderEvent::Type::Warning, std::string("O codificador ") + EncoderDisplayName(plan_.encoder) +
                                                   " falhou; usando " + EncoderDisplayName(next) + ".");
            plan_.encoder = next;
            continue;
        }
        if (plan_.useDDA && !ddaForced_) {
            Post(RecorderEvent::Type::Warning, "A captura pela placa de vídeo (DXGI) não funcionou; usando o modo de compatibilidade (GDI).");
            plan_.useDDA = false;
            RECT r = target_.fullScreen ? target_.monitor.rect : target_.region;
            plan_.capX = r.left;
            plan_.capY = r.top;
            continue;
        }
        if (EncoderIsHardware(plan_.encoder) && FallbackEncoder(plan_.encoder, next)) {
            Post(RecorderEvent::Type::Warning, std::string("Tentando a codificação por software (") + EncoderDisplayName(next) + ").");
            plan_.encoder = next;
            continue;
        }
        if (error) *error = why;
        return false;
    }
    if (error) *error = "A gravação não pôde ser iniciada depois de várias tentativas. Veja o log para detalhes.";
    return false;
}

void Recorder::StopSegment(bool graceful) {
    if (!proc_) return;
    if (graceful && proc_->Running()) {
        // 'q' é o jeito educado de parar o ffmpeg: ele termina de codificar e fecha o arquivo
        proc_->WriteStdin("q");
        if (!proc_->Wait(20000)) {
            Log::Warn("ffmpeg não terminou em 20 s após 'q'; encerrando à força.");
            proc_->Kill();
        }
    } else if (proc_->Running()) {
        proc_->Kill();
    }
    proc_->JoinReaders();
    if (sys_) sys_->EndSegment();
    if (mic_) mic_->EndSegment();
    if (!session_.segments.empty()) session_.segments.back().closed = true;
    session_.Save();
    {
        std::lock_guard<std::mutex> lk(statMtx_);
        if (settings_.fps > 0) completedSec_ += (double)prog_.frame / settings_.fps;
        prog_ = FfProgress();
    }
    curFrames_ = 0;
    proc_.reset();
}

void Recorder::CheckHealth() {
    int64_t now = QpcNowUs();
    // ---- ffmpeg morreu sozinho?
    if (proc_ && !proc_->Running()) {
        std::vector<std::string> tail;
        {
            std::lock_guard<std::mutex> lk(statMtx_);
            tail = errTail_;
        }
        bool dda = false, enc = false, disk = false, perm = false;
        std::string why = ExplainFfmpegError(tail, &dda, &enc, &disk, &perm);
        bool locked = !IsInputDesktopAvailable();
        if (locked) Log::Info("A captura parou porque a tela do Windows foi bloqueada (ou abriu uma janela de permissão).");
        else Log::Error("ffmpeg terminou inesperadamente (código %lu): %s", (unsigned long)proc_->ExitCode(), why.c_str());
        StopSegment(false);
        if (locked && !disk && !perm) {
            // não é defeito: pausa sozinho e continua quando a tela voltar
            EnterDesktopWait();
            return;
        }
        crashTimes_.push_back(now);
        while (!crashTimes_.empty() && now - crashTimes_.front() > 60000000) crashTimes_.erase(crashTimes_.begin());
        if (disk || perm || crashTimes_.size() >= 4) {
            Post(RecorderEvent::Type::Error, "A gravação foi interrompida: " + why + " O que já foi gravado será salvo.", true);
            Push(Cmd::Stop);
            return;
        }
        // tentativa de continuar automaticamente num novo trecho (ex.: resolução mudou,
        // tela de segurança do UAC, jogo em tela cheia...)
        Post(RecorderEvent::Type::Warning, "A captura foi interrompida (" + why + "). Retomando automaticamente…");
        std::string err;
        if (!StartSegment(&err)) {
            if (err == kDesktopUnavailable) {
                EnterDesktopWait();
                return;
            }
            Post(RecorderEvent::Type::Error, "Não foi possível retomar a captura: " + err + " O que já foi gravado será salvo.", true);
            Push(Cmd::Stop);
        }
        return;
    }
    // ---- espaço em disco
    // Não há limite de tempo: o limite é o disco. Para montar o vídeo final o
    // programa precisa de espaço livre parecido com o que a sessão já ocupa
    // (cópia do áudio alinhado + o arquivo final). Por isso a gravação para
    // ANTES de faltar esse espaço, e o vídeo sempre consegue ser montado.
    if (now - lastDiskCheckQpc_ > 3000000) {
        lastDiskCheckQpc_ = now;
        uint64_t freeB = 0, totalB = 0;
        if (GetDiskSpace(session_.dir, freeB, totalB)) {
            const uint64_t MB = 1024ull * 1024;
            uint64_t used = DirectoryBytes(session_.dir);
            uint64_t reserve = used + used / 20 + 300 * MB;   // + 5% (áudio comprimido) + margem
            if (freeB < reserve) {
                Post(RecorderEvent::Type::Error, "O espaço livre no disco chegou ao mínimo necessário para montar o vídeo (" +
                                                     FormatBytes(freeB) + " livres). A gravação foi finalizada automaticamente "
                                                     "para o vídeo ser salvo inteiro.", true);
                Push(Cmd::Stop);
                return;
            }
            if (freeB < reserve + 2048 * MB && !lowDiskWarned_) {
                lowDiskWarned_ = true;
                Post(RecorderEvent::Type::Warning, "Pouco espaço livre no disco de destino (" + FormatBytes(freeB) +
                                                       "). Para montar o vídeo, o programa precisa de espaço igual ao que já foi "
                                                       "gravado; ele finaliza sozinho antes de faltar.");
            }
        }
    }
    // ---- desempenho: velocidade abaixo de 0,9x por vários relatórios seguidos
    FfProgress p;
    {
        std::lock_guard<std::mutex> lk(statMtx_);
        p = prog_;
    }
    static int64_t lastSeenFrame = -1;
    if (p.frame != lastSeenFrame) {
        lastSeenFrame = p.frame;
        if (now - segStartQpc_ > 4000000 && p.speed > 0 && p.speed < 0.9) slowReports_++;
        else if (p.speed >= 0.97) slowReports_ = 0;
        if (slowReports_ >= 6 && !perfWarned_) {
            perfWarned_ = true;
            std::string speed = Fmt("%.2f", p.speed);
            for (auto& ch : speed) if (ch == '.') ch = ',';   // vírgula decimal (padrão brasileiro)
            Post(RecorderEvent::Type::Warning,
                 "O computador não está conseguindo gravar em tempo real nesta configuração (velocidade " + speed + "x). "
                 "O vídeo pode ficar com quadros repetidos. Reduza a resolução ou o FPS, ou use um codificador de hardware.");
        }
    }
}

// ---------------------------------------------------------------------------
// Tela bloqueada (Win+L, Ctrl+Alt+Del, janela de permissão do UAC)
// Nessas telas o Windows não deixa nenhum programa capturar a imagem. Em vez de
// encerrar a gravação, ela pausa sozinha e continua quando a tela volta; o
// tempo bloqueado simplesmente não entra no vídeo (como uma pausa).
// ---------------------------------------------------------------------------
void Recorder::EnterDesktopWait() {
    if (!waitingDesktop_) {
        waitingDesktop_ = true;
        desktopWaitStartQpc_ = QpcNowUs();
        Log::Info("Gravação pausada automaticamente: a tela do Windows está bloqueada. Ela continua sozinha quando a tela voltar.");
    }
    desktopOkSinceQpc_ = 0;
    if (state_.load() != RecState::Paused) {
        state_ = RecState::Paused;
        Post(RecorderEvent::Type::Paused, "");
    }
}

void Recorder::CheckDesktopBack() {
    int64_t now = QpcNowUs();
    if (!IsInputDesktopAvailable()) {
        desktopOkSinceQpc_ = 0;
        return;
    }
    if (!desktopOkSinceQpc_) {
        desktopOkSinceQpc_ = now;
        return;
    }
    if (now - desktopOkSinceQpc_ < 1500000) return;   // espera a tela ficar estável (o desbloqueio tem animação)
    double away = (double)(now - desktopWaitStartQpc_) / 1e6;
    waitingDesktop_ = false;
    state_ = RecState::Resuming;
    std::string err;
    if (StartSegment(&err)) {
        state_ = RecState::Recording;
        Post(RecorderEvent::Type::Resumed, "");
        int secs = (int)std::lround(away), mins = (int)std::lround(away / 60);
        std::string howLong = away < 90 ? (secs == 1 ? std::string("1 segundo") : Fmt("%d segundos", secs))
                                        : (mins == 1 ? std::string("1 minuto") : Fmt("%d minutos", mins));
        Log::Info("A tela do Windows voltou depois de %.0f s: gravação retomada automaticamente.", away);
        Post(RecorderEvent::Type::Info, "A tela do Windows ficou bloqueada por " + howLong +
                                            ". A gravação pausou nesse intervalo e já continuou sozinha.");
    } else if (err == kDesktopUnavailable) {
        EnterDesktopWait();   // bloqueou de novo
    } else {
        state_ = RecState::Paused;
        if (err != "cancelado")
            Post(RecorderEvent::Type::Error, "A tela do Windows voltou, mas a gravação não conseguiu continuar sozinha: " + err +
                                                 " Clique em Continuar para tentar de novo.", false);
    }
}

// ---------------------------------------------------------------------------
// Thread controladora
// ---------------------------------------------------------------------------
void Recorder::Controller() {
    ScopedCom com;
    KeepSystemAwake awake;   // vale até esta thread terminar (fim da sessão)
    std::string err;
    if (!StartSegment(&err)) {
        bool cancelled = err == "cancelado";
        if (err == kDesktopUnavailable)
            err = "A tela do Windows está bloqueada ou com uma janela de permissão aberta. Desbloqueie e tente de novo.";
        if (!cancelled) Post(RecorderEvent::Type::Error, "Não foi possível iniciar a gravação.\n\n" + err, true);
        Cleanup(true);
        state_ = RecState::Idle;
        if (cancelled) {
            Log::Info("Gravação cancelada antes do primeiro quadro.");
            Post(RecorderEvent::Type::Discarded, "Gravação cancelada antes de começar (nada foi gravado).");
        }
        return;
    }
    state_ = RecState::Recording;
    Post(RecorderEvent::Type::Started, "");

    for (;;) {
        Cmd cmd;
        bool have = false;
        {
            std::unique_lock<std::mutex> lk(cmdMtx_);
            cmdCv_.wait_for(lk, std::chrono::milliseconds(150), [this] { return !cmds_.empty(); });
            if (!cmds_.empty()) {
                cmd = cmds_.front();
                cmds_.pop_front();
                have = true;
            }
        }
        if (!have) {
            RecState st = state_.load();
            if (st == RecState::Recording) CheckHealth();
            else if (st == RecState::Paused && waitingDesktop_) CheckDesktopBack();
            continue;
        }
        if (cmd == Cmd::Pause && state_.load() == RecState::Recording) {
            state_ = RecState::Pausing;
            StopSegment(true);
            state_ = RecState::Paused;
            Post(RecorderEvent::Type::Paused, "");
        } else if (cmd == Cmd::Resume && state_.load() == RecState::Paused) {
            waitingDesktop_ = false;   // o usuário mandou continuar: vale o comando dele
            state_ = RecState::Resuming;
            if (StartSegment(&err)) {
                state_ = RecState::Recording;
                Post(RecorderEvent::Type::Resumed, "");
            } else if (err == kDesktopUnavailable) {
                EnterDesktopWait();
            } else if (err != "cancelado") {
                state_ = RecState::Paused;
                Post(RecorderEvent::Type::Error, "Não foi possível continuar a gravação: " + err, false);
            } else {
                state_ = RecState::Paused;
            }
        } else if (cmd == Cmd::Stop) {
            state_ = RecState::Stopping;
            StopSegment(true);
            Finalize();
            return;
        } else if (cmd == Cmd::Discard) {
            state_ = RecState::Stopping;
            StopSegment(false);
            Cleanup(true);
            state_ = RecState::Idle;
            Log::Info("Gravação descartada pelo usuário (temporários apagados).");
            Post(RecorderEvent::Type::Discarded, "A gravação foi descartada.");
            return;
        }
    }
}

void Recorder::Finalize() {
    state_ = RecState::Finalizing;
    // para o áudio antes (libera o dispositivo)
    if (sys_) sys_->Stop();
    if (mic_) mic_->Stop();
    Post(RecorderEvent::Type::Finalizing, "");
    FinalizeResult r = FinalizeSession(session_, defaultOutputDir_, [this](float f, const std::string& stage) {
        finalizeProgress_ = f;
        RecorderEvent e;
        e.type = RecorderEvent::Type::Progress;
        e.progress = f;
        e.text = stage;
        if (sink_) sink_(e);
    });
    if (r.ok) {
        Cleanup(true);
        RecorderEvent e;
        e.type = RecorderEvent::Type::Finished;
        e.path = r.outputFile;
        e.duration = r.duration;
        e.size = r.size;
        state_ = RecState::Idle;
        if (sink_) sink_(e);
    } else if (r.nothingRecorded) {
        Cleanup(true);
        state_ = RecState::Idle;
        Post(RecorderEvent::Type::Error, r.error, true);
    } else {
        // mantém a pasta da sessão para recuperação posterior
        Cleanup(false);
        state_ = RecState::Idle;
        Post(RecorderEvent::Type::Error,
             "Não foi possível montar o vídeo final: " + r.error +
                 "\n\nOs trechos gravados foram preservados e poderão ser recuperados na próxima vez que o programa abrir.",
             true);
    }
}

void Recorder::Cleanup(bool deleteSession) {
    if (proc_) StopSegment(false);
    if (sys_) sys_->Stop();
    if (mic_) mic_->Stop();
    if (deleteSession && !session_.dir.empty()) {
        DeleteDirectoryTree(session_.dir);
        RemoveDirectoryW(ParentDir(session_.dir).c_str());   // só some se estiver vazia
    }
}
