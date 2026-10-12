#pragma once
#include "common.h"
#include "settings.h"

// Atalhos globais (funcionam com qualquer programa em primeiro plano).
// Para não "roubar" teclas de outros programas à toa, o F9 só fica registrado
// enquanto o programa está esperando, e F10/F11 (e o F8 do microfone, quando
// ele está ligado) só durante a gravação.
enum HotkeyId { HK_START = 1, HK_PAUSE = 2, HK_STOP = 3, HK_MIC = 4 };

class Hotkeys {
public:
    void Attach(HWND hwnd) { hwnd_ = hwnd; }
    // Registra o conjunto certo para o estado atual. Devolve os que falharam.
    std::vector<std::string> Apply(const Settings& s, bool recording, bool micActive);
    void UnregisterAll();
    bool IsRegistered(int id) const { return id >= 1 && id <= 4 && registered_[id]; }
    std::string LastFailure(int id) const { return (id >= 1 && id <= 4) ? failure_[id] : ""; }

private:
    bool Reg(int id, const Hotkey& h);
    HWND hwnd_ = nullptr;
    bool registered_[5] = {};
    Hotkey current_[5] = {};
    std::string failure_[5];
};
