#pragma once
#include "common.h"

// Log técnico em arquivo (pasta logs\). Um arquivo por dia, guarda os 15 mais
// recentes. Tudo que der errado vai parar aqui com detalhes — é o primeiro
// lugar para olhar quando algo não funcionar.
namespace Log {
void Init(const std::wstring& logsDir);
void Info(const char* fmt, ...);
void Warn(const char* fmt, ...);
void Error(const char* fmt, ...);
void Raw(const std::string& line);                 // sem formatação (ex.: saída do ffmpeg)
std::vector<std::string> Recent(size_t maxLines);  // últimas linhas (para a tela de detalhes)
std::wstring CurrentFile();
std::wstring Dir();
}  // namespace Log
