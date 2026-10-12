#include "tray.h"

#include "log.h"

bool TrayIcon::Create(HWND owner, UINT msg, HICON icon, const std::wstring& tip) {
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = owner;
    nid_.uID = 1;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid_.uCallbackMessage = msg;
    nid_.hIcon = icon;
    wcsncpy(nid_.szTip, tip.c_str(), 127);
    created_ = Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
    if (created_) {
        nid_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid_);
    } else {
        Log::Warn("Não foi possível criar o ícone da bandeja.");
    }
    return created_;
}

void TrayIcon::Destroy() {
    if (created_) Shell_NotifyIconW(NIM_DELETE, &nid_);
    created_ = false;
}

void TrayIcon::SetIcon(HICON icon) {
    if (!created_) return;
    nid_.hIcon = icon;
    nid_.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void TrayIcon::SetTip(const std::wstring& tip) {
    if (!created_) return;
    wcsncpy(nid_.szTip, tip.c_str(), 127);
    nid_.szTip[127] = 0;
    nid_.uFlags = NIF_TIP | NIF_SHOWTIP;
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void TrayIcon::Notify(const std::string& title, const std::string& text, bool error) {
    if (!created_) return;
    std::wstring t = Widen(title), m = Widen(text);
    nid_.uFlags = NIF_INFO;
    wcsncpy(nid_.szInfoTitle, t.c_str(), 63);
    nid_.szInfoTitle[63] = 0;
    wcsncpy(nid_.szInfo, m.c_str(), 255);
    nid_.szInfo[255] = 0;
    nid_.dwInfoFlags = (error ? NIIF_ERROR : NIIF_INFO) | NIIF_RESPECT_QUIET_TIME;
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}
