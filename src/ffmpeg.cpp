#include "ffmpeg.h"

#include <algorithm>
#include <cmath>
#include <regex>

#include "log.h"
#include "paths.h"
#include "process.h"

static std::mutex g_capsMutex;
static EncoderCaps g_caps;

EncoderCaps GetEncoderCaps() {
    std::lock_guard<std::mutex> lk(g_capsMutex);
    return g_caps;
}

// Testa um codificador de verdade: codifica 3 quadros pretos. Se a placa de
// vídeo/driver não suportar, o ffmpeg sai com erro e o codificador fica de fora.
static bool TestEncoder(const char* name, const char* pixfmt) {
    std::wstring args = Widen(Fmt("-hide_banner -nostdin -loglevel error -f lavfi -i color=c=black:s=640x360:r=30 "
                                  "-frames:v 3 -pix_fmt %s -c:v %s -f null -", pixfmt, name));
    int code = 0;
    std::string out = RunCapture(Paths().ffmpegExe, args, 15000, &code);
    if (code != 0) Log::Info("Codificador %s indisponível (%d): %s", name, code, Trim(out).substr(0, 300).c_str());
    else Log::Info("Codificador %s OK", name);
    return code == 0;
}

void ProbeEncodersBlocking() {
    EncoderCaps c;
    if (!FileExists(Paths().ffmpegExe)) {
        Log::Error("ffmpeg.exe não encontrado em %s", Narrow(Paths().ffmpegExe).c_str());
        std::lock_guard<std::mutex> lk(g_capsMutex);
        g_caps = c;
        g_caps.probed = true;
        return;
    }
    int code = 0;
    std::string out = RunCapture(Paths().ffmpegExe, L"-hide_banner -version", 15000, &code);
    if (code == 0) {
        c.ffmpegOk = true;
        auto lines = SplitLines(out);
        if (!lines.empty()) c.ffmpegVersion = lines[0];
        Log::Info("%s", c.ffmpegVersion.c_str());
    } else {
        Log::Error("ffmpeg.exe não executou (código %d): %s", code, out.substr(0, 400).c_str());
    }
    if (c.ffmpegOk) {
        // cada fabricante só existe se o driver estiver instalado e a GPU suportar
        c.nvencH264 = TestEncoder("h264_nvenc", "yuv420p");
        c.nvencHevc = c.nvencH264 && TestEncoder("hevc_nvenc", "yuv420p");
        c.qsvH264 = TestEncoder("h264_qsv", "nv12");
        c.qsvHevc = c.qsvH264 && TestEncoder("hevc_qsv", "nv12");
        c.amfH264 = TestEncoder("h264_amf", "nv12");
        c.amfHevc = c.amfH264 && TestEncoder("hevc_amf", "nv12");
    }
    c.probed = true;
    std::lock_guard<std::mutex> lk(g_capsMutex);
    g_caps = c;
}

const char* EncoderDisplayName(EncoderKind k) {
    switch (k) {
        case EncoderKind::X264: return "Software x264 (H.264)";
        case EncoderKind::X265: return "Software x265 (H.265)";
        case EncoderKind::NvencH264: return "NVIDIA NVENC (H.264)";
        case EncoderKind::NvencHevc: return "NVIDIA NVENC (H.265)";
        case EncoderKind::QsvH264: return "Intel Quick Sync (H.264)";
        case EncoderKind::QsvHevc: return "Intel Quick Sync (H.265)";
        case EncoderKind::AmfH264: return "AMD AMF (H.264)";
        case EncoderKind::AmfHevc: return "AMD AMF (H.265)";
        case EncoderKind::Mpeg4: return "Software MPEG-4 (Xvid)";
        case EncoderKind::Mjpeg: return "Software Motion JPEG";
    }
    return "?";
}

const char* EncoderFfmpegName(EncoderKind k) {
    switch (k) {
        case EncoderKind::X264: return "libx264";
        case EncoderKind::X265: return "libx265";
        case EncoderKind::NvencH264: return "h264_nvenc";
        case EncoderKind::NvencHevc: return "hevc_nvenc";
        case EncoderKind::QsvH264: return "h264_qsv";
        case EncoderKind::QsvHevc: return "hevc_qsv";
        case EncoderKind::AmfH264: return "h264_amf";
        case EncoderKind::AmfHevc: return "hevc_amf";
        case EncoderKind::Mpeg4: return "mpeg4";
        case EncoderKind::Mjpeg: return "mjpeg";
    }
    return "libx264";
}

bool EncoderIsHardware(EncoderKind k) {
    return k != EncoderKind::X264 && k != EncoderKind::X265 && k != EncoderKind::Mpeg4 && k != EncoderKind::Mjpeg;
}

EncoderKind ResolveEncoder(VideoCodec codec, EncoderPref pref, const EncoderCaps& c, std::string* note) {
    if (codec == VideoCodec::MPEG4) return EncoderKind::Mpeg4;
    if (codec == VideoCodec::MJPEG) return EncoderKind::Mjpeg;
    bool hevc = codec == VideoCodec::HEVC;
    EncoderKind sw = hevc ? EncoderKind::X265 : EncoderKind::X264;
    EncoderKind nv = hevc ? EncoderKind::NvencHevc : EncoderKind::NvencH264;
    EncoderKind qs = hevc ? EncoderKind::QsvHevc : EncoderKind::QsvH264;
    EncoderKind am = hevc ? EncoderKind::AmfHevc : EncoderKind::AmfH264;
    bool hasNv = hevc ? c.nvencHevc : c.nvencH264;
    bool hasQs = hevc ? c.qsvHevc : c.qsvH264;
    bool hasAm = hevc ? c.amfHevc : c.amfH264;
    switch (pref) {
        case EncoderPref::Software: return sw;
        case EncoderPref::NVENC:
            if (hasNv) return nv;
            if (note) *note = "NVIDIA NVENC não está disponível neste computador; usando codificação por software.";
            return sw;
        case EncoderPref::QSV:
            if (hasQs) return qs;
            if (note) *note = "Intel Quick Sync não está disponível neste computador; usando codificação por software.";
            return sw;
        case EncoderPref::AMF:
            if (hasAm) return am;
            if (note) *note = "AMD AMF não está disponível neste computador; usando codificação por software.";
            return sw;
        case EncoderPref::Auto:
        default:
            // ordem: NVIDIA, Intel, AMD (na prática só existe uma ou duas)
            if (hasNv) return nv;
            if (hasQs) return qs;
            if (hasAm) return am;
            return sw;
    }
}

bool FallbackEncoder(EncoderKind cur, EncoderKind& next) {
    switch (cur) {
        case EncoderKind::NvencH264: case EncoderKind::QsvH264: case EncoderKind::AmfH264: next = EncoderKind::X264; return true;
        case EncoderKind::NvencHevc: case EncoderKind::QsvHevc: case EncoderKind::AmfHevc: next = EncoderKind::X265; return true;
        default: return false;
    }
}

void ComputeOutputSize(const Settings& s, int capW, int capH, int& outW, int& outH) {
    int w = 0, h = 0;
    capW &= ~1;
    capH &= ~1;
    if (!ResolutionSize(s, w, h)) {
        outW = capW;
        outH = capH;
        return;
    }
    if (s.aspect == AspectMode::Fit) {
        // cabe dentro de w x h sem barras: o tamanho final acompanha a proporção da captura
        double f = std::min((double)w / capW, (double)h / capH);
        outW = std::max(2, (int)std::lround(capW * f / 2.0) * 2);
        outH = std::max(2, (int)std::lround(capH * f / 2.0) * 2);
        return;
    }
    outW = w & ~1;
    outH = h & ~1;
}

// ---------------------------------------------------------------------------
// Montagem da linha de comando de um trecho
// ---------------------------------------------------------------------------
struct QualityNumbers { int x264, x265, nvH264, nvHevc, qsv, qsvHevc, amf, mpeg4, mjpeg; };
static QualityNumbers QualityFor(QualityPreset q) {
    switch (q) {
        case QualityPreset::Small: return {28, 30, 30, 31, 30, 31, 29, 7, 9};
        case QualityPreset::High: return {18, 22, 19, 21, 19, 21, 18, 2, 3};
        default: return {23, 26, 24, 26, 24, 26, 23, 4, 5};
    }
}

static std::string ScaleChain(const SegmentPlan& p, bool fullRangeJpeg, const char* pixfmt) {
    // Conversão de cor: a tela é RGB; vídeo é YUV. Uso BT.709 (padrão de HD)
    // e marco isso no arquivo, senão alguns players mostram as cores lavadas.
    std::string color = fullRangeJpeg ? "out_color_matrix=bt601:out_range=pc" : "out_color_matrix=bt709:out_range=tv";
    bool sameSize = (p.outW == p.capW && p.outH == p.capH);
    std::string chain;
    if (sameSize) {
        chain = "scale=" + color;
    } else if (p.aspect == AspectMode::Letterbox) {
        chain = Fmt("scale=%d:%d:force_original_aspect_ratio=decrease:force_divisible_by=2:flags=bicubic:%s", p.outW, p.outH, color.c_str());
    } else {
        // Fit: o tamanho final já foi calculado com a proporção certa; Stretch: estica mesmo
        chain = Fmt("scale=%d:%d:flags=bicubic:%s", p.outW, p.outH, color.c_str());
    }
    chain += Fmt(",format=%s", pixfmt);
    if (!sameSize && p.aspect == AspectMode::Letterbox)
        chain += Fmt(",pad=%d:%d:(ow-iw)/2:(oh-ih)/2:color=black", p.outW, p.outH);
    return chain;
}

static std::string EncoderArgs(const SegmentPlan& p) {
    QualityNumbers q = QualityFor(p.quality);
    bool cbr = p.quality == QualityPreset::CustomBitrate;
    int br = p.bitrateKbps;
    std::string rate = Fmt("-b:v %dk -maxrate %dk -bufsize %dk", br, br * 3 / 2, br * 2);
    int gop = p.fps * 2;   // um quadro-chave a cada 2 s: avançar/voltar no player fica rápido
    std::string bf = p.forAvi ? " -bf 0" : "";
    double pixelRate = (double)p.outW * p.outH * p.fps;
    switch (p.encoder) {
        case EncoderKind::X264: {
            const char* preset = pixelRate <= 1920.0 * 1080 * 30 ? "veryfast" : (pixelRate <= 1920.0 * 1080 * 60 ? "superfast" : "ultrafast");
            return Fmt("-c:v libx264 -preset %s %s -g %d%s -pix_fmt yuv420p", preset,
                       cbr ? rate.c_str() : Fmt("-crf %d", q.x264).c_str(), gop, bf.c_str());
        }
        case EncoderKind::X265: {
            const char* preset = pixelRate <= 1280.0 * 720 * 30 ? "superfast" : "ultrafast";
            return Fmt("-c:v libx265 -preset %s %s -g %d -pix_fmt yuv420p -x265-params log-level=error", preset,
                       cbr ? rate.c_str() : Fmt("-crf %d", q.x265).c_str(), gop);
        }
        case EncoderKind::NvencH264:
        case EncoderKind::NvencHevc: {
            int cq = p.encoder == EncoderKind::NvencH264 ? q.nvH264 : q.nvHevc;
            return Fmt("-c:v %s -preset p4 -tune hq -rc vbr %s -g %d%s -pix_fmt yuv420p", EncoderFfmpegName(p.encoder),
                       cbr ? rate.c_str() : Fmt("-cq %d -b:v 0", cq).c_str(), gop, bf.c_str());
        }
        case EncoderKind::QsvH264:
        case EncoderKind::QsvHevc: {
            int gq = p.encoder == EncoderKind::QsvH264 ? q.qsv : q.qsvHevc;
            return Fmt("-c:v %s -preset veryfast %s -g %d%s -pix_fmt nv12", EncoderFfmpegName(p.encoder),
                       cbr ? rate.c_str() : Fmt("-global_quality %d", gq).c_str(), gop, bf.c_str());
        }
        case EncoderKind::AmfH264:
        case EncoderKind::AmfHevc: {
            // o hevc_amf não tem quadros B (nem a opção qp_b)
            std::string cqp = p.encoder == EncoderKind::AmfH264 ? Fmt("-rc cqp -qp_i %d -qp_p %d -qp_b %d", q.amf, q.amf + 2, q.amf + 4)
                                                                 : Fmt("-rc cqp -qp_i %d -qp_p %d", q.amf, q.amf + 2);
            return Fmt("-c:v %s -quality balanced %s -g %d%s -pix_fmt nv12", EncoderFfmpegName(p.encoder),
                       cbr ? ("-rc vbr_peak " + rate).c_str() : cqp.c_str(), gop, bf.c_str());
        }
        case EncoderKind::Mpeg4:
            return Fmt("-c:v mpeg4 -vtag XVID %s -g %d -pix_fmt yuv420p", cbr ? rate.c_str() : Fmt("-q:v %d", q.mpeg4).c_str(), p.fps * 10);
        case EncoderKind::Mjpeg:
            return Fmt("-c:v mjpeg %s -pix_fmt yuvj420p", cbr ? rate.c_str() : Fmt("-q:v %d", q.mjpeg).c_str());
    }
    return "-c:v libx264 -preset veryfast -crf 23 -pix_fmt yuv420p";
}

static const char* PixFmtFor(EncoderKind k) {
    switch (k) {
        case EncoderKind::QsvH264: case EncoderKind::QsvHevc: case EncoderKind::AmfH264: case EncoderKind::AmfHevc: return "nv12";
        case EncoderKind::Mjpeg: return "yuvj420p";
        default: return "yuv420p";
    }
}

std::wstring BuildSegmentArgs(const SegmentPlan& p) {
    std::string a = "-hide_banner -loglevel level+warning -nostats -progress pipe:1 -stats_period 0.5 ";
    bool jpeg = p.encoder == EncoderKind::Mjpeg;
    std::string scale = ScaleChain(p, jpeg, PixFmtFor(p.encoder));
    // Âncora de tempo (ver comentário grande em recorder.cpp): o 1º quadro recebe
    // a hora do relógio do sistema (RTCTIME) e os demais seguem os intervalos
    // reais da captura. Tudo menos o "base" da sessão, para os números ficarem pequenos.
    std::string anchor = Fmt("setpts='if(eq(N\\,0)\\,(RTCTIME-%lld)/(TB*1000000)\\,PREV_OUTPTS+(PTS-PREV_INPTS))'",
                             (long long)p.baseUs);
    if (p.useDDA) {
        std::string dda = Fmt("ddagrab=output_idx=%d:framerate=%d:draw_mouse=%d", p.ddaOutput, p.fps, p.drawMouse ? 1 : 0);
        if (!p.fullMonitor) dda += Fmt(":offset_x=%d:offset_y=%d:video_size=%dx%d", p.capX, p.capY, p.capW, p.capH);
        a += Fmt("-init_hw_device d3d11va=scp:%d -filter_hw_device scp ", p.ddaAdapter);
        a += Fmt("-filter_complex \"%s,hwdownload,format=bgra,%s,%s[v]\" -map \"[v]\" ", dda.c_str(), anchor.c_str(), scale.c_str());
    } else {
        // gdigrab já marca cada quadro com a hora do sistema (av_gettime), então
        // basta subtrair o "base" — com -copyts os valores chegam intactos.
        a += Fmt("-thread_queue_size 64 -f gdigrab -framerate %d -draw_mouse %d -offset_x %d -offset_y %d -video_size %dx%d -i desktop ",
                 p.fps, p.drawMouse ? 1 : 0, p.capX, p.capY, p.capW, p.capH);
        a += Fmt("-vf \"setpts=PTS-%lld/(TB*1000000),%s\" ", (long long)p.baseUs, scale.c_str());
    }
    // -copyts + -r: o ffmpeg mantém nossos timestamps e gera quadros a FPS
    // constante a partir do 1º quadro (modo "vscfr"), sem preencher o começo.
    a += Fmt("-copyts -r %d ", p.fps);
    a += EncoderArgs(p) + " ";
    if (p.encoder != EncoderKind::Mjpeg && p.encoder != EncoderKind::Mpeg4)
        a += "-colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv ";
    a += "-an ";
    if (p.testSeconds > 0) {
        a += Fmt("-t %.1f -f null -", p.testSeconds);
    } else {
        // NUT + flush a cada pacote: se o PC desligar no meio, o trecho continua legível
        a += "-flush_packets 1 -f nut -y ";
        return Widen(a) + QuoteArg(p.outFile);
    }
    return Widen(a);
}

// ---------------------------------------------------------------------------
// Leitura de informações
// ---------------------------------------------------------------------------
MediaInfo ProbeMedia(const std::wstring& file) {
    MediaInfo mi;
    int code = 0;
    std::string out = RunCapture(Paths().ffmpegExe, L"-hide_banner -nostdin -i " + QuoteArg(file), 20000, &code);
    static const std::regex durRe(R"(Duration:\s*(\d+):(\d+):(\d+(?:\.\d+)?))");
    static const std::regex inputRe(R"(Input #0,\s*([^,]+))");
    static const std::regex sizeRe(R"((\d{2,5})x(\d{2,5}))");
    static const std::regex fpsRe(R"(([\d.]+) fps)");
    std::smatch m;
    for (const auto& line : SplitLines(out)) {
        if (std::regex_search(line, m, inputRe) && mi.container.empty()) mi.container = Trim(m[1]);
        if (std::regex_search(line, m, durRe)) {
            mi.duration = std::stoi(m[1]) * 3600.0 + std::stoi(m[2]) * 60.0 + std::stod(m[3]);
            mi.ok = true;
        }
        size_t v = line.find("Video: ");
        if (v != std::string::npos && line.find("Stream #") != std::string::npos && mi.vcodec.empty()) {
            std::string rest = line.substr(v + 7);
            mi.vcodec = rest.substr(0, rest.find_first_of(" ,("));
            std::string afterCodec = rest;
            std::smatch sm;
            // a resolução vem depois do formato de pixel; o primeiro "NNNxNNN" com 2+ dígitos serve
            std::string::const_iterator it = afterCodec.begin();
            while (std::regex_search(it, afterCodec.cend(), sm, sizeRe)) {
                int w = std::stoi(sm[1]), h = std::stoi(sm[2]);
                if (w >= 16 && h >= 16) { mi.width = w; mi.height = h; break; }
                it = sm.suffix().first;
            }
            if (std::regex_search(afterCodec, sm, fpsRe)) mi.fps = std::stod(sm[1]);
        }
        size_t a = line.find("Audio: ");
        if (a != std::string::npos && line.find("Stream #") != std::string::npos) {
            mi.audioTracks++;
            if (mi.acodec.empty()) {
                std::string rest = line.substr(a + 7);
                mi.acodec = rest.substr(0, rest.find_first_of(" ,("));
            }
        }
    }
    if (mi.container.find("mov") != std::string::npos) mi.container = "MP4";
    else if (mi.container.find("avi") != std::string::npos) mi.container = "AVI";
    else if (mi.container.find("matroska") != std::string::npos) mi.container = "MKV";
    return mi;
}

SegmentProbe ProbeSegment(const std::wstring& nutFile, const std::function<void(double)>& onProgress) {
    SegmentProbe sp;
    uint64_t fileBytes = FileSize(nutFile);
    if (fileBytes == 0) {
        sp.error = "trecho vazio";
        return sp;
    }
    // framecrc lista cada pacote com seu timestamp (sem decodificar = rápido).
    // As linhas são lidas na hora em que chegam (nada fica acumulado na memória),
    // então trechos de muitas horas funcionam do mesmo jeito.
    std::mutex m;
    int64_t tbNum = 0, tbDen = 0, minPts = INT64_MAX, maxPts = INT64_MIN, count = 0;
    uint64_t bytes = 0;
    std::atomic<int64_t> lastLineQpc{QpcNowUs()};
    std::vector<std::string> errTail;
    ChildProcess p;
    p.OnStdout([&](const std::string& line) {
        lastLineQpc = QpcNowUs();
        std::lock_guard<std::mutex> lk(m);
        if (StartsWith(line, "#tb 0:")) {
            std::string v = Trim(line.substr(6));
            size_t slash = v.find('/');
            if (slash != std::string::npos) {
                try {
                    tbNum = std::stoll(v.substr(0, slash));
                    tbDen = std::stoll(v.substr(slash + 1));
                } catch (...) {
                }
            }
            return;
        }
        if (line.empty() || line[0] == '#') return;
        // formato: stream, dts, pts, duração, tamanho, crc
        size_t c1 = line.find(','), c2 = c1 == std::string::npos ? c1 : line.find(',', c1 + 1),
               c3 = c2 == std::string::npos ? c2 : line.find(',', c2 + 1),
               c4 = c3 == std::string::npos ? c3 : line.find(',', c3 + 1),
               c5 = c4 == std::string::npos ? c4 : line.find(',', c4 + 1);
        if (c3 == std::string::npos) return;
        try {
            int64_t pts = std::stoll(Trim(line.substr(c2 + 1, c3 - c2 - 1)));
            minPts = std::min(minPts, pts);
            maxPts = std::max(maxPts, pts);
            count++;
            if (c5 != std::string::npos) bytes += (uint64_t)std::stoll(Trim(line.substr(c4 + 1, c5 - c4 - 1)));
        } catch (...) {
            return;
        }
        if (onProgress && count % 2000 == 0) onProgress(std::min(1.0, (double)bytes / (double)fileBytes));
    });
    p.OnStderr([&](const std::string& line) {
        lastLineQpc = QpcNowUs();
        std::lock_guard<std::mutex> lk(m);
        errTail.push_back(line);
        if (errTail.size() > 10) errTail.erase(errTail.begin());
    });
    std::string err;
    if (!p.Start(Paths().ffmpegExe,
                 L"-hide_banner -nostdin -loglevel error -copyts -i " + QuoteArg(nutFile) + L" -map 0:v:0 -c copy -f framecrc -",
                 L"", &err)) {
        sp.error = "não foi possível abrir o ffmpeg: " + err;
        return sp;
    }
    p.CloseStdin();
    // sem tempo máximo: só desiste se o ffmpeg ficar 2 minutos sem dar sinal de vida
    bool stalled = false;
    while (!p.Wait(1000)) {
        if (QpcNowUs() - lastLineQpc.load() > 120000000) {
            stalled = true;
            p.Kill();
            break;
        }
    }
    p.JoinReaders();
    std::lock_guard<std::mutex> lk(m);
    if (stalled) {
        sp.error = "a leitura do trecho travou";
        return sp;
    }
    if (count == 0 || tbDen == 0) {
        std::string tail;
        for (const auto& l : errTail) tail += l + " ";
        sp.error = "não foi possível ler o trecho: " + Trim(tail).substr(0, 300);
        return sp;
    }
    if (onProgress) onProgress(1.0);
    sp.ok = true;
    sp.frames = count;
    sp.firstPts = (double)minPts * (double)tbNum / (double)tbDen;
    sp.lastPts = (double)maxPts * (double)tbNum / (double)tbDen;
    return sp;
}

bool ParseProgressLine(const std::string& line, FfProgress& p) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
    try {
        if (k == "frame") p.frame = std::stoll(v);
        else if (k == "fps") p.fps = std::stod(v);
        else if (k == "dup_frames") p.dupFrames = std::stoll(v);
        else if (k == "drop_frames") p.dropFrames = std::stoll(v);
        else if (k == "out_time_us") { if (v != "N/A") p.outTimeUs = std::stoll(v); }
        else if (k == "speed") { if (v != "N/A") p.speed = std::stod(v); }
        else if (k == "progress") { p.ended = (v == "end"); return true; }
    } catch (...) {
    }
    return false;
}

std::string ExplainFfmpegError(const std::vector<std::string>& tail, bool* dda, bool* enc, bool* disk, bool* perm) {
    std::string joined;
    for (const auto& l : tail) joined += l + "\n";
    std::string low = ToLower(joined);
    auto has = [&](const char* s) { return low.find(s) != std::string::npos; };
    if (dda) *dda = has("ddagrab") || has("dxgi") || has("duplicat") || has("d3d11") || has("output parameters changed");
    if (enc) *enc = has("nvenc") || has("_qsv") || has("_amf") || has("error while opening encoder") ||
                    has("initializing encoder") || has("mfx") || has("cuda") || has("no capable devices") ||
                    has("driver does not support") || has("cannot load") || has("amf");
    if (disk) *disk = has("no space left") || has("disk full") || has("not enough space") || has("file too large");
    if (has("file too large")) return "O arquivo passou do tamanho máximo que o disco aceita (4 GB em discos FAT32). "
                                      "Grave numa pasta do disco do computador ou num pendrive em exFAT/NTFS.";
    if (perm) *perm = has("permission denied") || has("access is denied");
    if (disk && *disk) return "Falta espaço no disco de destino.";
    if (perm && *perm) return "Sem permissão para gravar na pasta de destino.";
    if (dda && *dda) return "A captura pela placa de vídeo (DXGI) não funcionou neste monitor.";
    if (enc && *enc) return "O codificador de vídeo escolhido falhou ao iniciar.";
    if (has("could not find") || has("invalid argument")) return "O ffmpeg recusou os parâmetros de gravação.";
    std::string last;
    for (auto it = tail.rbegin(); it != tail.rend(); ++it) {
        if (!Trim(*it).empty()) { last = Trim(*it); break; }
    }
    return last.empty() ? "O processo de gravação terminou inesperadamente." : "O ffmpeg informou: " + last;
}
