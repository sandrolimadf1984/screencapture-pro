#include "hotkeys.h"

#include "log.h"

bool Hotkeys::Reg(int id, const Hotkey& h) {
    if (registered_[id] && current_[id] == h) return true;   // já está certo
    if (registered_[id]) {
        UnregisterHotKey(hwnd_, id);
        registered_[id] = false;
    }
    failure_[id].clear();
    if (!h.enabled || !h.vk) return true;
    if (RegisterHotKey(hwnd_, id, h.mods | MOD_NOREPEAT, h.vk)) {
        registered_[id] = true;
        current_[id] = h;
        return true;
    }
    DWORD err = GetLastError();
    failure_[id] = err == ERROR_HOTKEY_ALREADY_REGISTERED ? "já está em uso por outro programa" : LastErrorText(err);
    Log::Warn("Atalho %s não registrado: %s", HotkeyText(h).c_str(), failure_[id].c_str());
    return false;
}

std::vector<std::string> Hotkeys::Apply(const Settings& s, bool recording, bool micActive) {
    std::vector<std::string> fails;
    auto un = [&](int id) {
        if (registered_[id]) { UnregisterHotKey(hwnd_, id); registered_[id] = false; }
        failure_[id].clear();
    };
    if (recording) {
        un(HK_START);
        if (!Reg(HK_PAUSE, s.hkPause)) fails.push_back(HotkeyText(s.hkPause) + " (pausar/continuar): " + failure_[HK_PAUSE]);
        if (!Reg(HK_STOP, s.hkStop)) fails.push_back(HotkeyText(s.hkStop) + " (finalizar): " + failure_[HK_STOP]);
        if (micActive) {
            if (!Reg(HK_MIC, s.hkMic)) fails.push_back(HotkeyText(s.hkMic) + " (silenciar o microfone): " + failure_[HK_MIC]);
        } else {
            un(HK_MIC);
        }
    } else {
        un(HK_PAUSE);
        un(HK_STOP);
        un(HK_MIC);
        if (!Reg(HK_START, s.hkStart)) fails.push_back(HotkeyText(s.hkStart) + " (iniciar gravação): " + failure_[HK_START]);
    }
    return fails;
}

void Hotkeys::UnregisterAll() {
    for (int id = 1; id <= 4; id++)
        if (registered_[id]) { UnregisterHotKey(hwnd_, id); registered_[id] = false; }
}
