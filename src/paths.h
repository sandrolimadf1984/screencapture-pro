#pragma once
#include "common.h"

// Onde fica cada coisa. No MODO PORTÁTIL (padrão do pacote) tudo fica junto
// do .exe: config\, logs\ e recordings\. Assim dá para levar a pasta no
// pendrive para qualquer PC. No modo normal, configurações e logs vão para
// %APPDATA%\ScreenCapturePro e os vídeos para a pasta Vídeos do usuário.
struct AppPaths {
    std::wstring exePath;            // caminho completo do ScreenCapturePro.exe
    std::wstring exeDir;             // pasta do programa
    std::wstring ffmpegExe;          // exeDir\ffmpeg\ffmpeg.exe
    bool portable = true;
    bool exeDirWritable = true;      // pendrive protegido / Arquivos de Programas = false
    std::wstring dataDir;            // onde ficam as pastas config e logs
    std::wstring configDir;
    std::wstring logsDir;
    std::wstring defaultRecordingsDir;
};

AppPaths& Paths();
void InitPaths();                               // chamar uma vez no início
// Liga/desliga o modo portátil (move o settings.json de lugar).
bool SetPortableMode(bool portable, std::string* error);
std::wstring PortableMarkerPath();               // exeDir\config\portable.ini
std::wstring KnownFolder(const GUID& id);
bool IsDirWritable(const std::wstring& dir);
