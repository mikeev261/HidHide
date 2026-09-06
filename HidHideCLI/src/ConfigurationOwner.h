// SPDX-License-Identifier: MIT
#pragma once
#include <Windows.h>
#include <system_error>
namespace HidHide
{
    // The creating thread must retain this lease for the manager lifetime.
    // Global names arbitrate the machine-wide driver across Windows sessions.
    class ConfigurationOwner
    {
        HANDLE m_Handle{};
    public:
        explicit ConfigurationOwner(wchar_t const* name)
        {
            m_Handle = ::CreateMutexW(nullptr, FALSE, name);
            if (!m_Handle) throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "Cannot acquire profile manager ownership");
            auto const result = ::WaitForSingleObject(m_Handle, 0);
            if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
            {
                auto const error = result == WAIT_FAILED ? ::GetLastError() : ERROR_BUSY;
                ::CloseHandle(m_Handle);
                m_Handle = nullptr;
                throw std::system_error(static_cast<int>(error), std::system_category(), "Another session owns the profile manager");
            }
        }
        ~ConfigurationOwner() { if (m_Handle) { ::ReleaseMutex(m_Handle); ::CloseHandle(m_Handle); } }
        ConfigurationOwner(ConfigurationOwner const&) = delete;
        ConfigurationOwner& operator=(ConfigurationOwner const&) = delete;
    };
}
