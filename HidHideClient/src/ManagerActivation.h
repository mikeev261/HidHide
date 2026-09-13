// SPDX-License-Identifier: MIT
#pragma once
#include "ConfigurationChannel.h"

namespace HidHide::ManagerActivation
{
    inline constexpr auto WindowProperty = L"HidHide.AppProfiles.ManagerWindow.v1";
    inline constexpr LRESULT Acknowledged = 1;

    inline bool SameOwner(DWORD processId, DWORD session, std::wstring const& sid)
    {
        DWORD targetSession{};
        if (!::ProcessIdToSessionId(processId, &targetSession) || targetSession != session) return false;
        try
        {
            auto process = Channel::Own(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
            HANDLE token{};
            if (!::OpenProcessToken(process.get(), TOKEN_QUERY, &token)) return false;
            auto owner = Channel::Own(token);
            return Channel::TokenSid(owner.get()) == sid;
        }
        catch (...) { return false; }
    }

    // This only activates UI. The machine-wide lease and authenticated configuration
    // channel remain the sole authority for profile and driver operations.
    inline bool ShowExisting(UINT message)
    {
        struct Request { UINT message; DWORD session; std::wstring sid; bool shown{}; };
        DWORD session{};
        if (!::ProcessIdToSessionId(::GetCurrentProcessId(), &session)) return false;
        std::wstring sid;
        try { sid = Channel::CurrentSid(); }
        catch (...) { return false; }
        Request request{ message, session, sid };
        ::EnumWindows([](HWND window, LPARAM parameter) -> BOOL
        {
            auto& request = *reinterpret_cast<Request*>(parameter);
            if (!::GetPropW(window, WindowProperty)) return TRUE;
            DWORD processId{};
            ::GetWindowThreadProcessId(window, &processId);
            if (!SameOwner(processId, request.session, request.sid)) return TRUE;
            ::AllowSetForegroundWindow(processId);
            DWORD_PTR response{};
            if (::SendMessageTimeoutW(window, request.message, 0, 0,
                SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &response) && response == Acknowledged)
            {
                request.shown = true;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&request));
        return request.shown;
    }
}
