#include "finalize.h"

#include <algorithm>
#include <cmath>

#include "audio.h"
#include "log.h"
#include "paths.h"
#include "process.h"

std::string ExpandNamePattern(const std::string& pattern, const std::string& mode, int monitorNumber) {
    std::string s = pattern;
    ReplaceAll(s, "{data}", NowStamp("%Y-%m-%d"));
    ReplaceAll(s, "{hora}", NowStamp("%H-%M-%S"));
    ReplaceAll(s, "{ano}", NowStamp("%Y"));
    ReplaceAll(s, "{mes}", NowStamp("%m"));
    ReplaceAll(s, "{dia}", NowStamp("%d"));
    ReplaceAll(s, "{modo}", mode);
    ReplaceAll(s, "{monitor}", Fmt("Monitor%d", monitorNumber));
    ReplaceAll(s, "{app}", APP_FILE_PREFIX);
    // caracteres proibidos em nomes de arquivo no Windows
    for (auto& c : s)
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '-';
    s = Trim(s);
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    if (s.empty()) s = std::string(APP_FILE_PREFIX) + "_" + NowStamp("%Y-%m-%d_%H-%M-%S");
    return s;
}

std::wstring UniqueOutputPath(const std::wstring& dir, const std::wstring& nameBase, const std::wstring& ext) {
    // Nunca sobrescreve: se já existir, acrescenta (2), (3)...
    std::wstring p = JoinPath(dir, nameBase + ext);
    for (int i = 2; FileExists(p) || FileExists(p + L".part"); i++) p = JoinPath(dir, nameBase + WFmt(L" (%d)", i) + ext);
    return p;
}

// Copia do arquivo de áudio bruto do trecho o intervalo [start, start+count)
// (em amostras de 48 kHz estéreo). O que faltar vira silêncio.
static bool ExtractAudio(const std::wstring& src, int64_t start, int64_t count, HANDLE dst, size_t frameBytes) {
    std::vector<char> zeros(frameBytes * 4800, 0);
    auto writeZeros = [&](int64_t n) {
        while (n > 0) {
            int64_t chunk = std::min<int64_t>(n, 4800);
            DWORD wr;
            if (!WriteFile(dst, zeros.data(), (DWORD)(chunk * frameBytes), &wr, nullptr)) return false;
            n -= chunk;
        }
        return true;
    };
    HANDLE in = src.empty() ? INVALID_HANDLE_VALUE
                            : CreateFileW(src.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (in == INVALID_HANDLE_VALUE) return writeZeros(count);   // sem áudio neste trecho
    LARGE_INTEGER sz;
    GetFileSizeEx(in, &sz);
    int64_t total = sz.QuadPart / (int64_t)frameBytes;
    bool ok = true;
    if (start < 0) {
        int64_t pre = std::min(-start, count);
        ok = writeZeros(pre);
        count -= pre;
        start = 0;
    }
    int64_t avail = std::max<int64_t>(0, std::min(count, total - start));
    if (ok && avail > 0) {
        LARGE_INTEGER pos;
        pos.QuadPart = start * (int64_t)frameBytes;
        SetFilePointerEx(in, pos, nullptr, FILE_BEGIN);
        std::vector<char> buf(1 << 20);
        int64_t left = avail * (int64_t)frameBytes;
        while (left > 0 && ok) {
            DWORD want = (DWORD)std::min<int64_t>(left, (int64_t)buf.size()), rd = 0, wr = 0;
            if (!ReadFile(in, buf.data(), want, &rd, nullptr) || rd == 0) break;
            ok = WriteFile(dst, buf.data(), rd, &wr, nullptr) && wr == rd;
            left -= rd;
        }
        // se o arquivo acabou antes (leitura curta), completa com zeros
        if (ok && left > 0) ok = writeZeros(left / (int64_t)frameBytes);
    }
    if (ok) ok = writeZeros(count - avail);
    CloseHandle(in);
    return ok;
}

static std::string VolumeExpr(int percent) { return Fmt("%.3f", percent / 100.0); }

// Volume de uma faixa; acima de 100% entra um limitador (sem ele, voz ou som
// do PC muito amplificados "estouram" e ficam chiados)
static const char* kLimiter = "alimiter=limit=0.97:attack=5:release=50:level=0:latency=1";
static std::string TrackChain(int percent) {
    std::string c = "volume=" + VolumeExpr(percent);
    if (percent > 100) c += std::string(",") + kLimiter;
    return c;
}

FinalizeResult FinalizeSession(Session& s, const std::wstring& fallbackOutputDir, FinalizeProgress progress) {
    FinalizeResult r;
    auto report = [&](float f, const std::string& stage) { if (progress) progress(f, stage); };
    Log::Info("Finalizando sessão %s (%zu trecho(s))", Narrow(s.dir).c_str(), s.segments.size());

    // ---- 1. lê os trechos
    struct Seg { SegmentRecord rec; double first = 0; int64_t frames = 0; };
    std::vector<Seg> segs;
    std::string encoder0;
    bool mixedEncoders = false;
    for (size_t i = 0; i < s.segments.size(); i++) {
        report(0.02f + 0.18f * (float)i / std::max<size_t>(1, s.segments.size()), "Lendo trechos gravados");
        const auto& rec = s.segments[i];
        std::wstring vf = s.File(rec.video);
        if (!FileExists(vf)) continue;
        size_t n = s.segments.size();
        SegmentProbe sp = ProbeSegment(vf, [&](double f) {
            report(0.02f + 0.18f * (float)((double)i + f) / (float)std::max<size_t>(1, n), "Lendo trechos gravados");
        });
        if (!sp.ok || sp.frames < 2) {
            Log::Warn("Trecho %s ignorado: %s", rec.video.c_str(), sp.error.c_str());
            continue;
        }
        if (encoder0.empty()) encoder0 = rec.encoder;
        else if (rec.encoder != encoder0) mixedEncoders = true;
        // Trecho cortado à força (ffmpeg caiu, PC desligou): com B-frames podem faltar
        // alguns quadros no fim, e a contagem fica menor que o tempo que o trecho ocupa.
        // Usa o tempo ocupado (até 1 s a mais) para o próximo trecho não se sobrepor.
        int64_t frames = sp.frames;
        int64_t span = (int64_t)std::llround((sp.lastPts - sp.firstPts) * s.fps) + 1;
        if (span > frames) {
            int64_t extra = std::min<int64_t>(span - frames, s.fps);
            Log::Info("Trecho %s com %lld quadro(s) faltando (interrompido no meio): usando %lld na linha do tempo.",
                      rec.video.c_str(), (long long)(span - frames), (long long)(frames + extra));
            frames += extra;
        }
        segs.push_back({rec, sp.firstPts, frames});
        Log::Info("Trecho %s: início %.4f s, %lld quadros, áudio a partir de %.4f s", rec.video.c_str(), sp.firstPts,
                  (long long)frames, rec.audioStartUs / 1e6);
    }
    if (segs.empty()) {
        r.nothingRecorded = true;
        r.error = "Nada foi gravado (a gravação foi interrompida antes do primeiro quadro).";
        return r;
    }

    // ---- 2. áudio alinhado ao vídeo, trecho por trecho
    double totalDur = 0;
    for (auto& g : segs) totalDur += (double)g.frames / s.fps;
    bool useSys = s.hasSys, useMic = s.hasMic;
    // formato do áudio bruto: 16 bits (atual) ou float (sessões antigas)
    const bool f32 = s.audioFormat == "f32le";
    const size_t frameBytes = f32 ? sizeof(float) * kAudioChannels : (size_t)kTempBytesPerFrame;
    const char* rawFmt = f32 ? "f32le" : "s16le";
    std::wstring sysAll = JoinPath(s.dir, L"sys_all.pcm"), micAll = JoinPath(s.dir, L"mic_all.pcm");
    for (int src = 0; src < 2; src++) {
        bool use = src == 0 ? useSys : useMic;
        if (!use) continue;
        std::wstring outPath = src == 0 ? sysAll : micAll;
        HANDLE out = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE) {
            r.error = "Não foi possível criar o áudio temporário: " + LastErrorText();
            return r;
        }
        bool ok = true;
        for (size_t i = 0; i < segs.size() && ok; i++) {
            report(0.2f + 0.15f * (float)(src * segs.size() + i) / (2.0f * segs.size()), "Sincronizando o áudio");
            const auto& g = segs[i];
            const std::string& name = src == 0 ? g.rec.sysAudio : g.rec.micAudio;
            // a amostra 0 do arquivo de áudio = audioStartUs; o vídeo começa em 'first'
            int64_t start = (int64_t)std::llround((g.first * 1e6 - (double)g.rec.audioStartUs) * kAudioRate / 1e6);
            int64_t count = (int64_t)std::llround((double)g.frames * kAudioRate / s.fps);
            ok = ExtractAudio(name.empty() ? std::wstring() : s.File(name), start, count, out, frameBytes);
        }
        CloseHandle(out);
        if (!ok) {
            r.error = "Falha ao preparar o áudio (disco cheio?)";
            return r;
        }
    }

    // ---- 3. lista de concatenação (com a duração exata de cada trecho)
    std::string list = "ffconcat version 1.0\n";
    for (const auto& g : segs) list += Fmt("file '%s'\nduration %.6f\n", g.rec.video.c_str(), (double)g.frames / s.fps);
    std::wstring listPath = JoinPath(s.dir, L"concat.txt");
    if (!WriteFileUtf8Atomic(listPath, list)) {
        r.error = "Falha ao gravar a lista de trechos: " + LastErrorText();
        return r;
    }

    // ---- 4. destino
    std::wstring outDir = Widen(s.outputDir);
    if (outDir.empty() || !EnsureDir(outDir)) {
        Log::Warn("Pasta de destino original indisponível (%s); usando %s", s.outputDir.c_str(), Narrow(fallbackOutputDir).c_str());
        outDir = fallbackOutputDir;
        EnsureDir(outDir);
    }
    std::wstring ext = ContainerExtension(s.container);
    std::wstring finalPath = UniqueOutputPath(outDir, Widen(s.nameBase), ext);
    std::wstring partPath = finalPath + L".part";

    // ---- 5. comando final
    std::string a = "-hide_banner -nostdin -loglevel level+warning -nostats -progress pipe:1 -y ";
    a += "-f concat -safe 0 -i \"" + Narrow(listPath) + "\" ";
    int nextInput = 1, sysIn = -1, micIn = -1;
    if (useSys) { a += Fmt("-f %s -ar 48000 -ac 2 -i \"", rawFmt) + Narrow(sysAll) + "\" "; sysIn = nextInput++; }
    if (useMic) { a += Fmt("-f %s -ar 48000 -ac 2 -i \"", rawFmt) + Narrow(micAll) + "\" "; micIn = nextInput++; }
    a += "-map 0:v:0 ";
    if (mixedEncoders) {
        // Trechos com codificadores diferentes (ex.: a placa de vídeo falhou no meio
        // e o programa passou para software): recodifica para ficar um vídeo só.
        Log::Warn("Trechos com codificadores diferentes; recodificando o vídeo.");
        const char* enc = s.codec == VideoCodec::HEVC ? "-c:v libx265 -preset fast -crf 24 -pix_fmt yuv420p"
                          : s.codec == VideoCodec::MPEG4 ? "-c:v mpeg4 -vtag XVID -q:v 3 -pix_fmt yuv420p"
                          : s.codec == VideoCodec::MJPEG ? "-c:v mjpeg -q:v 4 -pix_fmt yuvj420p"
                                                         : "-c:v libx264 -preset fast -crf 20 -pix_fmt yuv420p";
        a += Fmt("-vf scale=%d:%d -r %d %s ", s.outW, s.outH, s.fps, enc);
        if (s.container == Container::AVI) a += "-bf 0 ";
    } else {
        a += "-c:v copy ";
    }
    // AVI precisa da base de tempo = 1/FPS na cópia, senão o player conta quadros errado
    if (s.container == Container::AVI) a += Fmt("-time_base:v 1/%d ", s.fps);

    std::string acodec;
    if (s.container == Container::MP4) acodec = Fmt("-c:a aac -b:a %dk ", s.aacBitrate);
    else if (s.aviAudio == AviAudio::PCM) acodec = "-c:a pcm_s16le ";
    else acodec = Fmt("-c:a libmp3lame -b:a %dk ", s.mp3Bitrate);

    if (sysIn >= 0 && micIn >= 0) {
        if (s.tracks == AudioTracks::Separate) {
            a += Fmt("-filter_complex \"[%d:a]%s[sys];[%d:a]%s[mic]\" -map \"[sys]\" -map \"[mic]\" ", sysIn,
                     TrackChain(s.sysVolume).c_str(), micIn, TrackChain(s.micVolume).c_str());
            a += acodec;
            a += "-metadata:s:a:0 title=\"Áudio do sistema\" -metadata:s:a:1 title=\"Microfone\" ";
            a += "-metadata:s:a:0 language=por -metadata:s:a:1 language=por ";
        } else {
            // mistura com volumes independentes; o limitador evita estouro quando os dois somam alto
            a += Fmt("-filter_complex \"[%d:a]volume=%s[sys];[%d:a]volume=%s[mic];[sys][mic]amix=inputs=2:duration=longest:normalize=0,"
                     "%s[aout]\" -map \"[aout]\" ",
                     sysIn, VolumeExpr(s.sysVolume).c_str(), micIn, VolumeExpr(s.micVolume).c_str(), kLimiter);
            a += acodec;
        }
    } else if (sysIn >= 0 || micIn >= 0) {
        int in = sysIn >= 0 ? sysIn : micIn;
        int vol = sysIn >= 0 ? s.sysVolume : s.micVolume;
        if (vol == 100) a += Fmt("-map %d:a ", in);
        else a += Fmt("-filter_complex \"[%d:a]%s[aout]\" -map \"[aout]\" ", in, TrackChain(vol).c_str());
        a += acodec;
    } else {
        a += "-an ";
    }
    if (s.container == Container::MP4) {
        a += "-movflags +faststart ";
        if (s.codec == VideoCodec::HEVC) a += "-tag:v hvc1 ";
    }
    a += "-metadata title=\"" + s.nameBase + "\" -metadata comment=\"Gravado com " APP_NAME_U8 "\" ";
    a += s.container == Container::MP4 ? "-f mp4 " : "-f avi ";
    std::wstring args = Widen(a) + QuoteArg(partPath);

    Log::Info("Montando vídeo final: ffmpeg %s", Narrow(args).c_str());
    report(0.36f, "Gerando o vídeo final");
    ChildProcess p;
    std::mutex m;
    std::vector<std::string> errTail;
    FfProgress prog;
    p.OnStdout([&](const std::string& line) {
        std::lock_guard<std::mutex> lk(m);
        if (ParseProgressLine(line, prog) && totalDur > 0) {
            float f = (float)std::min(1.0, prog.outTimeUs / 1e6 / totalDur);
            report(0.36f + 0.62f * f, "Gerando o vídeo final");
        }
    });
    p.OnStderr([&](const std::string& line) {
        Log::Raw(line);
        std::lock_guard<std::mutex> lk(m);
        errTail.push_back(line);
        if (errTail.size() > 30) errTail.erase(errTail.begin());
    });
    std::string err;
    if (!p.Start(Paths().ffmpegExe, args, s.dir, &err)) {
        r.error = err;
        return r;
    }
    p.Wait(INFINITE);
    p.JoinReaders();
    DWORD code = p.ExitCode();
    if (code != 0 || FileSize(partPath) == 0) {
        bool d1, d2, disk, perm;
        r.error = ExplainFfmpegError(errTail, &d1, &d2, &disk, &perm);
        Log::Error("Falha ao montar o vídeo final (código %lu): %s", (unsigned long)code, r.error.c_str());
        DeleteFileW(partPath.c_str());
        return r;
    }
    if (!MoveFileExW(partPath.c_str(), finalPath.c_str(), MOVEFILE_WRITE_THROUGH)) {
        // alguém criou um arquivo com o mesmo nome no meio do caminho: escolhe outro
        finalPath = UniqueOutputPath(outDir, Widen(s.nameBase), ext);
        if (!MoveFileExW(partPath.c_str(), finalPath.c_str(), MOVEFILE_WRITE_THROUGH)) {
            r.error = "Não foi possível renomear o arquivo final: " + LastErrorText();
            return r;
        }
    }
    report(1.0f, "Concluído");
    r.ok = true;
    r.outputFile = finalPath;
    r.duration = totalDur;
    r.size = FileSize(finalPath);
    s.finalized = true;
    s.Save();
    Log::Info("Vídeo salvo: %s (%.1f s, %s)", Narrow(finalPath).c_str(), totalDur, FormatBytes(r.size).c_str());
    return r;
}
