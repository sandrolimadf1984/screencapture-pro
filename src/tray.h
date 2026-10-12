#pragma once
#include "common.h"

#include <shellapi.h>

// Ícone na bandeja do sistema (perto do relógio) com menu e notificações.
class TrayIcon {
public:
    bool Create(HWND owner, UINT callbackMsg, HICON icon, const std::wstring& tip);
    void Destroy();
    void SetIcon(HICON icon);
    void SetTip(const std::wstring& tip);
    // Notificação (balão / toast). Ao clicar nela, a janela recebe NIN_BALLOONUSERCLICK.
    void Notify(const std::string& title, const std::string& text, bool error = false);

private:
    NOTIFYICONDATAW nid_{};
    bool created_ = false;
};
