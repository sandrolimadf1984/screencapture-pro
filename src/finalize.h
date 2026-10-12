#pragma once
#include "common.h"
#include "session.h"

// Monta o vídeo final a partir dos trechos de uma sessão:
//  1) lê cada trecho .nut (primeiro quadro e número de quadros)
//  2) corta o áudio de cada trecho EXATAMENTE no intervalo do vídeo
//  3) junta os trechos sem recodificar o vídeo (concat do ffmpeg) e
//     codifica o áudio (AAC no MP4; MP3 ou PCM no AVI)
// Também é usado para recuperar gravações interrompidas.
struct FinalizeResult {
    bool ok = false;
    bool nothingRecorded = false;
    std::wstring outputFile;
    std::string error;
    double duration = 0;
    uint64_t size = 0;
};

using FinalizeProgress = std::function<void(float fraction, const std::string& stage)>;

FinalizeResult FinalizeSession(Session& session, const std::wstring& fallbackOutputDir, FinalizeProgress progress);

// Nome de arquivo final a partir do padrão do usuário
std::string ExpandNamePattern(const std::string& pattern, const std::string& mode, int monitorNumber);
std::wstring UniqueOutputPath(const std::wstring& dir, const std::wstring& nameBase, const std::wstring& ext);
