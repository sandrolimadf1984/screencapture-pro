#pragma once
// ---------------------------------------------------------------------------
// Nome e identidade do programa.
// Para trocar o nome do produto no futuro, mude AQUI. O código, as mensagens,
// as pastas, o log e as informações do .exe (resources.rc) usam estes valores.
// Fora deste arquivo só ficam: o ícone (assets/*.ico) e o app.manifest
// (campos "name" e "description", que são apenas informativos).
// ---------------------------------------------------------------------------

#define APP_NAME_W        L"ScreenCapture Pro"
#define APP_NAME_U8       "ScreenCapture Pro"
#define APP_SUBTITLE_U8   "Gravador de tela"
#define APP_DESCRIPTION   "ScreenCapture Pro - gravador de tela"
#define APP_ID_W          L"ScreenCapturePro"      // pastas, mutex, classe da janela, log
#define APP_ID_U8         "ScreenCapturePro"
#define APP_EXE_U8        "ScreenCapturePro.exe"
#define APP_FILE_PREFIX   "ScreenCapture"          // prefixo padrão dos vídeos
#define APP_VERSION_U8    "1.1.1"
#define APP_VERSION_W     L"1.1.1"
#define APP_VERSION_RC    1,1,1,0                  // mesma versão, no formato do resources.rc
#define APP_AUTHOR_U8     "Sandro de Lima Pereira"
#define APP_COPYRIGHT_U8  "(c) 2026 Sandro de Lima Pereira"

// Identificadores internos derivados do nome (não aparecem para o usuário)
#define APP_MAIN_CLASS_W  APP_ID_W L"_Janela"
#define APP_MUTEX_W       L"Local\\" APP_ID_W L".Instancia"
#define APP_AUMID_W       L"Sandro." APP_ID_W      // AppUserModelID (notificações do Windows)
