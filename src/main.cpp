// Ponto de entrada do programa (nome e identidade em branding.h).
#include "app.h"

#include <shellapi.h>
#include <shobjidl.h>
#include "log.h"
#include "paths.h"
#include "win_util.h"

// Se o programa travar feio, pelo menos o log fica registrado (com a pilha de
// chamadas e a DLL culpada) e o usuário recebe uma mensagem clara em
// português (em vez da janela genérica do Windows).
static std::string ModuleAt(DWORD64 addr) {
    HMODULE mod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &mod) && mod) {
        wchar_t name[MAX_PATH] = {0};
        GetModuleFileNameW(mod, name, MAX_PATH);
        return Fmt("%s+0x%llx", Narrow(FileNameOf(name)).c_str(), (unsigned long long)(addr - (DWORD64)mod));
    }
    return Fmt("0x%llx", (unsigned long long)addr);
}

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* info) {
    static std::atomic<bool> once{false};
    if (once.exchange(true)) return EXCEPTION_EXECUTE_HANDLER;   // segunda falha: só sai
    DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    DWORD64 addr = info && info->ExceptionRecord ? (DWORD64)info->ExceptionRecord->ExceptionAddress : 0;
    Log::Error("FALHA GRAVE: exceção 0x%08lX em %s (thread %lu).", (unsigned long)code, ModuleAt(addr).c_str(),
               (unsigned long)GetCurrentThreadId());
    // pilha de chamadas (desenrolando com as tabelas de exceção do x64)
    if (info && info->ContextRecord) {
        CONTEXT ctx = *info->ContextRecord;
        std::string stack;
        for (int i = 0; i < 24 && ctx.Rip; i++) {
            stack += "\n    " + ModuleAt(ctx.Rip);
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
            if (!fn) {
                if (!ctx.Rsp) break;
                ctx.Rip = *(DWORD64*)ctx.Rsp;
                ctx.Rsp += 8;
            } else {
                PVOID handlerData = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn, &ctx, &handlerData, &establisher, nullptr);
            }
        }
        Log::Error("Pilha:%s", stack.c_str());
    }
    FatalMessage(APP_NAME_U8,
                 Fmt("O programa encontrou um erro inesperado e precisa fechar (código 0x%08lX).\n\n"
                     "Se você estava gravando, os trechos foram preservados: abra o programa de novo e escolha \"Recuperar\".\n\n"
                     "Detalhes técnicos estão na pasta \"logs\".",
                     (unsigned long)code));
    return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    {
        char v[4] = {0};
        // SCP_NO_CRASH_HANDLER=1 deixa o depurador do sistema ver a falha (só para testes)
        if (!(GetEnvironmentVariableA("SCP_NO_CRASH_HANDLER", v, sizeof(v)) && v[0] == '1')) SetUnhandledExceptionFilter(CrashHandler);
    }
    // DPI por monitor (o manifesto já pede isso; aqui é só garantia)
    using SetCtxFn = BOOL(WINAPI*)(HANDLE);
    if (auto fn = (SetCtxFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))
        fn((HANDLE)-4 /*DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2*/);

    // Só uma cópia aberta por vez: se já estiver rodando, traz a janela para frente.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, APP_MUTEX_W);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(APP_MAIN_CLASS_W, nullptr);
        if (other) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }

    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SetCurrentProcessExplicitAppUserModelID(APP_AUMID_W);
    InitPaths();
    Log::Init(Paths().logsDir);
    if (GetWindowsVersion().major < 10)
        Log::Warn("Windows anterior ao 10 detectado (%s). Suporte oficial: Windows 10/11 64 bits.", WindowsVersionText().c_str());

    int rc = 0;
    {
        App app;
        rc = app.Run(inst, show);
    }
    Log::Info("Programa encerrado (código %d).", rc);
    if (SUCCEEDED(com)) CoUninitialize();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return rc;
}
