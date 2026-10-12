#include "paths.h"

#include <knownfolders.h>
#include <shlobj.h>

#include "log.h"

static AppPaths g_paths;

AppPaths& Paths() { return g_paths; }

std::wstring KnownFolder(const GUID& id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p)) && p) out = p;
    if (p) CoTaskMemFree(p);
    return out;
}

bool IsDirWritable(const std::wstring& dir) {
    if (!EnsureDir(dir)) return false;
    // Testa de verdade criando um arquivo (permissões do Windows são cheias de
    // exceções; tentar é a única forma confiável).
    std::wstring probe = JoinPath(dir, L".scp_write_test.tmp");
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    DeleteFileW(probe.c_str());
    return true;
}

std::wstring PortableMarkerPath() { return JoinPath(JoinPath(g_paths.exeDir, L"config"), L"portable.ini"); }

static void ApplyMode(bool portable) {
    AppPaths& p = g_paths;
    p.portable = portable;
    if (portable) {
        p.dataDir = p.exeDir;
        p.defaultRecordingsDir = JoinPath(p.exeDir, L"recordings");
    } else {
        std::wstring appdata = KnownFolder(FOLDERID_RoamingAppData);
        if (appdata.empty()) appdata = p.exeDir;
        p.dataDir = JoinPath(appdata, APP_ID_W);
        std::wstring videos = KnownFolder(FOLDERID_Videos);
        if (videos.empty()) videos = KnownFolder(FOLDERID_Profile);
        p.defaultRecordingsDir = JoinPath(videos, APP_NAME_W);
    }
    p.configDir = JoinPath(p.dataDir, L"config");
    p.logsDir = JoinPath(p.dataDir, L"logs");
}

void InitPaths() {
    AppPaths& p = g_paths;
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    p.exePath.assign(buf, n);
    p.exeDir = ParentDir(p.exePath);
    p.ffmpegExe = JoinPath(JoinPath(p.exeDir, L"ffmpeg"), L"ffmpeg.exe");

    // Modo portátil = existe o arquivo-marcador config\portable.ini ao lado do exe.
    bool wantPortable = FileExists(PortableMarkerPath());
    p.exeDirWritable = IsDirWritable(JoinPath(p.exeDir, L"config"));
    if (wantPortable && !p.exeDirWritable) {
        // Pendrive protegido contra gravação ou pasta sem permissão: não dá para
        // salvar nada ali, então uso o %APPDATA% e aviso o usuário depois.
        wantPortable = false;
    }
    ApplyMode(wantPortable);
    EnsureDir(p.configDir);
    EnsureDir(p.logsDir);
}

bool SetPortableMode(bool portable, std::string* error) {
    AppPaths& p = g_paths;
    if (portable == p.portable) return true;
    if (portable && !p.exeDirWritable) {
        if (error) *error = "A pasta do programa não permite gravação (pendrive protegido ou pasta do sistema). "
                            "O modo portátil precisa salvar as configurações ao lado do programa.";
        return false;
    }
    std::wstring oldConfig = JoinPath(p.configDir, L"settings.json");
    std::string data;
    bool haveOld = ReadFileUtf8(oldConfig, data);

    if (portable) {
        EnsureDir(JoinPath(p.exeDir, L"config"));
        std::string marker =
            "; Este arquivo liga o MODO PORTATIL do " APP_NAME_U8 ".\r\n"
            "; Com ele aqui, configuracoes e logs ficam nesta pasta (bom para pendrive).\r\n"
            "; Apague-o para guardar as configuracoes em %APPDATA%\\" APP_ID_U8 ".\r\n"
            "portable=1\r\n";
        if (!WriteFileUtf8Atomic(PortableMarkerPath(), marker)) {
            if (error) *error = "Não foi possível criar o arquivo config\\portable.ini.";
            return false;
        }
    } else {
        if (!DeleteFileW(PortableMarkerPath().c_str()) && FileExists(PortableMarkerPath())) {
            if (error) *error = "Não foi possível remover o arquivo config\\portable.ini.";
            return false;
        }
    }
    ApplyMode(portable);
    EnsureDir(p.configDir);
    EnsureDir(p.logsDir);
    if (haveOld) WriteFileUtf8Atomic(JoinPath(p.configDir, L"settings.json"), data);
    Log::Init(p.logsDir);
    Log::Info("Modo portátil %s. Configurações agora em: %s", portable ? "ligado" : "desligado", Narrow(p.configDir).c_str());
    return true;
}
